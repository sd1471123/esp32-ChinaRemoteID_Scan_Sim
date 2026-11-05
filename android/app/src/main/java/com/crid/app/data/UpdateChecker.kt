package com.crid.app.data

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import androidx.core.content.FileProvider
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONArray
import java.io.File
import java.util.concurrent.TimeUnit

/** GitHub Release 里的一条记录（只保留 App 需要的信息）。 */
data class AppRelease(
    val version: String,
    val tag: String,
    val notes: String,
    val pageUrl: String,
    val apkUrl: String?,
    val apkName: String,
    val apkSize: Long,
)

/**
 * 版本检查：读本仓库 GitHub Releases，取版本号最高且非草稿/非预发布的一条，
 * 与本地 versionName 比较，有更高级版本就让用户直接下载并安装 APK。
 *
 * Release 约定：tag 用 v2.0.2 这种形式，附件里放一个 .apk。
 */
object UpdateChecker {

    private const val OWNER = "sd1471123"
    private const val REPO = "esp32-ChinaRemoteID_Scan_Sim"
    private const val API = "https://api.github.com/repos/$OWNER/$REPO/releases?per_page=20"

    private val client = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(20, TimeUnit.SECONDS)
        .build()

    fun currentVersion(ctx: Context): String = runCatching {
        ctx.packageManager.getPackageInfo(ctx.packageName, 0).versionName
    }.getOrNull() ?: "0.0.0"

    /** a 比 b 新返正数，相等返 0。按数字段比较，忽略前面的 v。 */
    fun compare(a: String, b: String): Int {
        val pa = nums(a)
        val pb = nums(b)
        for (i in 0 until maxOf(pa.size, pb.size)) {
            val x = pa.getOrElse(i) { 0 }
            val y = pb.getOrElse(i) { 0 }
            if (x != y) return x - y
        }
        return 0
    }

    private fun nums(v: String): List<Int> =
        Regex("\\d+").findAll(v).map { it.value.toIntOrNull() ?: 0 }.take(4).toList()

    /** 拿版本最高的那条 Release；网络失败/仓库没有 Release 返回 null。 */
    suspend fun latest(): AppRelease? = withContext(Dispatchers.IO) {
        val req = Request.Builder()
            .url(API)
            .header("Accept", "application/vnd.github+json")
            .header("User-Agent", "CRID-Android")
            .build()
        runCatching {
            client.newCall(req).execute().use { resp ->
                if (!resp.isSuccessful) return@use null
                val body = resp.body?.string() ?: return@use null
                parse(JSONArray(body))
            }
        }.getOrNull()
    }

    private fun parse(arr: JSONArray): AppRelease? {
        var best: AppRelease? = null
        for (i in 0 until arr.length()) {
            val o = arr.optJSONObject(i) ?: continue
            if (o.optBoolean("draft") || o.optBoolean("prerelease")) continue
            val tag = o.optString("tag_name").ifBlank { o.optString("name") }
            val version = tag.trim().removePrefix("v").removePrefix("V")
            if (version.isBlank()) continue

            var apkUrl: String? = null
            var apkName = ""
            var apkSize = 0L
            val assets = o.optJSONArray("assets")
            if (assets != null) {
                for (j in 0 until assets.length()) {
                    val a = assets.optJSONObject(j) ?: continue
                    val n = a.optString("name")
                    if (!n.endsWith(".apk", ignoreCase = true)) continue
                    apkUrl = a.optString("browser_download_url")
                    apkName = n
                    apkSize = a.optLong("size")
                    break
                }
            }
            val rel = AppRelease(
                version = version,
                tag = tag,
                notes = o.optString("body").trim(),
                pageUrl = o.optString("html_url"),
                apkUrl = apkUrl,
                apkName = apkName.ifBlank { "crid-$version.apk" },
                apkSize = apkSize,
            )
            val cur = best
            if (cur == null || compare(rel.version, cur.version) > 0) best = rel
        }
        return best
    }

    /** 下载 APK 到 cacheDir/updates，回调 (百分比, 已下载字节)。 */
    suspend fun download(
        ctx: Context,
        rel: AppRelease,
        onProgress: (Int, Long) -> Unit,
    ): File = withContext(Dispatchers.IO) {
        val url = rel.apkUrl ?: throw IllegalStateException("该版本没有上传 APK 附件")
        val dir = File(ctx.cacheDir, "updates").apply { mkdirs() }
        // 清掉旧包，避免和这次下载混在一起
        dir.listFiles()?.forEach { it.delete() }
        val out = File(dir, rel.apkName)

        val req = Request.Builder()
            .url(url)
            .header("User-Agent", "CRID-Android")
            .build()
        client.newCall(req).execute().use { resp ->
            if (!resp.isSuccessful) throw IllegalStateException("下载失败（HTTP ${resp.code}）")
            val body = resp.body ?: throw IllegalStateException("下载内容为空")
            val total = if (body.contentLength() > 0) body.contentLength() else rel.apkSize
            var got = 0L
            body.byteStream().use { input ->
                out.outputStream().use { output ->
                    val buf = ByteArray(64 * 1024)
                    while (true) {
                        val n = input.read(buf)
                        if (n <= 0) break
                        output.write(buf, 0, n)
                        got += n
                        val pct = if (total > 0) ((got * 100) / total).toInt() else 0
                        onProgress(pct, got)
                    }
                }
            }
        }
        out
    }

    /** 系统是否允许本应用安装 APK（Android 8+ 要单独授权）。 */
    fun canInstall(ctx: Context): Boolean = runCatching {
        ctx.packageManager.canRequestPackageInstalls()
    }.getOrDefault(true)

    /** 跳到「安装未知应用」授权页。 */
    fun openInstallSettings(ctx: Context) {
        runCatching {
            ctx.startActivity(
                Intent(
                    Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                    Uri.parse("package:${ctx.packageName}"),
                ).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
        }
    }

    /** 拉起系统安装界面。 */
    fun install(ctx: Context, file: File) {
        val uri = FileProvider.getUriForFile(ctx, "${ctx.packageName}.fileprovider", file)
        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, "application/vnd.android.package-archive")
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        ctx.startActivity(intent)
    }
}

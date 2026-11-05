package com.crid.app.data

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.decodeFromString
import kotlinx.serialization.json.Json
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody
import okhttp3.RequestBody.Companion.toRequestBody
import okio.BufferedSink
import java.io.File
import java.io.IOException
import java.net.URLEncoder
import java.security.SecureRandom
import java.security.cert.X509Certificate
import java.util.concurrent.TimeUnit
import javax.net.ssl.SSLContext
import javax.net.ssl.TrustManager
import javax.net.ssl.X509TrustManager

/**
 * 设备热点内的 HTTPS 服务（https://192.168.4.1）。
 * 固件使用自签名证书，所以这里必须跳过证书与主机名校验。
 */
class HttpApi(private val base: String = "https://192.168.4.1") {

    private val json = Json {
        ignoreUnknownKeys = true
        isLenient = true
        coerceInputValues = true
        encodeDefaults = true
    }

    private val client: OkHttpClient = run {
        val trustAll = object : X509TrustManager {
            override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?) {}
            override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?) {}
            override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
        }
        val ctx = SSLContext.getInstance("TLS")
        ctx.init(null, arrayOf<TrustManager>(trustAll), SecureRandom())
        OkHttpClient.Builder()
            .sslSocketFactory(ctx.socketFactory, trustAll)
            .hostnameVerifier { _, _ -> true }
            .connectTimeout(6, TimeUnit.SECONDS)
            .readTimeout(20, TimeUnit.SECONDS)
            .writeTimeout(60, TimeUnit.SECONDS)
            .build()
    }

    suspend fun raw(path: String, timeoutSec: Long = 10): String = getText(path, timeoutSec)

    /** 快速探测设备热点是否可达（不解析内容） */
    suspend fun probe(): Boolean = try {
        getText("/api/status", 3)
        true
    } catch (_: Exception) {
        false
    }

    private suspend fun getText(path: String, timeoutSec: Long = 10): String =
        withContext(Dispatchers.IO) {
            val req = Request.Builder().url(base + path).get().build()
            client.newBuilder()
                .readTimeout(timeoutSec, TimeUnit.SECONDS)
                .build()
                .newCall(req).execute().use { resp ->
                    val body = resp.body?.string().orEmpty()
                    if (!resp.isSuccessful) {
                        throw IOException("HTTP ${resp.code}: ${body.take(120)}")
                    }
                    body
                }
        }

    private suspend fun post(path: String, body: RequestBody, timeoutSec: Long = 20): String =
        withContext(Dispatchers.IO) {
            val req = Request.Builder().url(base + path).post(body).build()
            client.newBuilder()
                .readTimeout(timeoutSec, TimeUnit.SECONDS)
                .build()
                .newCall(req).execute().use { resp ->
                    val text = resp.body?.string().orEmpty()
                    if (!resp.isSuccessful) {
                        throw IOException("HTTP ${resp.code}: ${text.take(160)}")
                    }
                    text
                }
        }

    private fun jsonBody(obj: String): RequestBody =
        obj.toRequestBody("application/json; charset=utf-8".toMediaType())

    // ---------------- 只读接口（蓝牙或 Wi-Fi 均可） ----------------

    suspend fun rx(): RxResponse = json.decodeFromString(getText("/api/rx"))

    suspend fun status(): Status = json.decodeFromString(getText("/api/status"))

    suspend fun config(): Config = json.decodeFromString(getText("/api/config"))

    suspend fun logInfo(): LogInfo = json.decodeFromString(getText("/api/log/info"))

    suspend fun logPoints(mac: String?, offset: Int, limit: Int = 30): LogPage {
        val q = StringBuilder("/api/log/points?limit=$limit&offset=$offset")
        if (!mac.isNullOrBlank()) {
            q.append("&mac=").append(URLEncoder.encode(mac, "UTF-8"))
        }
        return json.decodeFromString(getText(q.toString()))
    }

    suspend fun otaInfo(): OtaInfo = json.decodeFromString(getText("/api/ota/info"))

    suspend fun timeGet(epochMs: Long): ApiOk =
        json.decodeFromString(getText("/api/time?ms=$epochMs"))

    suspend fun logEnableGet(on: Boolean): ApiOk =
        json.decodeFromString(getText("/api/log/enable?on=" + if (on) 1 else 0))

    // ---------------- 写接口（仅 Wi-Fi 可用） ----------------

    suspend fun postConfig(cfg: Config): ApiOk =
        json.decodeFromString(post("/api/config", jsonBody(json.encodeToString(Config.serializer(), cfg))))

    suspend fun timePost(epochMs: Long): ApiOk =
        json.decodeFromString(post("/api/time", jsonBody("""{"epoch_ms":$epochMs}""")))

    suspend fun logEnablePost(on: Boolean): ApiOk =
        json.decodeFromString(post("/api/log/enable", jsonBody("""{"on":${if (on) 1 else 0}}""")))

    suspend fun deleteLog(mac: String): ApiOk {
        val quoted = "\"" + mac.replace("\\", "\\\\").replace("\"", "\\\"") + "\""
        val body = jsonBody("""{"mac":$quoted}""")
        return json.decodeFromString(post("/api/log/del", body))
    }

    suspend fun clearLog(): ApiOk = json.decodeFromString(post("/api/log/clear", jsonBody("{}")))

    suspend fun reset(): ApiOk = json.decodeFromString(post("/api/reset", jsonBody("{}")))

    suspend fun exportCsv(mac: String?): String {
        val path = if (mac.isNullOrBlank()) "/api/log/export"
        else "/api/log/export?mac=" + URLEncoder.encode(mac, "UTF-8")
        return getText(path, timeoutSec = 40)
    }

    /** OTA 上传：裸二进制 + X-OTA-Token 头，仅 Wi-Fi */
    suspend fun otaUpload(file: File, token: String, onProgress: (sent: Long, total: Long) -> Unit) {
        val total = file.length()
        var lastCb = 0L
        val body = object : RequestBody() {
            override fun contentType() = "application/octet-stream".toMediaType()
            override fun contentLength(): Long = total
            override fun writeTo(sink: BufferedSink) {
                file.inputStream().use { input ->
                    val buf = ByteArray(8192)
                    var sent = 0L
                    while (true) {
                        val n = input.read(buf)
                        if (n < 0) break
                        sink.write(buf, 0, n)
                        sent += n
                        val now = System.currentTimeMillis()
                        if (now - lastCb > 200 || sent == total) {
                            lastCb = now
                            onProgress(sent, total)
                        }
                    }
                }
            }
        }
        val req = Request.Builder()
            .url("$base/api/ota/upload")
            .addHeader("X-OTA-Token", token)
            .post(body)
            .build()
        withContext(Dispatchers.IO) {
            client.newBuilder()
                .readTimeout(20, TimeUnit.SECONDS)
                .writeTimeout(120, TimeUnit.SECONDS)
                .build()
                .newCall(req).execute().use { resp ->
                    val text = resp.body?.string().orEmpty()
                    if (!resp.isSuccessful) {
                        throw IOException("HTTP ${resp.code}: ${text.take(160)}")
                    }
                }
        }
    }
}

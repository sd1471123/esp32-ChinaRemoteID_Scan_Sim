package com.crid.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.SystemUpdate
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.crid.app.data.AppRelease
import com.crid.app.data.UpdateChecker
import kotlinx.coroutines.launch
import java.util.Locale

private sealed interface UpdateState {
    data object Idle : UpdateState
    data object Checking : UpdateState
    data class UpToDate(val version: String) : UpdateState
    data class Found(val release: AppRelease) : UpdateState
    data class Downloading(val release: AppRelease, val percent: Int, val bytes: Long) : UpdateState
    data class Failed(val message: String) : UpdateState
}

/**
 * 顶部右侧的「检查更新」按钮：读 GitHub Releases 里版本号最高的 APK，
 * 比本地 versionName 高就提示下载并直接拉起系统安装。
 */
@Composable
fun UpdateAction() {
    val ctx = LocalContext.current
    val scope = rememberCoroutineScope()
    val current = remember { UpdateChecker.currentVersion(ctx) }
    var state by remember { mutableStateOf<UpdateState>(UpdateState.Idle) }
    var show by remember { mutableStateOf(false) }

    fun check() {
        show = true
        state = UpdateState.Checking
        scope.launch {
            val rel = UpdateChecker.latest()
            state = when {
                rel == null -> UpdateState.Failed("连不上 GitHub，或仓库里还没有 Release。")
                UpdateChecker.compare(rel.version, current) <= 0 -> UpdateState.UpToDate(current)
                else -> UpdateState.Found(rel)
            }
        }
    }

    fun startDownload(rel: AppRelease) {
        // Android 8+ 装 APK 要单独授权，没给就直接跳到授权页
        if (!UpdateChecker.canInstall(ctx)) {
            UpdateChecker.openInstallSettings(ctx)
            state = UpdateState.Failed("请先打开「允许安装未知应用」，回来再点一次下载。")
            return
        }
        state = UpdateState.Downloading(rel, 0, 0)
        scope.launch {
            runCatching {
                val file = UpdateChecker.download(ctx, rel) { pct, got ->
                    state = UpdateState.Downloading(rel, pct, got)
                }
                UpdateChecker.install(ctx, file)
            }.onFailure { e ->
                state = UpdateState.Failed(e.message ?: "下载失败")
            }
        }
    }

    IconButton(
        onClick = { check() },
        enabled = state !is UpdateState.Checking,
        // 顶栏只有 48dp 高，默认 48dp 的 IconButton 会把整行顶满，收窄到 40dp
        modifier = Modifier.width(40.dp).height(40.dp),
    ) {
        Icon(
            Icons.Filled.SystemUpdate,
            contentDescription = "检查更新",
            modifier = Modifier.width(20.dp).height(20.dp),
            tint = Color.White,
        )
    }

    if (!show) return

    val dismiss = {
        show = false
        if (state !is UpdateState.Downloading) state = UpdateState.Idle
    }

    AlertDialog(
        onDismissRequest = { if (state !is UpdateState.Downloading) dismiss() },
        title = { Text("版本更新", fontSize = 16.sp, fontWeight = FontWeight.Bold) },
        text = {
            when (val s = state) {
                UpdateState.Checking -> Row(verticalAlignment = Alignment.CenterVertically) {
                    CircularProgressIndicator(Modifier.width(18.dp).height(18.dp), strokeWidth = 2.dp)
                    Spacer(Modifier.width(10.dp))
                    Text("正在检查更新…", fontSize = 13.sp)
                }

                is UpdateState.UpToDate -> Column {
                    Text("当前已是最新版本", fontSize = 13.sp)
                    Spacer(Modifier.height(6.dp))
                    Text("v${s.version}", fontSize = 15.sp, fontWeight = FontWeight.Bold)
                }

                is UpdateState.Failed -> Column {
                    Text(s.message, fontSize = 13.sp, color = Color(0xFFB91C1C))
                    Spacer(Modifier.height(8.dp))
                    TextButton(onClick = { check() }) { Text("重试") }
                }

                is UpdateState.Found -> Column {
                    Text("当前版本 v$current", fontSize = 12.sp, color = Color(0xFF6B7280))
                    Text(
                        "发现新版本 v${s.release.version}",
                        fontSize = 15.sp,
                        fontWeight = FontWeight.Bold,
                        modifier = Modifier.padding(top = 2.dp),
                    )
                    if (s.release.notes.isNotBlank()) {
                        Spacer(Modifier.height(8.dp))
                        Text(
                            s.release.notes,
                            fontSize = 12.sp,
                            color = Color(0xFF374151),
                            modifier = Modifier
                                .heightIn(max = 180.dp)
                                .verticalScroll(rememberScrollState()),
                        )
                    }
                    Spacer(Modifier.height(8.dp))
                    Text(
                        if (s.release.apkUrl == null) "该版本没有上传 APK 附件"
                        else "安装包 ${fmtSize(s.release.apkSize)}",
                        fontSize = 11.sp,
                        color = Color(0xFF9CA3AF),
                    )
                }

                is UpdateState.Downloading -> Column {
                    Text(
                        "正在下载 v${s.release.version}",
                        fontSize = 13.sp,
                        fontWeight = FontWeight.SemiBold,
                    )
                    Spacer(Modifier.height(10.dp))
                    LinearProgressIndicator(
                        progress = { s.percent / 100f },
                        modifier = Modifier.fillMaxWidth(),
                    )
                    Spacer(Modifier.height(8.dp))
                    Text(
                        if (s.percent > 0) "${s.percent}%   ${fmtSize(s.bytes)} / ${fmtSize(s.release.apkSize)}"
                        else "正在连接…",
                        fontSize = 11.sp,
                        color = Color(0xFF6B7280),
                    )
                }

                UpdateState.Idle -> Text("", fontSize = 13.sp)
            }
        },
        confirmButton = {
            when (val s = state) {
                is UpdateState.Found -> if (s.release.apkUrl != null) {
                    TextButton(onClick = { startDownload(s.release) }) { Text("下载并安装") }
                } else {
                    TextButton(onClick = { openUrl(ctx, s.release.pageUrl); dismiss() }) { Text("打开发布页") }
                }

                is UpdateState.Downloading -> TextButton(onClick = {}, enabled = false) { Text("下载中…") }
                else -> TextButton(onClick = { dismiss() }) { Text("关闭") }
            }
        },
        dismissButton = if (state is UpdateState.Found) {
            { TextButton(onClick = { dismiss() }) { Text("稍后") } }
        } else null,
    )
}

private fun fmtSize(bytes: Long): String = when {
    bytes <= 0 -> "--"
    bytes < 1024 * 1024 -> String.format(Locale.US, "%.0f KB", bytes / 1024.0)
    else -> String.format(Locale.US, "%.1f MB", bytes / 1024.0 / 1024.0)
}

private fun openUrl(ctx: android.content.Context, url: String) {
    if (url.isBlank()) return
    runCatching {
        ctx.startActivity(
            android.content.Intent(
                android.content.Intent.ACTION_VIEW,
                android.net.Uri.parse(url),
            ).addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK)
        )
    }
}

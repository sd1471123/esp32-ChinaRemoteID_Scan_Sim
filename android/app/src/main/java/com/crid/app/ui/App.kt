package com.crid.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Map
import androidx.compose.material.icons.filled.Sensors
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Timeline
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.crid.app.ui.theme.CridDark
import com.crid.app.ui.theme.CridGreen
import com.crid.app.ui.theme.CridGreenLight
import com.crid.app.ui.theme.CridGrey

// 顶栏与底栏高度。
// 原来用 Material3 的 TopAppBar(64dp) + NavigationBar(80dp)，加上状态栏与手势条，
// 上下两条一共吃掉 200dp，800dp 高的屏幕里内容区只剩 600dp。
// 这里自己画两条紧凑栏：顶栏 48dp、底栏 54dp，文字/图标都放得下，
// 上下合计省 44dp；触控高度仍 ≥48dp。
private val TopBarHeight = 48.dp
private val BottomBarHeight = 54.dp

private val tabs = listOf(
    Triple("监控", Icons.Filled.Sensors, 0),
    Triple("地图", Icons.Filled.Map, 1),
    Triple("轨迹", Icons.Filled.Timeline, 2),
    Triple("配置", Icons.Filled.Settings, 3),
)

@Composable
fun CridApp(vm: AppViewModel) {
    val state by vm.state.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }
    var tab by rememberSaveable { mutableIntStateOf(0) }

    LaunchedEffect(state.message) {
        val msg = state.message
        if (msg != null) {
            snackbar.showSnackbar(msg)
            vm.clearMessage()
        }
    }

    val channelText = when (state.channel) {
        "ble" -> "蓝牙取数中"
        "wifi" -> "设备热点取数中"
        else -> "未连接设备"
    }

    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        topBar = { CridTopBar(channelText) },
        snackbarHost = { SnackbarHost(snackbar) },
        bottomBar = { CridBottomBar(tab) { tab = it } },
    ) { padding ->
        Box(
            Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            when (tab) {
                0 -> MonitorScreen(vm, state)
                1 -> MapScreen(state)
                2 -> LogScreen(vm, state)
                else -> ConfigScreen(vm, state)
            }
        }
    }
}

/**
 * 紧凑顶栏：标题 + 通道副标题两行塞在 48dp 里，右侧放检查更新。
 *
 * 不用 M3 的 TopAppBar——它把 64dp 写死在内部（1.3.1 没有 expandedHeight 参数），
 * 状态栏 inset 还会叠加上去，压不下来。这里自己排，statusBarsPadding 在 height 之前，
 * 才能是「状态栏 + 48dp」而不是「总高 48dp 里再扣状态栏」。
 */
@Composable
private fun CridTopBar(channelText: String) {
    Surface(color = CridDark) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .statusBarsPadding()
                .height(TopBarHeight)
                .padding(start = 16.dp, end = 2.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(
                    "CRID 扫描器",
                    fontSize = 16.sp,
                    fontWeight = FontWeight.Bold,
                    color = Color.White,
                )
                Text(channelText, fontSize = 10.sp, color = Color(0xFFB8ECD2))
            }
            UpdateAction()
        }
    }
}

/** 紧凑底栏：54dp + 手势条内边距，每项图标 18dp、文字 10sp。 */
@Composable
private fun CridBottomBar(tab: Int, onSelect: (Int) -> Unit) {
    Surface(color = Color.White, shadowElevation = 6.dp) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .navigationBarsPadding()
                .height(BottomBarHeight),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            tabs.forEach { (label, icon, index) ->
                val selected = tab == index
                Column(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .clickable { onSelect(index) },
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center,
                ) {
                    Box(
                        modifier = Modifier
                            .width(44.dp)
                            .height(22.dp)
                            .clip(RoundedCornerShape(11.dp))
                            .background(if (selected) CridGreenLight else Color.Transparent),
                        contentAlignment = Alignment.Center,
                    ) {
                        Icon(
                            icon,
                            contentDescription = label,
                            modifier = Modifier.size(18.dp),
                            tint = if (selected) CridGreen else CridGrey,
                        )
                    }
                    Text(
                        label,
                        fontSize = 10.sp,
                        color = if (selected) CridGreen else CridGrey,
                        fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
                    )
                }
            }
        }
    }
}

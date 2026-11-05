package com.crid.app.ui

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.crid.app.data.BleState
import com.crid.app.data.Track
import com.crid.app.data.UiState
import com.crid.app.ui.theme.CridDark
import com.crid.app.ui.theme.CridGreen
import com.crid.app.ui.theme.CridGrey
import com.crid.app.ui.theme.CridOrange
import com.crid.app.ui.theme.CridRed

@Composable
fun MonitorScreen(vm: AppViewModel, state: UiState) {
    val context = LocalContext.current
    var showDevices by remember { mutableStateOf(false) }
    var navTarget by remember { mutableStateOf<Track?>(null) }

    val tracks = state.rx?.tracks.orEmpty()
        .sortedWith(compareByDescending<Track> { it.active }.thenByDescending { it.rssi })
    val online = tracks.count { it.active }
    val stats = state.rx?.stats

    LazyColumn(
        modifier = Modifier.fillMaxWidth(),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(12.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        item { ConnectionCard(vm, state) { showDevices = true } }

        item {
            Card(
                shape = RoundedCornerShape(12.dp),
                colors = CardDefaults.cardColors(containerColor = Color.White),
            ) {
                Column(Modifier.padding(12.dp)) {
                    Row(
                        Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        StatChip(online.toString(), "在线目标", CridGreen, Modifier.weight(1f))
                        StatChip(stats?.rid?.toString() ?: "0", "RID 帧", CridDark, Modifier.weight(1f))
                        StatChip(stats?.total?.toString() ?: "0", "总帧数", CridDark, Modifier.weight(1f))
                    }
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(top = 8.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        StatChip(stats?.ok?.toString() ?: "0", "解析成功", CridDark, Modifier.weight(1f))
                        StatChip(stats?.ovf?.toString() ?: "0", "队列溢出", CridRed, Modifier.weight(1f))
                        StatChip(
                            if (state.timeSynced) clockText(state.status?.tnow ?: 0) else "未校准",
                            "设备时间",
                            if (state.timeSynced) CridGreen else CridOrange,
                            Modifier.weight(1f),
                        )
                    }
                }
            }
        }

        if (tracks.isEmpty()) {
            item {
                Column(
                    Modifier
                        .fillMaxWidth()
                        .background(Color(0xFFFAFBFC), RoundedCornerShape(12.dp))
                        .padding(vertical = 28.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    Text("等待 RID 广播…", fontSize = 15.sp, fontWeight = FontWeight.Bold, color = CridGrey)
                    Text(
                        if (state.channel == "none") "请先连接蓝牙或设备热点" else "当前信道没有收到广播",
                        fontSize = 12.sp,
                        color = CridGrey,
                        modifier = Modifier.padding(top = 4.dp),
                    )
                }
            }
        }

        items(tracks, key = { it.mac }) { track ->
            TrackCard(
                track = track,
                onNavigate = { navTarget = track },
            )
        }
    }

    if (showDevices) {
        BleDeviceDialog(
            state = state,
            onScan = { vm.startScan() },
            onConnect = {
                vm.connect(it)
                showDevices = false
            },
            onDismiss = {
                vm.stopScan()
                showDevices = false
            },
        )
    }

    navTarget?.let { t ->
        NavigationDialog(
            track = t,
            onDismiss = { navTarget = null },
            onSelect = { amap, operator ->
                navTarget = null
                val lat = if (operator) t.op_latitude else t.latitude
                val lon = if (operator) t.op_longitude else t.longitude
                openMapApp(context, lat, lon, t.displayName, amap)
            },
        )
    }
}

@Composable
private fun ConnectionCard(vm: AppViewModel, state: UiState, onScanClick: () -> Unit) {
    val context = LocalContext.current
    val ble = state.ble
    SectionCard("连接") {
        when (ble) {
            is BleState.Connected -> {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Dot(CridGreen)
                    Column(Modifier.weight(1f).padding(start = 6.dp)) {
                        Text(ble.name, fontSize = 13.sp, fontWeight = FontWeight.Bold)
                        Text("蓝牙已连接 · 可关闭 Wi-Fi 用流量看地图", fontSize = 11.sp, color = CridGrey)
                    }
                    OutlinedButton(onClick = { vm.disconnect() }) { Text("断开") }
                }
            }
            is BleState.Connecting -> Row(verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
                Text("正在连接 ${ble.name} …", Modifier.padding(start = 8.dp), fontSize = 13.sp)
            }
            is BleState.Failed -> {
                Text(ble.message, fontSize = 12.sp, color = CridRed)
                Row(Modifier.padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = onScanClick) { Text("搜索蓝牙设备") }
                    OutlinedButton(onClick = onScanClick) { Text("重新连接") }
                }
            }
            else -> {
                Text(
                    "读取数据：可走蓝牙（无需连热点），也可连接设备热点走 Wi-Fi。\n" +
                        "修改配置 / 升级固件必须连接设备热点。",
                    fontSize = 12.sp,
                    color = CridGrey,
                )
                Row(Modifier.padding(top = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = onScanClick, modifier = Modifier.weight(1f)) {
                        Text("连接蓝牙")
                    }
                    OutlinedButton(
                        onClick = {
                            runCatching {
                                context.startActivity(Intent(Settings.ACTION_WIFI_SETTINGS))
                            }
                        },
                        modifier = Modifier.weight(1f),
                    ) { Text("打开 Wi-Fi") }
                }
            }
        }

        if (state.channel == "wifi") {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(top = 8.dp)) {
                Dot(CridGreen)
                Text("已通过设备热点取数", Modifier.padding(start = 6.dp), fontSize = 12.sp, color = CridGreen)
            }
        }
        state.status?.let {
            Text(
                "设备：${fmt1(it.latitude)}, ${fmt1(it.longitude)} · 堆 ${it.heap / 1024}KB（最低 ${it.hmin / 1024}KB）",
                fontSize = 11.sp,
                color = CridGrey,
                modifier = Modifier.padding(top = 6.dp),
            )
        }
    }
}

@Composable
private fun Dot(color: Color) {
    Column(
        Modifier
            .size(9.dp)
            .background(color, RoundedCornerShape(50))
    ) {}
}

@Composable
private fun TrackCard(track: Track, onNavigate: () -> Unit) {
    var expanded by remember(track.mac) { mutableStateOf(false) }
    val offline = !track.active || track.age_ms > 10_000

    Card(
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(
            containerColor = if (offline) Color(0xFFF7F7F7) else Color.White,
        ),
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp),
    ) {
        Column {
            Row(
                Modifier
                    .fillMaxWidth()
                    .background(if (offline) Color(0xFFF0F1F2) else Color(0xFFF6F8FA))
                    .padding(horizontal = 10.dp, vertical = 7.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                Text(
                    protocolName(track.protocol),
                    fontSize = 9.sp,
                    fontWeight = FontWeight.Bold,
                    color = Color.White,
                    modifier = Modifier
                        .background(protocolColor(track.protocol), RoundedCornerShape(20.dp))
                        .padding(horizontal = 7.dp, vertical = 2.dp),
                )
                Text(
                    track.displayName,
                    modifier = Modifier.weight(1f),
                    fontSize = 13.sp,
                    fontWeight = FontWeight.Bold,
                    color = if (offline) CridGrey else CridDark,
                    maxLines = 1,
                )
                Text(
                    "${track.rssi} dBm",
                    fontSize = 10.sp,
                    fontFamily = FontFamily.Monospace,
                    color = if (offline) CridGrey else CridGreen,
                )
            }

            Column(Modifier.padding(horizontal = 10.dp, vertical = 8.dp)) {
                Text(
                    "${track.mac} · ${track.msg_count} 帧 · ${ageText(track.age_ms)}前",
                    fontSize = 10.sp,
                    fontFamily = FontFamily.Monospace,
                    color = CridGrey,
                )
                if (track.uas_id.isNotBlank()) {
                    Text(
                        "UAS ID: ${track.uas_id}",
                        fontSize = 11.sp,
                        fontFamily = FontFamily.Monospace,
                        color = Color(0xFF1F2937),
                        modifier = Modifier.padding(top = 2.dp),
                    )
                }

                Row(
                    Modifier
                        .fillMaxWidth()
                        .padding(top = 6.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Kv("纬度", if (track.hasPosition) fmt6(track.latitude) else "--")
                    Kv("经度", if (track.hasPosition) fmt6(track.longitude) else "--")
                    Kv("高度", fmt1(track.altitude_geo) + "m")
                }
                Row(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Kv("水平速度", fmt1(track.speed_horizontal) + "m/s")
                    Kv("垂直速度", fmt1(track.speed_vertical) + "m/s")
                    Kv("航向", fmt1(track.direction) + "°")
                }
                if (track.has_op_loc) {
                    Text(
                        "飞手: ${fmt6(track.op_latitude)}, ${fmt6(track.op_longitude)}",
                        fontSize = 10.sp,
                        color = CridOrange,
                        modifier = Modifier.padding(top = 2.dp),
                    )
                }
                if (track.realname_id.isNotBlank()) {
                    Text("实名登记: ${track.realname_id}", fontSize = 10.sp, color = CridGrey)
                }

                Row(
                    Modifier
                        .fillMaxWidth()
                        .padding(top = 6.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    OutlinedButton(
                        onClick = { expanded = !expanded },
                        modifier = Modifier.weight(1f),
                        contentPadding = androidx.compose.foundation.layout.PaddingValues(vertical = 6.dp),
                    ) {
                        Text(if (expanded) "收起详情" else "报文详情", fontSize = 12.sp)
                    }
                    OutlinedButton(
                        onClick = onNavigate,
                        modifier = Modifier.weight(1f),
                        enabled = track.hasPosition || track.has_op_loc,
                        contentPadding = androidx.compose.foundation.layout.PaddingValues(vertical = 6.dp),
                    ) {
                        Text("导航", fontSize = 12.sp)
                    }
                }

                if (expanded) {
                    HorizontalDivider(Modifier.padding(vertical = 8.dp))
                    DetailRow("MAC", track.mac)
                    DetailRow("协议", protocolName(track.protocol))
                    DetailRow("信道", track.channel.toString())
                    DetailRow("机型代码 UA Type", track.ua_type.toString())
                    DetailRow("运行状态", track.status.toString())
                    DetailRow("操作员 ID", track.operator_id.ifBlank { "--" })
                    if (!track.uas_id2.isNullOrBlank()) DetailRow("UAS ID 2", track.uas_id2!!)
                    if (track.altitude_agl != null) DetailRow("相对高度 AGL", fmt1(track.altitude_agl) + " m")
                    if (track.altitude_baro != null) DetailRow("气压高度", fmt1(track.altitude_baro) + " m")
                    if (track.agl_type != null) DetailRow("高度基准", track.agl_type.toString())
                    if (track.op_loc_type != null) DetailRow("遥控站位置类型", track.op_loc_type.toString())
                    if (track.coord_sys != null) DetailRow("坐标系统", track.coord_sys.toString())
                    if (track.h_acc != null) {
                        DetailRow("水平/垂直精度", "${track.h_acc} / ${track.v_acc}")
                        DetailRow("速度精度", track.spd_acc.toString())
                    }
                    if (track.ts_unix_ms != null) {
                        DetailRow(
                            "报文时间",
                            unixTimeText(track.ts_unix_ms!! / 1000) + " (精度 ${track.ts_acc ?: 0})",
                        )
                    }
                    DetailRow("累计帧数", "${track.msg_count} 帧")
                    DetailRow("已观测时长", ageText(track.first_age_ms))
                }
            }
        }
    }
}

@Composable
private fun DetailRow(label: String, value: String) {
    Row(
        Modifier
            .fillMaxWidth()
            .padding(vertical = 2.dp),
    ) {
        Text(label, Modifier.weight(1f), fontSize = 11.sp, color = CridGrey)
        Text(
            value,
            fontSize = 11.sp,
            fontFamily = FontFamily.Monospace,
            color = Color(0xFF1F2937),
        )
    }
}

@Composable
private fun BleDeviceDialog(
    state: UiState,
    onScan: () -> Unit,
    onConnect: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    val scanning = state.ble is BleState.Scanning
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = {
            TextButton(onClick = onDismiss) { Text("关闭") }
        },
        title = { Text("选择 ESP32-CRID 设备") },
        text = {
            Column {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    if (scanning) {
                        CircularProgressIndicator(Modifier.size(16.dp), strokeWidth = 2.dp)
                        Text(" 正在搜索…", fontSize = 12.sp)
                    } else {
                        Text(
                            if (state.devices.isEmpty()) "未发现设备，请确认设备已上电" else "共 ${state.devices.size} 台设备",
                            fontSize = 12.sp,
                            color = CridGrey,
                        )
                    }
                    TextButton(onClick = onScan) { Text("重新搜索") }
                }
                state.devices.forEach { dev ->
                    TextButton(
                        onClick = { onConnect(dev.address) },
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Column(Modifier.weight(1f), horizontalAlignment = Alignment.Start) {
                            Text(dev.name, fontSize = 13.sp, fontWeight = FontWeight.Bold)
                            Text(dev.address + " · " + dev.rssi + " dBm", fontSize = 10.sp, color = CridGrey)
                        }
                    }
                }
            }
        },
    )
}

@Composable
private fun NavigationDialog(
    track: Track,
    onDismiss: () -> Unit,
    onSelect: (amap: Boolean, operator: Boolean) -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = { TextButton(onClick = onDismiss) { Text("取消") } },
        title = { Text("导航到 ${track.displayName}") },
        text = {
            Column {
                Text(
                    "无人机 ${fmt6(track.latitude)}, ${fmt6(track.longitude)}\n" +
                        if (track.has_op_loc) "飞手 ${fmt6(track.op_latitude)}, ${fmt6(track.op_longitude)}" else "",
                    fontSize = 11.sp,
                    color = CridGrey,
                )
                TextButton(onClick = { onSelect(true, false) }, enabled = track.hasPosition) {
                    Text("高德导航到无人机")
                }
                TextButton(onClick = { onSelect(false, false) }, enabled = track.hasPosition) {
                    Text("百度导航到无人机")
                }
                if (track.has_op_loc) {
                    TextButton(onClick = { onSelect(true, true) }) { Text("高德导航到飞手") }
                    TextButton(onClick = { onSelect(false, true) }) { Text("百度导航到飞手") }
                }
            }
        },
    )
}

private fun openMapApp(context: Context, lat: Double, lon: Double, name: String, amap: Boolean) {
    if (lat == 0.0 && lon == 0.0) return
    val encodedName = Uri.encode(name)
    val candidates = if (amap) {
        listOf(
            Intent(Intent.ACTION_VIEW, Uri.parse("amapuri://route/plan/?dlat=$lat&dlon=$lon&dname=$encodedName&dev=0&t=0"))
                .setPackage("com.autonavi.minimap"),
            Intent(Intent.ACTION_VIEW, Uri.parse("https://uri.amap.com/navigation?to=$lon,$lat,$encodedName&mode=car&coordinate=wgs84&callnative=1")),
        )
    } else {
        listOf(
            Intent(Intent.ACTION_VIEW, Uri.parse("baidumap://map/direction?destination=latlng:$lat,$lon|name:$encodedName&coord_type=wgs84&mode=driving"))
                .setPackage("com.baidu.BaiduMap"),
            Intent(Intent.ACTION_VIEW, Uri.parse("https://api.map.baidu.com/direction?destination=latlng:$lat,$lon|name:$encodedName&mode=driving&coord_type=wgs84&output=html")),
        )
    }
    for (intent in candidates) {
        try {
            context.startActivity(intent)
            return
        } catch (_: Exception) {
            // 尝试下一个地图 App / 网页版
        }
    }
}

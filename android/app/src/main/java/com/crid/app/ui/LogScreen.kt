package com.crid.app.ui

import android.content.Context
import android.content.Intent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
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
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.FileProvider
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.crid.app.data.LogInfo
import com.crid.app.data.LogPoint
import com.crid.app.data.LogTarget
import com.crid.app.data.UiState
import com.crid.app.ui.theme.CridDark
import com.crid.app.ui.theme.CridGreen
import com.crid.app.ui.theme.CridGrey
import com.crid.app.ui.theme.CridRed
import kotlinx.coroutines.delay
import org.osmdroid.config.Configuration
import org.osmdroid.tileprovider.tilesource.TileSourceFactory
import org.osmdroid.util.BoundingBox
import org.osmdroid.util.GeoPoint
import org.osmdroid.views.CustomZoomButtonsController
import org.osmdroid.views.MapView
import org.osmdroid.views.overlay.Marker
import org.osmdroid.views.overlay.Polyline
import kotlin.math.ceil
import kotlin.math.min

private const val PER_PAGE = 30
private const val MAX_PAGES = 10

@Composable
fun LogScreen(vm: AppViewModel, state: UiState) {
    var selected by remember { mutableStateOf<LogTarget?>(null) }
    if (selected != null) {
        LogDetail(vm, selected!!) { selected = null }
    } else {
        LogList(vm, state) { selected = it }
    }
}

@Composable
private fun LogList(vm: AppViewModel, state: UiState, onOpen: (LogTarget) -> Unit) {
    val context = LocalContext.current
    val info = state.logInfo
    var confirmClear by remember { mutableStateOf(false) }
    var deleteMac by remember { mutableStateOf<String?>(null) }

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(12.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        item {
            SectionCard("历史轨迹") {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(
                            if (info?.en == true) "记录：开" else "记录：关",
                            fontSize = 14.sp,
                            fontWeight = FontWeight.Bold,
                            color = if (info?.en == true) CridGreen else CridGrey,
                        )
                        Text(
                            "仅接收模式写入；建议先授时再开启记录",
                            fontSize = 11.sp,
                            color = CridGrey,
                        )
                    }
                    Switch(
                        checked = info?.en == true,
                        onCheckedChange = { vm.setLogEnabled(it) },
                    )
                }

                if (info == null) {
                    Text(
                        "正在读取设备记录信息…（需要连接蓝牙或设备热点）",
                        fontSize = 12.sp,
                        color = CridGrey,
                        modifier = Modifier.padding(top = 8.dp),
                    )
                } else {
                    if (!info.ready) {
                        Text("存储分区未就绪", fontSize = 12.sp, color = CridRed)
                    }
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(top = 8.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        StatChip(info.count.toString(), "记录条数", CridDark, Modifier.weight(1f))
                        StatChip(kbText(info.used), "占用", CridDark, Modifier.weight(1f))
                        StatChip(info.targets.size.toString(), "目标数", CridDark, Modifier.weight(1f))
                    }
                    if (info.capacity > 0) {
                        LinearProgressIndicator(
                            progress = { (info.used.toFloat() / info.capacity).coerceIn(0f, 1f) },
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(top = 8.dp),
                        )
                    }
                    Text(
                        "时间范围：${unixTimeText(info.oldest_unix)} ~ ${unixTimeText(info.newest_unix)}",
                        fontSize = 11.sp,
                        color = CridGrey,
                        modifier = Modifier.padding(top = 4.dp),
                    )
                    Text(
                        "每条 64 字节，写满自动覆盖最旧记录；单目标最快 5 秒一条",
                        fontSize = 11.sp,
                        color = CridGrey,
                    )

                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(top = 8.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        OutlinedButton(
                            onClick = {
                                vm.exportCsv(null) { f -> f?.let { shareFile(context, it) } }
                            },
                            modifier = Modifier.weight(1f),
                        ) { Text("导出 CSV", fontSize = 12.sp) }
                        OutlinedButton(
                            onClick = { confirmClear = true },
                            modifier = Modifier.weight(1f),
                        ) { Text("清空记录", fontSize = 12.sp) }
                        OutlinedButton(
                            onClick = { vm.refreshLogInfo() },
                            modifier = Modifier.weight(1f),
                        ) { Text("刷新", fontSize = 12.sp) }
                    }
                }
            }
        }

        if (info != null && info.targets.isEmpty()) {
            item {
                Text(
                    "暂无历史目标。设备开启记录并收到广播后，这里会出现可回放的目标。",
                    fontSize = 12.sp,
                    color = CridGrey,
                    modifier = Modifier.padding(12.dp),
                )
            }
        }

        items(info?.targets.orEmpty(), key = { it.mac }) { target ->
            SectionCard(target.uid.ifBlank { target.mac }) {
                Text(
                    target.mac + " · " + target.count + " 条",
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace,
                    color = CridGrey,
                )
                Text(
                    "最近：${unixTimeText(target.newest_unix)}  ·  ${fmt6(target.lat)}, ${fmt6(target.lon)}",
                    fontSize = 11.sp,
                    color = CridGrey,
                )
                Row(
                    Modifier
                        .fillMaxWidth()
                        .padding(top = 6.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Button(
                        onClick = { onOpen(target) },
                        modifier = Modifier.weight(1f),
                        contentPadding = androidx.compose.foundation.layout.PaddingValues(vertical = 6.dp),
                    ) { Text("看轨迹", fontSize = 12.sp) }
                    OutlinedButton(
                        onClick = {
                            vm.exportCsv(target.mac) { f -> f?.let { shareFile(context, it) } }
                        },
                        modifier = Modifier.weight(1f),
                        contentPadding = androidx.compose.foundation.layout.PaddingValues(vertical = 6.dp),
                    ) { Text("导出", fontSize = 12.sp) }
                    OutlinedButton(
                        onClick = { deleteMac = target.mac },
                        modifier = Modifier.weight(1f),
                        contentPadding = androidx.compose.foundation.layout.PaddingValues(vertical = 6.dp),
                    ) { Text("删除", fontSize = 12.sp, color = CridRed) }
                }
            }
        }
    }

    if (confirmClear) {
        AlertDialog(
            onDismissRequest = { confirmClear = false },
            title = { Text("清空全部历史记录？") },
            text = { Text("清空后不可恢复，其他设置不受影响。") },
            confirmButton = {
                TextButton(onClick = {
                    confirmClear = false
                    vm.clearLog()
                }) { Text("清空", color = CridRed) }
            },
            dismissButton = {
                TextButton(onClick = { confirmClear = false }) { Text("取消") }
            },
        )
    }

    deleteMac?.let { mac ->
        AlertDialog(
            onDismissRequest = { deleteMac = null },
            title = { Text("删除该目标的历史轨迹？") },
            text = { Text("$mac\n只作废这台目标的记录，其他目标不受影响；重启后依然有效。") },
            confirmButton = {
                TextButton(onClick = {
                    vm.deleteLog(mac)
                    deleteMac = null
                }) { Text("删除", color = CridRed) }
            },
            dismissButton = {
                TextButton(onClick = { deleteMac = null }) { Text("取消") }
            },
        )
    }
}

@Composable
private fun LogDetail(vm: AppViewModel, target: LogTarget, onBack: () -> Unit) {
    var points by remember { mutableStateOf<List<LogPoint>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }
    var pageCount by remember { mutableIntStateOf(1) }
    var idx by remember { mutableIntStateOf(0) }
    var playing by remember { mutableStateOf(false) }
    var rate by remember { mutableIntStateOf(1) }

    LaunchedEffect(target.mac) {
        loading = true
        points = emptyList()
        val first = vm.repo.loadLogPoints(target.mac, 0, PER_PAGE)
        val matched = first?.matched ?: 0
        pageCount = min(MAX_PAGES, maxOf(1, ceil(matched.toDouble() / PER_PAGE).toInt()))
        val pages = mutableListOf<List<LogPoint>>()
        if (first != null) pages.add(first.points.reversed())
        for (p in 1 until pageCount) {
            val pg = vm.repo.loadLogPoints(target.mac, p * PER_PAGE, PER_PAGE)
            if (pg == null || pg.points.isEmpty()) break
            pages.add(pg.points.reversed())
        }
        val path = mutableListOf<LogPoint>()
        for (p in pages.indices.reversed()) path.addAll(pages[p])
        points = path
        idx = path.size - 1
        loading = false
    }

    LaunchedEffect(playing, rate) {
        while (playing && idx < points.size - 1) {
            delay(1000L / rate)
            idx++
        }
        if (idx >= points.size - 1) playing = false
    }

    Column(
        Modifier
            .fillMaxSize()
            .padding(12.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onBack) { Text("← 返回列表") }
            Text(
                target.uid.ifBlank { target.mac },
                Modifier.weight(1f),
                fontSize = 14.sp,
                fontWeight = FontWeight.Bold,
                color = CridDark,
                maxLines = 1,
            )
            if (loading) CircularProgressIndicator(Modifier.size(16.dp), strokeWidth = 2.dp)
        }

        if (loading) {
            Text(
                "正在读取 ${target.count} 条记录（最多 300 条）…",
                fontSize = 12.sp,
                color = CridGrey,
                modifier = Modifier.padding(8.dp),
            )
        } else if (points.isEmpty()) {
            Text("该目标暂无记录", fontSize = 13.sp, color = CridGrey, modifier = Modifier.padding(16.dp))
            return@Column
        }

        if (points.isNotEmpty()) {
            HistoryMap(
                points = points,
                currentIdx = idx,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(280.dp),
            )

            Row(
                Modifier
                    .fillMaxWidth()
                    .padding(top = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Button(onClick = { playing = !playing }, modifier = Modifier.weight(1f)) {
                    Text(if (playing) "暂停" else "播放", fontSize = 12.sp)
                }
                OutlinedButton(
                    onClick = {
                        playing = false
                        idx = 0
                    },
                    modifier = Modifier.weight(1f),
                ) { Text("重置", fontSize = 12.sp) }
                Text("速度", fontSize = 11.sp, color = CridGrey)
                listOf(1, 2, 4).forEach { r ->
                    FilterChip(
                        selected = rate == r,
                        onClick = { rate = r },
                        label = { Text("${r}x", fontSize = 11.sp) },
                    )
                }
            }

            Slider(
                value = idx.toFloat(),
                onValueChange = {
                    playing = false
                    idx = it.toInt()
                },
                valueRange = 0f..(points.size - 1).toFloat(),
                modifier = Modifier.fillMaxWidth(),
            )

            val p = points.getOrNull(idx)
            if (p != null) {
                Text(
                    "${fmtPointTime(p)} · ${fmt6(p.lat)}, ${fmt6(p.lon)} · 高 ${fmt1(p.alt)}m · " +
                        "${fmt1(p.spd_h)}m/s · ${fmt1(p.track)}° · ${p.rssi}dBm",
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace,
                    color = CridGreen,
                )
            }

            val pathPage = idx / PER_PAGE
            val displayPage = pageCount - 1 - pathPage
            Text(
                "第 ${displayPage + 1} / $pageCount 页 · 共 ${points.size} 个点",
                fontSize = 11.sp,
                color = CridGrey,
                modifier = Modifier.padding(top = 4.dp),
            )

            HorizontalDivider(Modifier.padding(vertical = 4.dp))

            LazyColumn(Modifier.weight(1f)) {
                val start = pathPage * PER_PAGE
                val end = min(start + PER_PAGE, points.size)
                items((start until end).toList()) { i ->
                    val pt = points[i]
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .background(if (i == idx) Color(0xFFE8F5EE) else Color.Transparent)
                            .padding(horizontal = 4.dp, vertical = 5.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                    ) {
                        Text(fmtPointTime(pt), fontSize = 10.sp, fontFamily = FontFamily.Monospace, color = CridGrey)
                        Text(fmt6(pt.lat), fontSize = 10.sp, fontFamily = FontFamily.Monospace, color = CridDark)
                        Text(fmt6(pt.lon), fontSize = 10.sp, fontFamily = FontFamily.Monospace, color = CridDark)
                        Text("${fmt1(pt.alt)}m", fontSize = 10.sp, color = CridGrey)
                        Text("${fmt1(pt.spd_h)}m/s", fontSize = 10.sp, color = CridGrey)
                    }
                }
            }
        }
    }
}

@Composable
private fun HistoryMap(points: List<LogPoint>, currentIdx: Int, modifier: Modifier) {
    val context = LocalContext.current
    val mapView = remember {
        Configuration.getInstance().userAgentValue = context.packageName
        Configuration.getInstance()
            .load(context, context.getSharedPreferences("osmdroid", Context.MODE_PRIVATE))
        MapView(context).apply {
            setTileSource(TileSourceFactory.MAPNIK)
            setMultiTouchControls(true)
            setTilesScaledToDpi(true)
            zoomController.setVisibility(CustomZoomButtonsController.Visibility.NEVER)
            controller.setZoom(15.0)
        }
    }
    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> mapView.onResume()
                Lifecycle.Event.ON_PAUSE -> mapView.onPause()
                else -> Unit
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            mapView.onDetach()
        }
    }

    var currentMarker by remember { mutableStateOf<Marker?>(null) }

    AndroidView(factory = { mapView }, modifier = modifier)

    LaunchedEffect(points) {
        mapView.overlays.clear()
        val geo = points.filter { it.lat != 0.0 || it.lon != 0.0 }
            .map { GeoPoint(it.lat, it.lon) }
        if (geo.isEmpty()) return@LaunchedEffect
        val line = Polyline().apply {
            setPoints(geo)
            outlinePaint.color = 0xFF1565C0.toInt()
            outlinePaint.strokeWidth = 4f
        }
        mapView.overlays.add(line)
        currentMarker = Marker(mapView).apply {
            position = geo.last()
            setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER)
            icon = markerIcon(context, com.crid.app.R.drawable.ic_marker_drone)
        }
        mapView.overlays.add(currentMarker)
        mapView.overlays.add(
            Marker(mapView).apply {
                position = geo.first()
                title = "起点"
                setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER)
                icon = markerIcon(context, com.crid.app.R.drawable.ic_marker_operator)
            }
        )
        val box = BoundingBox.fromGeoPoints(geo)
        runCatching { mapView.zoomToBoundingBox(box, false, 60) }
        mapView.invalidate()
    }

    LaunchedEffect(currentIdx, points) {
        val p = points.getOrNull(currentIdx) ?: return@LaunchedEffect
        currentMarker?.position = GeoPoint(p.lat, p.lon)
        currentMarker?.title = fmtPointTime(p)
        mapView.invalidate()
    }
}

private fun fmtPointTime(p: LogPoint): String =
    if (p.t > 0) unixTimeText(p.t) else {
        val s = p.tick / 1000
        "上电 " + String.format(java.util.Locale.US, "%02d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60)
    }

private fun kbText(bytes: Long): String =
    if (bytes >= 1024 * 1024) String.format(java.util.Locale.US, "%.1fMB", bytes / 1048576.0)
    else "${bytes / 1024}KB"

private fun shareFile(context: Context, file: java.io.File) {
    try {
        val uri = FileProvider.getUriForFile(context, context.packageName + ".fileprovider", file)
        val intent = Intent(Intent.ACTION_SEND).apply {
            type = "text/csv"
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        context.startActivity(Intent.createChooser(intent, "导出 CSV"))
    } catch (_: Exception) {
    }
}

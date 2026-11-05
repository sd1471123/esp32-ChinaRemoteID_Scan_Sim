package com.crid.app.ui

import android.content.Context
import android.graphics.drawable.Drawable
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.crid.app.R
import com.crid.app.data.Track
import com.crid.app.data.UiState
import com.crid.app.ui.theme.CridGreen
import com.crid.app.ui.theme.CridGrey
import com.crid.app.ui.theme.CridOrange
import org.osmdroid.config.Configuration
import org.osmdroid.tileprovider.tilesource.TileSourceFactory
import org.osmdroid.util.BoundingBox
import org.osmdroid.util.GeoPoint
import org.osmdroid.views.CustomZoomButtonsController
import org.osmdroid.views.MapView
import org.osmdroid.views.overlay.Marker

@Composable
fun MapScreen(state: UiState) {
    val tracks = state.rx?.tracks.orEmpty()
    var fitTick by remember { mutableIntStateOf(0) }

    Box(Modifier.fillMaxSize()) {
        LiveMap(tracks = tracks, fitTick = fitTick, modifier = Modifier.fillMaxSize())

        Button(
            onClick = { fitTick++ },
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(10.dp),
            colors = ButtonDefaults.buttonColors(containerColor = Color.White, contentColor = CridGreen),
        ) { Text("全览", fontSize = 12.sp) }

        Row(
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(10.dp)
                .background(Color(0xF2FFFFFF), RoundedCornerShape(8.dp))
                .padding(horizontal = 8.dp, vertical = 5.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            LegendDot(CridGreen, "无人机")
            LegendDot(CridOrange, "飞手")
            LegendDot(protocolColor(4), "GB46750")
            LegendDot(protocolColor(3), "GB42590")
            LegendDot(protocolColor(0), "ASTM")
        }

        Column(
            Modifier
                .align(Alignment.BottomEnd)
                .padding(10.dp)
                .background(Color(0xF2FFFFFF), RoundedCornerShape(8.dp))
                .padding(horizontal = 8.dp, vertical = 5.dp),
            horizontalAlignment = Alignment.End,
        ) {
            Text("在线 ${tracks.count { it.active }} / ${tracks.size}", fontSize = 11.sp, color = CridGrey)
            if (state.channel == "none") {
                Text("未连接设备", fontSize = 11.sp, color = Color(0xFFC0392B))
            }
        }
    }
}

@Composable
private fun LegendDot(color: Color, label: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier
                .size(9.dp)
                .background(color, RoundedCornerShape(50))
        )
        Text(label, Modifier.padding(start = 4.dp), fontSize = 10.sp, color = CridGrey)
    }
}

@Composable
private fun LiveMap(tracks: List<Track>, fitTick: Int, modifier: Modifier) {
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
            controller.setCenter(GeoPoint(34.0, 108.0))
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

    AndroidView(factory = { mapView }, modifier = modifier)

    var lastCount by remember { mutableIntStateOf(-1) }
    LaunchedEffect(tracks, fitTick) {
        mapView.overlays.removeAll { it is Marker }
        val pts = mutableListOf<GeoPoint>()
        tracks.forEach { t ->
            if (t.hasPosition) {
                val p = GeoPoint(t.latitude, t.longitude)
                pts.add(p)
                mapView.overlays.add(
                    Marker(mapView).apply {
                        position = p
                        title = t.displayName
                        snippet = "${protocolName(t.protocol)} · ${t.rssi} dBm"
                        setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER)
                        icon = markerIcon(context, R.drawable.ic_marker_drone)
                    }
                )
            }
            if (t.has_op_loc && (t.op_latitude != 0.0 || t.op_longitude != 0.0)) {
                val p = GeoPoint(t.op_latitude, t.op_longitude)
                pts.add(p)
                mapView.overlays.add(
                    Marker(mapView).apply {
                        position = p
                        title = "飞手 " + t.displayName
                        setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER)
                        icon = markerIcon(context, R.drawable.ic_marker_operator)
                    }
                )
            }
        }
        if (pts.isNotEmpty() && (fitTick > 0 || lastCount <= 0)) {
            val box = BoundingBox.fromGeoPoints(pts)
            runCatching { mapView.zoomToBoundingBox(box, true, 90) }
        }
        lastCount = pts.size
        mapView.invalidate()
    }
}

internal fun markerIcon(context: Context, res: Int): Drawable? {
    val d = ContextCompat.getDrawable(context, res) ?: return null
    val s = (26 * context.resources.displayMetrics.density).toInt()
    d.setBounds(0, 0, s, s)
    return d
}

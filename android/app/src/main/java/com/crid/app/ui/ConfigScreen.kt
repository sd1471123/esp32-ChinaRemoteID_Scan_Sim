package com.crid.app.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.crid.app.data.Config
import com.crid.app.data.UiState
import com.crid.app.ui.theme.CridDark
import com.crid.app.ui.theme.CridGrey
import com.crid.app.ui.theme.CridOrange
import com.crid.app.ui.theme.CridRed
import java.util.Locale

private val UA_TYPES = listOf(
    0 to "0 未声明",
    1 to "1 固定翼",
    2 to "2 多旋翼/直升机",
    3 to "3 旋翼机",
    4 to "4 垂直起降固定翼",
    5 to "5 扑翼机",
    6 to "6 滑翔机",
    7 to "7 风筝",
    8 to "8 自由气球",
    9 to "9 系留气球",
    10 to "10 飞艇",
    11 to "11 降落伞/自由落体",
    12 to "12 火箭",
    13 to "13 系留动力飞行器",
    14 to "14 地面障碍物",
    15 to "15 其他",
)

private val RUN_STATUS = listOf(
    0 to "0 未声明",
    1 to "1 地面",
    2 to "2 空中",
    3 to "3 紧急",
    4 to "4 识别故障",
)

private val GB_CATEGORY = listOf(
    0 to "0 微型",
    1 to "1 轻型",
    2 to "2 小型",
    3 to "3 中型",
    4 to "4 大型",
)

private val OPERATOR_LOC_TYPE = listOf(
    0 to "0 起飞点",
    1 to "1 实时 GNSS",
    2 to "2 固定位置",
)

private val CH5G = listOf(149 to "149", 153 to "153", 157 to "157", 161 to "161", 165 to "165")

@Composable
fun ConfigScreen(vm: AppViewModel, state: UiState) {
    val cfg = state.config

    LaunchedEffect(Unit) {
        if (state.config == null) vm.refreshConfig()
    }

    if (cfg == null) {
        Column(
            Modifier
                .fillMaxSize()
                .padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text("尚未读取到设备配置", fontSize = 15.sp, fontWeight = FontWeight.Bold, color = CridGrey)
            Text(
                "读取配置可走蓝牙；保存必须连接设备热点。",
                fontSize = 12.sp,
                color = CridGrey,
                modifier = Modifier.padding(top = 6.dp, bottom = 12.dp),
            )
            Button(onClick = { vm.refreshConfig() }) { Text("读取配置") }
        }
        return
    }

    val draft = remember(cfg) {
        mutableStateMapOf<String, String>().apply { putAll(seedDraft(cfg)) }
    }

    fun s(k: String): String = draft[k].orEmpty()
    fun set(k: String, v: String) { draft[k] = v }
    fun d(k: String): Double = draft[k]?.trim()?.toDoubleOrNull() ?: 0.0
    fun i(k: String): Int = draft[k]?.trim()?.toIntOrNull() ?: 0

    val mode = i("device_mode")
    val band = i("band")
    val has5g = cfg.has5g
    var showReset by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        if (uri != null) vm.uploadOta(uri) { }
    }

    fun buildConfig(): Config? {
        val channel = i("channel")
        val beacon = i("beacon_interval_ms")
        val spdH = d("speed_horizontal")
        val spdV = d("speed_vertical")
        val heading = d("heading")
        val aglWave = d("altitude_wave")
        val realname = s("realname_id").trim()

        error = when {
            channel !in 1..13 -> "Wi-Fi 信道需在 1-13"
            beacon !in 200..60000 -> "广播间隔需在 200-60000 ms"
            spdH !in 0.0..254.0 -> "水平速度需在 0-254 m/s"
            spdV !in -62.0..62.0 -> "垂直速度需在 -62~62 m/s"
            heading !in 0.0..359.9 -> "航向需在 0-359.9°"
            aglWave !in 0.0..100.0 -> "高度波动幅度需在 0-100 m"
            realname.length > 8 || (realname.isNotEmpty() && !realname.all { it.isLetterOrDigit() }) ->
                "实名登记号后 8 位只能包含字母或数字"
            s("ap_password").isNotEmpty() && s("ap_password").length !in 8..63 ->
                "热点密码需为 8-63 位，留空表示开放热点"
            else -> null
        }
        if (error != null) return null

        val tx2 = i("tx_2g")
        val rx2 = i("rx_2g")
        val tx5 = i("tx_5g")
        val rx5 = i("rx_5g")
        val tx = mode == 0
        val tx2n = if (band == 0) if (tx) 1 else 0 else tx2
        val rx2n = if (band == 0) if (tx) 0 else 1 else rx2
        val tx5n = if (band == 1) if (tx) 1 else 0 else tx5
        val rx5n = if (band == 1) if (tx) 0 else 1 else rx5

        return cfg.copy(
            uas_id = s("uas_id").trim(),
            drone_name = s("drone_name").trim(),
            operator_id = s("operator_id").trim(),
            ssid = s("ssid").trim(),
            ap_password = s("ap_password"),
            channel = channel,
            beacon_interval_ms = beacon,
            ua_type = i("ua_type"),
            base_latitude = d("base_latitude"),
            base_longitude = d("base_longitude"),
            base_altitude_msl = d("base_altitude_msl"),
            ground_altitude = d("ground_altitude"),
            baro_altitude = d("baro_altitude"),
            altitude_wave = aglWave,
            operator_latitude = d("operator_latitude"),
            operator_longitude = d("operator_longitude"),
            operator_altitude = d("operator_altitude"),
            altitude_agl = d("altitude_agl"),
            speed_horizontal = spdH,
            speed_vertical = spdV,
            heading = heading,
            heading_mode = i("heading_mode"),
            vspd_mode = i("vspd_mode"),
            height_type = i("height_type"),
            operator_location_type = i("operator_location_type"),
            status = i("status"),
            patrol_speed = d("patrol_speed"),
            patrol_radius_lat = d("patrol_radius_lat"),
            patrol_radius_lon = d("patrol_radius_lon"),
            device_mode = mode,
            rid_standard = i("rid_standard"),
            realname_id = realname,
            ua_category_gb = i("ua_category_gb"),
            band = if (has5g) band else 0,
            tx_2g = tx2n,
            rx_2g = rx2n,
            tx_5g = tx5n,
            rx_5g = rx5n,
            channel_5g = i("channel_5g"),
            has5g = has5g,
        )
    }

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(12.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        item {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    "配置会立即生效；Wi-Fi 热点参数变化会断开所有客户端",
                    Modifier.weight(1f),
                    fontSize = 11.sp,
                    color = CridGrey,
                )
                TextButton(onClick = { vm.refreshConfig() }) { Text("重新读取", fontSize = 12.sp) }
            }
        }

        item {
            SectionCard("设备角色与频段") {
                Text("工作模式", fontSize = 11.sp, color = CridGrey, fontWeight = FontWeight.SemiBold)
                SegRow(
                    options = listOf(0 to "发射 TX", 1 to "接收 RX"),
                    selected = mode,
                    onChange = { set("device_mode", it.toString()) },
                )
                if (mode == 0) {
                    Text(
                        "发射模式按下面参数模拟无人机广播；接收模式只扫描真实设备。",
                        fontSize = 11.sp,
                        color = CridGrey,
                        modifier = Modifier.padding(top = 4.dp),
                    )
                } else {
                    Text(
                        "接收模式下身份/飞行参数不参与广播，切回发射模式后仍保留。",
                        fontSize = 11.sp,
                        color = CridOrange,
                        modifier = Modifier.padding(top = 4.dp),
                    )
                }

                if (has5g) {
                    Text(
                        "频段",
                        fontSize = 11.sp,
                        color = CridGrey,
                        fontWeight = FontWeight.SemiBold,
                        modifier = Modifier.padding(top = 10.dp),
                    )
                    SegRow(
                        options = listOf(0 to "2.4GHz", 1 to "5.8GHz"),
                        selected = band,
                        onChange = { set("band", it.toString()) },
                    )
                } else {
                    Text(
                        "当前硬件仅支持 2.4GHz",
                        fontSize = 11.sp,
                        color = CridGrey,
                        modifier = Modifier.padding(top = 10.dp),
                    )
                }

                Text(
                    "RID 标准",
                    fontSize = 11.sp,
                    color = CridGrey,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.padding(top = 10.dp),
                )
                SegRow(
                    options = listOf(0 to "GB42590 (旧)", 1 to "GB46750 (新)"),
                    selected = i("rid_standard"),
                    onChange = { set("rid_standard", it.toString()) },
                )

                if (mode == 0) {
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(top = 10.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        if (has5g && band == 1) {
                            Column(Modifier.weight(1f)) {
                                SelectRow(
                                    "Wi-Fi 信道",
                                    CH5G,
                                    i("channel_5g"),
                                ) { set("channel_5g", it.toString()) }
                            }
                        } else {
                            LabeledField(
                                "Wi-Fi 信道 (1-13)",
                                s("channel"),
                                { set("channel", it) },
                                Modifier.weight(1f),
                            )
                        }
                        LabeledField(
                            "广播间隔 (ms, 200-60000)",
                            s("beacon_interval_ms"),
                            { set("beacon_interval_ms", it) },
                            Modifier.weight(1f),
                        )
                    }
                }
            }
        }

        if (mode == 0) {
            item {
                SectionCard("身份标识") {
                    LabeledField("UAS ID (序列号)", s("uas_id"), { set("uas_id", it) })
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(top = 6.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        LabeledField("机型名称", s("drone_name"), { set("drone_name", it) }, Modifier.weight(1f))
                        LabeledField("操作员 ID", s("operator_id"), { set("operator_id", it) }, Modifier.weight(1f))
                    }
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(top = 6.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        LabeledField(
                            "实名登记号后 8 位",
                            s("realname_id"),
                            { set("realname_id", it) },
                            Modifier.weight(1f),
                        )
                        Column(Modifier.weight(1f)) {
                            SelectRow("新国标分类", GB_CATEGORY, i("ua_category_gb")) {
                                set("ua_category_gb", it.toString())
                            }
                        }
                    }
                    Text(
                        "实名登记号仅新国标广播，留空 = 不填。",
                        fontSize = 11.sp,
                        color = CridGrey,
                        modifier = Modifier.padding(top = 4.dp),
                    )
                    SelectRow("机型代码 (UA Type)", UA_TYPES, i("ua_type")) {
                        set("ua_type", it.toString())
                    }
                }
            }
        }

        item {
            SectionCard("Wi-Fi 热点") {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    LabeledField("热点名称 SSID", s("ssid"), { set("ssid", it) }, Modifier.weight(1f))
                    LabeledField("热点密码", s("ap_password"), { set("ap_password", it) }, Modifier.weight(1f))
                }
                Text(
                    "WPA2：8-63 位；留空 = 开放热点。保存后热点立即重建，所有已连接手机都会被断开，请用新密码重连。",
                    fontSize = 11.sp,
                    color = CridOrange,
                    modifier = Modifier.padding(top = 6.dp),
                )
            }
        }

        if (mode == 0) {
            item {
                SectionCard("基准位置与高度") {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        LabeledField("基准纬度", s("base_latitude"), { set("base_latitude", it) }, Modifier.weight(1f))
                        LabeledField("基准经度", s("base_longitude"), { set("base_longitude", it) }, Modifier.weight(1f))
                    }
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(top = 6.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        LabeledField("海拔高度 m (MSL)", s("base_altitude_msl"), { set("base_altitude_msl", it) }, Modifier.weight(1f))
                        LabeledField("地面海拔 m", s("ground_altitude"), { set("ground_altitude", it) }, Modifier.weight(1f))
                    }
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(top = 6.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        LabeledField("相对高度 AGL m", s("altitude_agl"), { set("altitude_agl", it) }, Modifier.weight(1f))
                        LabeledField("气压高度 m", s("baro_altitude"), { set("baro_altitude", it) }, Modifier.weight(1f))
                    }
                }
            }

            item {
                SectionCard("飞手位置") {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        LabeledField("飞手纬度", s("operator_latitude"), { set("operator_latitude", it) }, Modifier.weight(1f))
                        LabeledField("飞手经度", s("operator_longitude"), { set("operator_longitude", it) }, Modifier.weight(1f))
                    }
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(top = 6.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        LabeledField(
                            "飞手海拔高度 m",
                            s("operator_altitude"),
                            { set("operator_altitude", it) },
                            Modifier.weight(1f),
                        )
                        SelectRow(
                            "遥控站位置类型",
                            OPERATOR_LOC_TYPE,
                            i("operator_location_type"),
                        ) { set("operator_location_type", it.toString()) }
                    }
                    Text(
                        "改飞手坐标后，无人机位置会按默认相对偏移自动生成。",
                        fontSize = 11.sp,
                        color = CridGrey,
                        modifier = Modifier.padding(top = 6.dp),
                    )
                }
            }

            item {
                SectionCard("飞行参数") {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        LabeledField("水平速度 m/s", s("speed_horizontal"), { set("speed_horizontal", it) }, Modifier.weight(1f))
                        LabeledField("垂直速度 m/s", s("speed_vertical"), { set("speed_vertical", it) }, Modifier.weight(1f))
                    }
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(top = 6.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        LabeledField("航向 °", s("heading"), { set("heading", it) }, Modifier.weight(1f))
                        Column(Modifier.weight(1f)) {
                            SelectRow("运行状态", RUN_STATUS, i("status")) { set("status", it.toString()) }
                        }
                    }
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(top = 6.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        Column(Modifier.weight(1f)) {
                            SelectRow("航向来源", listOf(0 to "自动（跟随巡游）", 1 to "固定（用左侧值）"), i("heading_mode")) {
                                set("heading_mode", it.toString())
                            }
                        }
                        Column(Modifier.weight(1f)) {
                            SelectRow("垂直速度来源", listOf(0 to "自动（跟高度波动）", 1 to "固定（用左侧值）"), i("vspd_mode")) {
                                set("vspd_mode", it.toString())
                            }
                        }
                    }
                    SelectRow(
                        "高度基准（相对高度参考）",
                        listOf(0 to "起飞点高度", 1 to "地面高度"),
                        i("height_type"),
                    ) { set("height_type", it.toString()) }
                }
            }

            item {
                SectionCard("巡游参数") {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        LabeledField("巡游速度系数", s("patrol_speed"), { set("patrol_speed", it) }, Modifier.weight(1f))
                        LabeledField("高度波动幅度 m", s("altitude_wave"), { set("altitude_wave", it) }, Modifier.weight(1f))
                    }
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(top = 6.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        LabeledField("纬度半径 (度)", s("patrol_radius_lat"), { set("patrol_radius_lat", it) }, Modifier.weight(1f))
                        LabeledField("经度半径 (度)", s("patrol_radius_lon"), { set("patrol_radius_lon", it) }, Modifier.weight(1f))
                    }
                }
            }
        }

        item {
            SectionCard("设备操作") {
                Text(
                    "设备时间：${if (state.timeSynced) clockText(state.status?.tnow ?: 0) else "未校准"}",
                    fontSize = 12.sp,
                    color = if (state.timeSynced) CridDark else CridOrange,
                )
                Row(
                    Modifier
                        .fillMaxWidth()
                        .padding(top = 8.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    OutlinedButton(
                        onClick = { vm.syncTime() },
                        modifier = Modifier.weight(1f),
                    ) { Text("用手机时间授时", fontSize = 12.sp) }
                    OutlinedButton(
                        onClick = { showReset = true },
                        modifier = Modifier.weight(1f),
                    ) { Text("恢复出厂设置", fontSize = 12.sp, color = CridRed) }
                }
                Text(
                    "授时可用蓝牙；恢复出厂需要连接设备热点。",
                    fontSize = 11.sp,
                    color = CridGrey,
                    modifier = Modifier.padding(top = 4.dp),
                )
            }
        }

        item {
            SectionCard("固件升级 OTA") {
                val info = state.otaInfo
                if (info == null) {
                    Text(
                        "连接设备热点后可读取固件版本并上传 .bin 升级包。",
                        fontSize = 12.sp,
                        color = CridGrey,
                    )
                    OutlinedButton(
                        onClick = { vm.refreshOtaInfo() },
                        modifier = Modifier.padding(top = 8.dp),
                    ) { Text("读取固件信息", fontSize = 12.sp) }
                } else {
                    Text("当前版本：${info.ver}", fontSize = 13.sp, fontWeight = FontWeight.Bold, color = CridDark)
                    Text(
                        "分区：${info.part} · 容量 ${info.part_size / 1024}KB · 上限 ${info.max / 1024}KB",
                        fontSize = 11.sp,
                        color = CridGrey,
                    )
                    Text(
                        "升级期间请保持热点连接，不要断电；失败不会影响旧固件。",
                        fontSize = 11.sp,
                        color = CridOrange,
                        modifier = Modifier.padding(top = 4.dp),
                    )
                    if (state.otaProgress != null) {
                        LinearProgressIndicator(
                            progress = { state.otaProgress ?: 0f },
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(top = 8.dp),
                        )
                        Text(
                            "已写入 ${((state.otaProgress ?: 0f) * 100).toInt()}%",
                            fontSize = 11.sp,
                            color = CridGrey,
                        )
                    }
                }
                Row(
                    Modifier
                        .fillMaxWidth()
                        .padding(top = 8.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Button(
                        onClick = { picker.launch("*/*") },
                        enabled = state.otaProgress == null,
                        modifier = Modifier.weight(1f),
                    ) {
                        Text("选择 .bin 升级", fontSize = 12.sp)
                    }
                    OutlinedButton(
                        onClick = { vm.refreshOtaInfo() },
                        modifier = Modifier.weight(1f),
                    ) { Text("刷新版本", fontSize = 12.sp) }
                }
            }
        }

        item {
            error?.let {
                Text(it, fontSize = 12.sp, color = CridRed, modifier = Modifier.padding(horizontal = 4.dp))
            }
            Button(
                onClick = {
                    val next = buildConfig()
                    if (next != null) vm.saveConfig(next)
                },
                enabled = !state.busy,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Text(if (state.busy) "保存中…" else "保存配置", fontWeight = FontWeight.Bold)
            }
            Text(
                "保存必须连接设备热点（https://192.168.4.1）。",
                fontSize = 11.sp,
                color = CridGrey,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(top = 4.dp),
            )
            HorizontalDivider(Modifier.padding(vertical = 8.dp))
        }
    }

    if (showReset) {
        AlertDialog(
            onDismissRequest = { showReset = false },
            title = { Text("恢复出厂设置？") },
            text = { Text("所有参数（含热点名称与密码）将回到默认值，设备随后重启热点。") },
            confirmButton = {
                TextButton(onClick = {
                    showReset = false
                    vm.resetConfig()
                }) { Text("恢复出厂", color = CridRed) }
            },
            dismissButton = {
                TextButton(onClick = { showReset = false }) { Text("取消") }
            },
        )
    }
}

private fun numText(v: Double): String =
    if (v == v.toLong().toDouble()) v.toLong().toString() else String.format(Locale.US, "%s", v.toString())

private fun seedDraft(c: Config): Map<String, String> = mapOf(
    "uas_id" to c.uas_id,
    "drone_name" to c.drone_name,
    "operator_id" to c.operator_id,
    "ssid" to c.ssid,
    "ap_password" to c.ap_password,
    "channel" to c.channel.toString(),
    "beacon_interval_ms" to c.beacon_interval_ms.toString(),
    "ua_type" to c.ua_type.toString(),
    "base_latitude" to numText(c.base_latitude),
    "base_longitude" to numText(c.base_longitude),
    "base_altitude_msl" to numText(c.base_altitude_msl),
    "ground_altitude" to numText(c.ground_altitude),
    "baro_altitude" to numText(c.baro_altitude),
    "altitude_wave" to numText(c.altitude_wave),
    "operator_latitude" to numText(c.operator_latitude),
    "operator_longitude" to numText(c.operator_longitude),
    "operator_altitude" to numText(c.operator_altitude),
    "altitude_agl" to numText(c.altitude_agl),
    "speed_horizontal" to numText(c.speed_horizontal),
    "speed_vertical" to numText(c.speed_vertical),
    "heading" to numText(c.heading),
    "heading_mode" to c.heading_mode.toString(),
    "vspd_mode" to c.vspd_mode.toString(),
    "height_type" to c.height_type.toString(),
    "operator_location_type" to c.operator_location_type.toString(),
    "status" to c.status.toString(),
    "patrol_speed" to numText(c.patrol_speed),
    "patrol_radius_lat" to numText(c.patrol_radius_lat),
    "patrol_radius_lon" to numText(c.patrol_radius_lon),
    "device_mode" to c.device_mode.toString(),
    "rid_standard" to c.rid_standard.toString(),
    "realname_id" to c.realname_id,
    "ua_category_gb" to c.ua_category_gb.toString(),
    "band" to c.band.toString(),
    "tx_2g" to c.tx_2g.toString(),
    "rx_2g" to c.rx_2g.toString(),
    "tx_5g" to c.tx_5g.toString(),
    "rx_5g" to c.rx_5g.toString(),
    "channel_5g" to c.channel_5g.toString(),
)

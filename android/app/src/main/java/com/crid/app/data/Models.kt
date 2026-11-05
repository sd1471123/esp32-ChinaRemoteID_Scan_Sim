package com.crid.app.data

import kotlinx.serialization.Serializable

/** 单个 Remote ID 目标（/api/rx 的 tracks 元素） */
@Serializable
data class Track(
    val mac: String = "",
    val active: Boolean = false,
    val rssi: Int = 0,
    val channel: Int = 0,
    val protocol: Int = 0,
    val uas_id: String = "",
    val drone_name: String = "",
    val operator_id: String = "",
    val realname_id: String = "",
    val ua_type: Int = 0,
    val latitude: Double = 0.0,
    val longitude: Double = 0.0,
    val altitude_geo: Double = 0.0,
    val speed_horizontal: Double = 0.0,
    val speed_vertical: Double = 0.0,
    val direction: Double = 0.0,
    val status: Int = 0,
    val has_op_loc: Boolean = false,
    val op_latitude: Double = 0.0,
    val op_longitude: Double = 0.0,
    val op_altitude_geo: Double = 0.0,
    val msg_count: Int = 0,
    val age_ms: Long = 0,
    val first_age_ms: Long = 0,
    val uas_id2: String? = null,
    val altitude_agl: Double? = null,
    val altitude_baro: Double? = null,
    val agl_type: Int? = null,
    val op_loc_type: Int? = null,
    val coord_sys: Int? = null,
    val h_acc: Int? = null,
    val v_acc: Int? = null,
    val spd_acc: Int? = null,
    val ts_unix_ms: Long? = null,
    val ts_acc: Int? = null,
    val ts_rel_10s: Int? = null,
) {
    val protocolName: String
        get() = when (protocol) {
            4 -> "GB46750"
            3 -> "GB42590"
            else -> "ASTM"
        }

    val hasPosition: Boolean
        get() = latitude != 0.0 && longitude != 0.0

    val displayName: String
        get() = drone_name.ifBlank { uas_id.ifBlank { mac } }
}

@Serializable
data class RxStats(
    val total: Long = 0,
    val rid: Long = 0,
    val ok: Long = 0,
    val ovf: Long = 0,
)

@Serializable
data class RxResponse(
    val tracks: List<Track> = emptyList(),
    val stats: RxStats = RxStats(),
)

@Serializable
data class Status(
    val latitude: Double = 0.0,
    val longitude: Double = 0.0,
    val altitude_msl: Double = 0.0,
    val heading: Double = 0.0,
    val speed: Double = 0.0,
    val heap: Long = 0,
    val hmin: Long = 0,
    val hblk: Long = 0,
    val tsync: Int = 0,
    val tnow: Long = 0,
)

/** 发射端完整配置（GET/POST /api/config） */
@Serializable
data class Config(
    val uas_id: String = "",
    val drone_name: String = "",
    val operator_id: String = "",
    val ssid: String = "",
    val ap_password: String = "",
    val channel: Int = 6,
    val beacon_interval_ms: Int = 1000,
    val ua_type: Int = 0,
    val base_latitude: Double = 0.0,
    val base_longitude: Double = 0.0,
    val base_altitude_msl: Double = 0.0,
    val ground_altitude: Double = 0.0,
    val baro_altitude: Double = 0.0,
    val altitude_wave: Double = 0.0,
    val operator_latitude: Double = 0.0,
    val operator_longitude: Double = 0.0,
    val operator_altitude: Double = 0.0,
    val altitude_agl: Double = 0.0,
    val speed_horizontal: Double = 0.0,
    val speed_vertical: Double = 0.0,
    val heading: Double = 0.0,
    val heading_mode: Int = 0,
    val vspd_mode: Int = 0,
    val height_type: Int = 0,
    val operator_location_type: Int = 0,
    val status: Int = 0,
    val patrol_speed: Double = 0.0,
    val patrol_radius_lat: Double = 0.0,
    val patrol_radius_lon: Double = 0.0,
    val device_mode: Int = 1,
    val rid_standard: Int = 0,
    val realname_id: String = "",
    val ua_category_gb: Int = 0,
    val band: Int = 0,
    val tx_2g: Int = 1,
    val rx_2g: Int = 0,
    val tx_5g: Int = 0,
    val rx_5g: Int = 0,
    val channel_5g: Int = 149,
    val has5g: Boolean = false,
)

@Serializable
data class LogTarget(
    val mac: String = "",
    val uid: String = "",
    val count: Long = 0,
    val oldest_unix: Long = 0,
    val newest_unix: Long = 0,
    val newest_tick: Long = 0,
    val lat: Double = 0.0,
    val lon: Double = 0.0,
    val alt: Double = 0.0,
)

@Serializable
data class LogInfo(
    val ready: Boolean = false,
    val en: Boolean = false,
    val count: Long = 0,
    val capacity: Long = 0,
    val used: Long = 0,
    val total: Long = 0,
    val dropped: Long = 0,
    val oldest_unix: Long = 0,
    val oldest_tick: Long = 0,
    val newest_unix: Long = 0,
    val newest_tick: Long = 0,
    val targets: List<LogTarget> = emptyList(),
)

@Serializable
data class LogPoint(
    val seq: Long = 0,
    val t: Long = 0,
    val tick: Long = 0,
    val lat: Double = 0.0,
    val lon: Double = 0.0,
    val alt: Double = 0.0,
    val agl: Double? = null,
    val spd_h: Double = 0.0,
    val spd_v: Double = 0.0,
    val track: Double = 0.0,
    val ua: Int = 0,
    val status: Int = 0,
    val proto: Int = 0,
    val rssi: Int = 0,
)

@Serializable
data class LogPage(
    val total: Long = 0,
    val matched: Long = 0,
    val offset: Long = 0,
    val limit: Long = 0,
    val mac: String = "",
    val points: List<LogPoint> = emptyList(),
)

@Serializable
data class OtaInfo(
    val ver: String = "?",
    val part: String = "?",
    val part_size: Long = 0,
    val max: Long = 0,
    val token: String = "",
    val ota: Boolean = false,
)

/** /api/time、/api/log/enable、/api/config(POST)、/api/reset 等通用回包 */
@Serializable
data class ApiOk(
    val ok: Boolean = false,
    val now: Long? = null,
    val on: Boolean? = null,
    val restarted: Boolean? = null,
    val err: String? = null,
    val error: String? = null,
)

/** 蓝牙扫描到的一台设备 */
data class BleDeviceItem(
    val name: String,
    val address: String,
    val rssi: Int,
)

sealed class BleState {
    data object Idle : BleState()
    data object Scanning : BleState()
    data class Connecting(val name: String) : BleState()
    data class Connected(val name: String, val address: String) : BleState()
    data class Failed(val message: String) : BleState()
}

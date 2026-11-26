package com.crid.app.data

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Looper
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.serialization.decodeFromString
import kotlinx.serialization.json.Json
import java.io.File
import java.io.IOException

data class UiState(
    val ble: BleState = BleState.Idle,
    val devices: List<BleDeviceItem> = emptyList(),
    /** "ble" / "wifi" / "none" */
    val channel: String = "none",
    val rx: RxResponse? = null,
    val status: Status? = null,
    val config: Config? = null,
    val logInfo: LogInfo? = null,
    val otaInfo: OtaInfo? = null,
    val timeSynced: Boolean = false,
    val busy: Boolean = false,
    val message: String? = null,
    val otaProgress: Float? = null,
    val lastUpdate: Long = 0,
)

/**
 * 双通道设备仓库：
 *  - 读（GET /api/…）：蓝牙已连接时走 BLE，否则走设备热点 HTTPS
 *  - 写（POST /api/…、OTA、CSV 导出）：必须走热点 HTTPS
 */
class DeviceRepository(context: Context) {

    private val app = context.applicationContext
    val ble = BleClient(app)
    private val http = HttpApi()

    // ---- 雷达自身坐标：手机定位通过 GET /api/selfpos 上报给设备 ----
    // 设备本身没有 GPS，TFT 雷达页要靠这里报上去的真实坐标才能按方位打点。
    // 坐标只在内存里，不落盘；设备重启后等下一次上报即可。
    private val locMgr = app.getSystemService(LocationManager::class.java)
    @Volatile private var fixLat: Double? = null
    @Volatile private var fixLon: Double? = null
    @Volatile private var fixAcc: Float = 0f
    @Volatile private var locOn = false
    private var locWarn = ""     /* 定位权限/开关的提示去重：perm / off / 空 */
    private var lastPosPush = 0L
    private val locListener = LocationListener { loc: Location ->
        fixLat = loc.latitude
        fixLon = loc.longitude
        fixAcc = if (loc.hasAccuracy()) loc.accuracy else 0f
    }
    private val json = Json {
        ignoreUnknownKeys = true
        isLenient = true
        coerceInputValues = true
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val _state = MutableStateFlow(UiState())
    val state: StateFlow<UiState> = _state.asStateFlow()

    private var pollJob: Job? = null
    private var tick = 0
    private var lastProbe = 0L
    private var lastErrAt = 0L
    private var lastErrText = ""

    // 自动授时的节流状态，见 autoSyncTime()
    private val syncLock = Mutex()
    private var lastSyncTryAt = 0L
    private var lastSyncOkAt = 0L
    private var syncFails = 0
    private var syncChannel = "none"

    private companion object {
        /** 成功后隔这么久补一次，压住设备晶振漂移 */
        const val RESYNC_MS = 10 * 60 * 1000L
        /** 两次尝试之间的最小间隔，避免轮询把失败请求刷爆 */
        const val SYNC_RETRY_GAP_MS = 8_000L
        /** 一轮里失败这么多次就闭嘴，等换通道或重新连接再重来 */
        const val SYNC_MAX_TRIES = 3
    }

    init {
        scope.launch {
            ble.state.collect { s -> _state.update { it.copy(ble = s) } }
        }
        scope.launch {
            ble.scanResults.collect { list -> _state.update { it.copy(devices = list) } }
        }
    }

    // ---------------- 轮询 ----------------

    fun startPolling() {
        if (pollJob?.isActive == true) return
        startLocation()
        pollJob = scope.launch {
            while (isActive) {
                try {
                    pollOnce()
                    // 连接后自动授时：平时只做几次比较就返回，真正发请求由内部节流决定
                    autoSyncTime()
                    // 雷达坐标：内部 4 秒节流，不会跟着 1 秒轮询刷蓝牙
                    if (_state.value.channel != "none") reportSelfPos()
                } catch (_: Exception) {
                }
                delay(1000)
            }
        }
    }

    fun stopPolling() {
        pollJob?.cancel()
        pollJob = null
        stopLocation()
    }

    private suspend fun pollOnce() {
        tick++
        val viaBle = ble.isConnected
        if (viaBle) {
            try {
                val raw = fetchJson("/api/rx", "\"tracks\"", 8_000) ?: throw IOException("回包异常")
                val rx = json.decodeFromString<RxResponse>(raw)
                _state.update {
                    it.copy(rx = rx, channel = "ble", lastUpdate = System.currentTimeMillis())
                }
                if (tick % 2 == 0) loadStatus()
                if (tick % 6 == 0) loadLogInfo()
            } catch (e: Exception) {
                pollError(e)
            }
            return
        }

        val now = System.currentTimeMillis()
        if (_state.value.channel != "wifi" && now - lastProbe < 5000) return
        lastProbe = now
        try {
            val rx = json.decodeFromString<RxResponse>(http.raw("/api/rx"))
            _state.update {
                it.copy(rx = rx, channel = "wifi", lastUpdate = now)
            }
            if (tick % 2 == 0) loadStatus()
            if (tick % 6 == 0) loadLogInfo()
        } catch (_: Exception) {
            if (_state.value.channel == "wifi") {
                _state.update { it.copy(channel = "none") }
            }
        }
    }

    private suspend fun loadStatus() {
        try {
            val raw = fetchJson("/api/status", "\"tsync\"") ?: return
            val st = json.decodeFromString<Status>(raw)
            _state.update { it.copy(status = st, timeSynced = st.tsync != 0) }
        } catch (_: Exception) {
        }
    }

    // ---------------- 连接 ----------------

    fun startScan() = ble.startScan()

    fun stopScan() = ble.stopScan()

    fun connect(address: String) {
        scope.launch {
            if (ble.connect(address)) {
                message("蓝牙已连接，可以不连热点用流量查看数据")
                // 先校时再读配置：设备刚上电时间还是 0，越早校准，轨迹里的时间越准
                autoSyncTime(force = true)
                loadConfig()
            }
        }
    }

    fun disconnect() {
        ble.disconnect()
        _state.update { it.copy(rx = null, channel = "none") }
    }

    // ---------------- 读取 ----------------

    private suspend fun fetchText(path: String, timeoutMs: Long = 8_000): String =
        if (ble.isConnected) ble.request(path, timeoutMs) else http.raw(path)

    /**
     * 读一个 GET 接口，并确认回包真的属于这个接口。
     *
     * 蓝牙是一座单通道桥：设备侧不携带请求 ID，上一个请求超时后迟到的分片有可能串进
     * 本次回包。把 `{"ok":true}` 当成 `/api/log/info` 反序列化不会报错（配置了
     * ignoreUnknownKeys），所有字段静默落成默认值 —— 界面就冒出「存储分区未就绪」
     * 这种根本不存在的故障。这里用特征键识别，不像就重发一次，仍不像返回 null，
     * 让调用方保持原有状态而不是被污染。
     */
    private suspend fun fetchJson(
        path: String,
        marker: String,
        timeoutMs: Long = 8_000,
    ): String? {
        repeat(2) {
            val txt = runCatching { fetchText(path, timeoutMs) }.getOrNull()
            if (txt != null && txt.contains(marker)) return txt
            android.util.Log.w("CRID", "回包与接口不符 $path -> ${txt?.take(160)}")
            // 拿到的明显是别的接口的回包：说明通道错位了，让蓝牙侧重新对齐，
            // 否则之后每次请求都会拿到上一份响应，永远好不了
            ble.requestResync()
        }
        return null
    }

    suspend fun loadConfig(): Config? = try {
        val raw = fetchJson("/api/config", "\"ssid\"") ?: throw IOException("回包异常")
        val c = json.decodeFromString<Config>(raw)
        _state.update { it.copy(config = c) }
        c
    } catch (e: Exception) {
        message("读取配置失败: ${e.message}")
        null
    }

    suspend fun loadLogInfo(): LogInfo? = try {
        val raw = fetchJson("/api/log/info", "\"ready\"") ?: throw IOException("回包异常")
        val info = json.decodeFromString<LogInfo>(raw)
        _state.update { it.copy(logInfo = info) }
        info
    } catch (_: Exception) {
        null
    }

    suspend fun loadLogPoints(mac: String, offset: Int, limit: Int = 30): LogPage? = try {
        val raw = fetchJson(
            "/api/log/points?limit=$limit&offset=$offset&mac=$mac", "\"points\"", 12_000
        ) ?: throw IOException("回包异常")
        json.decodeFromString<LogPage>(raw)
    } catch (e: Exception) {
        message("读取轨迹失败: ${e.message}")
        null
    }

    suspend fun loadOtaInfo(): OtaInfo? = try {
        val info = http.otaInfo()
        _state.update { it.copy(otaInfo = info) }
        info
    } catch (e: Exception) {
        message("读取固件信息失败（需连接设备热点）: ${e.message}")
        null
    }

    // ---------------- 写入 ----------------

    suspend fun saveConfig(cfg: Config): Boolean = try {
        _state.update { it.copy(busy = true) }
        // 有蓝牙就走蓝牙：设备用自己签的证书从 443 打回本机 httpd（见 crid_ble.c），
        // 手机不必连热点。写配置的请求体较大，走分帧 + 15 秒超时。
        val r: ApiOk = if (ble.isConnected) {
            json.decodeFromString<ApiOk>(
                ble.post("/api/config", http.configPayload(cfg), 15_000)
            )
        } else {
            http.postConfig(cfg)
        }
        if (r.ok) {
            _state.update { it.copy(config = cfg) }
            message(
                if (r.restarted == true) "已保存。热点/频段已变更，请重新连接设备"
                else "配置已保存并生效"
            )
            true
        } else {
            message("保存失败: " + (r.err ?: r.error ?: "设备拒绝"))
            false
        }
    } catch (e: Exception) {
        message("保存失败（蓝牙与设备热点都不通）: ${e.message}")
        false
    } finally {
        _state.update { it.copy(busy = false) }
    }

    // ---------------- 授时 ----------------

    /** 手动授时（配置页按钮）：失败原因直接弹提示 */
    suspend fun syncTime(ms: Long = System.currentTimeMillis()): Boolean {
        val (ok, err) = doSync(ms)
        message(
            when {
                ok -> "设备时间已校准"
                err != null -> "授时失败: $err"
                else -> "设备拒绝了这个时间，请检查手机系统时间"
            }
        )
        if (ok) markSynced()
        return ok
    }

    /**
     * 连接后自动授时。设备没有 RTC、也不联网，断电后时间从 0 重新开始，
     * 轨迹里的时间戳全靠这一次校准；不开自动的话，用户很容易在「未校准」
     * 状态下开记录，写下一批时间为「未知」的轨迹。
     *
     * 轮询每秒调一次，但真正发请求受三重节流：通道必须就绪、
     * 距上次成功不足 RESYNC_MS 就跳过、两次尝试至少隔 SYNC_RETRY_GAP_MS。
     */
    private suspend fun autoSyncTime(force: Boolean = false) {
        val st = _state.value
        val ch = if (ble.isConnected) "ble" else st.channel
        if (ch != "ble" && ch != "wifi") return
        if (ch != syncChannel) {
            // 换了通道（蓝牙↔热点）就重新给一份重试预算，别被上一通道的失败拖住
            syncChannel = ch
            syncFails = 0
        }
        val now = System.currentTimeMillis()
        if (!force && st.timeSynced && now - lastSyncOkAt < RESYNC_MS) return
        if (syncFails >= SYNC_MAX_TRIES) return
        if (now - lastSyncTryAt < SYNC_RETRY_GAP_MS) return
        // tryLock 而不是 lock：pollOnce 与 connect() 可能同时进来，谁抢到谁发，另一个直接跳过
        if (!syncLock.tryLock()) return
        lastSyncTryAt = now
        try {
            val (ok, err) = doSync(now)
            if (ok) {
                markSynced()
                message("已自动校时 ${clockOf(now)}（${if (ch == "ble") "蓝牙" else "热点"}）")
            } else {
                syncFails++
                if (syncFails >= SYNC_MAX_TRIES) {
                    message(
                        if (err != null) "自动授时失败: $err（可到配置页手动授时）"
                        else "自动授时被设备拒绝，请检查手机系统时间"
                    )
                }
            }
        } finally {
            syncLock.unlock()
        }
    }

    /** 真正发请求，自身不发提示；返回 (是否成功, 传输层错误描述) */
    private suspend fun doSync(ms: Long): Pair<Boolean, String?> = try {
        val r = if (ble.isConnected) {
            json.decodeFromString<ApiOk>(fetchText("/api/time?ms=$ms"))
        } else {
            http.timePost(ms)
        }
        Pair(r.ok, null)
    } catch (e: Exception) {
        Pair(false, e.message ?: "请求失败")
    }

    private fun markSynced() {
        lastSyncOkAt = System.currentTimeMillis()
        syncFails = 0
        _state.update { it.copy(timeSynced = true) }
    }

    private fun clockOf(ms: Long): String =
        java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.getDefault())
            .format(java.util.Date(ms))

    suspend fun setLogEnabled(on: Boolean): Boolean = try {
        val r = if (ble.isConnected) {
            json.decodeFromString<ApiOk>(fetchText("/api/log/enable?on=" + if (on) 1 else 0))
        } else {
            http.logEnablePost(on)
        }
        if (r.ok) {
            message(if (on) "已开启记录" else "已关闭记录")
            loadLogInfo()
            true
        } else {
            message("切换记录开关失败")
            false
        }
    } catch (e: Exception) {
        message("切换记录开关失败: ${e.message}")
        false
    }

    suspend fun deleteLog(mac: String): Boolean = try {
        // 蓝牙桥接只转发 GET，固件为删除开了 GET 变体（/api/log/del?mac=…），
        // 回包与 POST 版字段一致，所以这里只切通道、不用改解析。
        val r = if (ble.isConnected) {
            json.decodeFromString<ApiOk>(fetchText("/api/log/del?mac=$mac"))
        } else {
            http.deleteLog(mac)
        }
        if (r.ok) {
            message("已删除该目标的历史轨迹")
            loadLogInfo()
            true
        } else {
            message("删除失败: " + (r.err ?: r.error ?: "设备拒绝"))
            false
        }
    } catch (e: Exception) {
        message("删除失败（需连蓝牙或设备热点）: ${e.message}")
        false
    }

    suspend fun clearLog(): Boolean = try {
        val r = if (ble.isConnected) {
            json.decodeFromString<ApiOk>(fetchText("/api/log/clear"))
        } else {
            http.clearLog()
        }
        if (r.ok) {
            message("历史记录已清空")
            loadLogInfo()
            true
        } else {
            message("清空失败: " + (r.err ?: r.error ?: "设备拒绝"))
            false
        }
    } catch (e: Exception) {
        message("清空失败（需连蓝牙或设备热点）: ${e.message}")
        false
    }

    suspend fun resetConfig(): Boolean = try {
        val r: ApiOk = if (ble.isConnected) {
            json.decodeFromString<ApiOk>(ble.post("/api/reset", "{}", 15_000))
        } else {
            http.reset()
        }
        if (r.ok) {
            message(if (r.restarted == true) "已恢复出厂设置，热点已重建，请重新连接" else "已恢复出厂设置")
            loadConfig()
            true
        } else {
            message("恢复出厂失败")
            false
        }
    } catch (e: Exception) {
        message("恢复出厂失败（蓝牙与设备热点都不通）: ${e.message}")
        false
    }

    // ---------------- 导出 / OTA ----------------

    suspend fun exportCsv(mac: String?): File? = try {
        // 导出接口本身是 GET，蓝牙桥接放行；只有热点时走 HTTPS 更稳（不受 BLE 分片速率限制）。
        // 量很小用蓝牙没问题，满盘几 MB 时蓝牙会明显慢，界面上仍提示优先连热点。
        val csv = if (ble.isConnected) {
            // 导出可能几 MB，蓝牙分片传输比普通查询慢得多，超时放宽到 90 秒
            fetchText(if (mac.isNullOrBlank()) "/api/log/export" else "/api/log/export?mac=$mac", 90_000)
        } else {
            http.exportCsv(mac)
        }
        val dir = File(app.cacheDir, "exports").apply { mkdirs() }
        val name = if (mac.isNullOrBlank()) {
            "crid_tracks.csv"
        } else {
            "crid_track_" + mac.replace(":", "") + ".csv"
        }
        val f = File(dir, name)
        // 设备端是 chunked 分块发送的，蓝牙分片偶尔会把行尾的 \r 多发一次，文件里就出现
        // \r\r\n。Excel / 记事本遇到这种行尾会多出空行、单元格看起来错位，所以落盘前
        // 按行重组一遍：去掉全部 \r，按 \n 切行，再统一用 CRLF 写回。
        val lines = csv.replace("\r", "").split('\n').filter { it.isNotEmpty() }
        f.writeText(
            if (lines.isEmpty()) "" else lines.joinToString("\r\n", postfix = "\r\n"),
            Charsets.UTF_8,
        )
        message("CSV 已导出：${f.name}")
        f
    } catch (e: Exception) {
        message("导出失败（需连蓝牙或设备热点）: ${e.message}")
        null
    }

    suspend fun otaUpload(file: File): Boolean = try {
        val info = http.otaInfo()
        _state.update { it.copy(otaProgress = 0f, busy = true) }
        http.otaUpload(file, info.token) { sent, total ->
            val p = if (total > 0) sent.toFloat() / total else 0f
            _state.update { it.copy(otaProgress = p) }
        }
        _state.update { it.copy(otaProgress = 1f) }
        message("升级包已写入，设备正在重启…")
        true
    } catch (e: Exception) {
        message("升级失败（需连接设备热点）: ${e.message}")
        false
    } finally {
        _state.update { it.copy(busy = false, otaProgress = null) }
    }

    fun clearMessage() = _state.update { it.copy(message = null) }

    // ---------------- 手机定位（雷达自身坐标） ----------------

    @SuppressLint("MissingPermission")
    private fun startLocation() {
        if (locOn) return
        val mgr = locMgr ?: return
        val fine = ContextCompat.checkSelfPermission(
            app, Manifest.permission.ACCESS_FINE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
        val coarse = ContextCompat.checkSelfPermission(
            app, Manifest.permission.ACCESS_COARSE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
        if (!fine && !coarse) {
            if (locWarn != "perm") {
                locWarn = "perm"
                message("未授予定位权限：雷达页需要手机定位，请在系统设置里允许位置权限")
            }
            return
        }
        // GPS 和网络定位都订阅：室内 GPS 开了但没有卫星，只靠 GPS 会一直拿不到坐标
        val providers = buildList {
            if (mgr.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
                add(LocationManager.GPS_PROVIDER)
            }
            if (mgr.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
                add(LocationManager.NETWORK_PROVIDER)
            }
        }
        if (providers.isEmpty()) {
            if (locWarn != "off") {
                locWarn = "off"
                message("手机定位服务未开启：请打开 GPS 或网络定位")
            }
            return
        }
        try {
            // 先用两个 provider 里最新的已知位置兜底，再等实时更新
            providers.mapNotNull { p ->
                runCatching { mgr.getLastKnownLocation(p) }.getOrNull()
            }.maxByOrNull { it.time }?.let { locListener.onLocationChanged(it) }
            for (p in providers) {
                mgr.requestLocationUpdates(p, 2_000L, 0f, locListener, Looper.getMainLooper())
            }
            locOn = true
            locWarn = ""
        } catch (_: Exception) {
            locOn = false
        }
    }

    private fun stopLocation() {
        if (!locOn) return
        try {
            locMgr?.removeUpdates(locListener)
        } catch (_: Exception) {
        }
        locOn = false
    }

    /** 上报手机坐标；4 秒节流，失败静默（雷达页会显示 GPS 过期提示） */
    private suspend fun reportSelfPos() {
        if (!locOn) startLocation()   // 定位权限是异步授予的，这里顺带重试
        val la = fixLat ?: return
        val lo = fixLon ?: return
        val now = System.currentTimeMillis()
        if (now - lastPosPush < 4_000) return
        lastPosPush = now
        val path = String.format(
            java.util.Locale.US,
            "/api/selfpos?lat=%.6f&lon=%.6f&acc=%.0f", la, lo, fixAcc,
        )
        runCatching { fetchText(path, 5_000) }
    }

    /** 轮询每秒一次，同样的错误只在内容变化或 5 秒后再次提示，避免刷屏。 */
    private fun pollError(e: Exception) {
        val text = "蓝牙读取失败: ${e.message}"
        val now = System.currentTimeMillis()
        if (text != lastErrText || now - lastErrAt > 5_000) {
            lastErrText = text
            lastErrAt = now
            message(text)
        }
    }

    private fun message(text: String) {
        _state.update { it.copy(message = text) }
    }
}

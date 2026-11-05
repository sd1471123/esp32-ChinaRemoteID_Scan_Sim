package com.crid.app.data

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.withTimeoutOrNull
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap

/**
 * ESP32-CRID 的蓝牙取数通道。
 *
 * 设备侧是 BLE 外设（NimBLE）：
 *   service 6e400001-b5a3-f393-e0a9-e50e24dcca9e
 *   RX      6e400002-...  手机写入 UTF-8 路径（如 /api/rx）
 *   TX      6e400003-...  notify 回包
 *
 * 固件把**每一次 ATT 写入**当成一条完整请求路径入队，所以路径不能分片；
 * 同时 Android 的 GATT 同一时间只允许一个操作，MTU 协商与 CCCD 写入必须串行。
 *
 * 回包帧：0x01+4字节总长度 | 0x02+数据分片 | 0x03 结束 | 0x04+错误码
 */
class BleClient(private val context: Context) {

    companion object {
        val SERVICE: UUID = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
        val RX_CHAR: UUID = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
        val TX_CHAR: UUID = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e")
        private val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        const val NAME_PREFIX = "ESP32-CRID-"
        private const val TARGET_MTU = 247
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val adapter = context.getSystemService(BluetoothManager::class.java)?.adapter

    private val _state = MutableStateFlow<BleState>(BleState.Idle)
    val state: StateFlow<BleState> = _state

    private val _scanResults = MutableStateFlow<List<BleDeviceItem>>(emptyList())
    val scanResults: StateFlow<List<BleDeviceItem>> = _scanResults

    private val found = ConcurrentHashMap<String, BleDeviceItem>()

    private var gatt: BluetoothGatt? = null
    private var rxChar: BluetoothGattCharacteristic? = null
    private var txChar: BluetoothGattCharacteristic? = null
    @Volatile
    private var mtu = 23

    private var connectDone: CompletableDeferred<Boolean>? = null
    private var mtuDone: CompletableDeferred<Int>? = null
    private var notifyDone: CompletableDeferred<Int>? = null
    private var writeDone: CompletableDeferred<Int>? = null

    @Volatile
    private var pending: CompletableDeferred<String>? = null
    private val buffer = ByteArrayOutputStream()
    private var expected = -1

    /**
     * 帧同步门闩：只有收到 0x01 START 之后才接受 DATA/END。
     *
     * 设备端每次响应固定以 START 开头。上一个请求超时/被丢弃时，它的剩余分片可能
     * 在我们发出新请求之后才到，没有这道门闩就会被拼进新请求的缓冲里——表现为
     * JSON 多字节解析失败，或更糟：拿 `{"ok":true}` 当 `/api/log/info` 解析成功
     * （ignoreUnknownKeys 不报错，字段全落默认值，界面就显示「存储分区未就绪」）。
     */
    private var armed = false

    /** 上一次请求异常结束（超时/写入重试）后置位：下一次请求前先让残留分片流干净 */
    @Volatile
    private var resyncNeeded = false

    @Volatile
    private var ignoreFrames = false

    /**
     * 请求把通道重新对齐。
     *
     * 设备侧没有请求 ID，写入重试会让它把同一条请求处理两次、发两份响应。多出来的那份
     * 留在通道里，从此每个请求都收到「上一次」的响应——永久错一拍。表现就是
     * `/api/log/info` 永远拿到 `/api/status` 的回包，界面一直显示「存储分区未就绪」。
     * 上层发现回包与接口对不上时调这里，下一次请求前会闭眼 120ms 把积压放掉。
     */
    fun requestResync() {
        resyncNeeded = true
    }
    private var manualClose = false
    private val requestMutex = Mutex()

    val isConnected: Boolean
        get() = _state.value is BleState.Connected

    fun hasPermission(): Boolean {
        val perms = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                android.Manifest.permission.BLUETOOTH_SCAN,
                android.Manifest.permission.BLUETOOTH_CONNECT,
            )
        } else {
            arrayOf(android.Manifest.permission.ACCESS_FINE_LOCATION)
        }
        return perms.all {
            ContextCompat.checkSelfPermission(context, it) == PackageManager.PERMISSION_GRANTED
        }
    }

    // ---------------- 扫描 ----------------

    @SuppressLint("MissingPermission")
    fun startScan() {
        if (!hasPermission()) {
            _state.value = BleState.Failed("缺少蓝牙权限，请在系统设置中允许")
            return
        }
        val a = adapter ?: run {
            _state.value = BleState.Failed("此设备不支持蓝牙")
            return
        }
        if (!a.isEnabled) {
            _state.value = BleState.Failed("请先打开手机蓝牙")
            return
        }
        val scanner = a.bluetoothLeScanner ?: run {
            _state.value = BleState.Failed("蓝牙扫描不可用")
            return
        }
        found.clear()
        _scanResults.value = emptyList()
        _state.value = BleState.Scanning
        try {
            scanner.startScan(
                emptyList(),
                ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),
                scanCallback,
            )
        } catch (e: Exception) {
            _state.value = BleState.Failed("开始扫描失败: ${e.message}")
            return
        }
        scope.launch {
            delay(15_000)
            stopScan()
            if (_state.value is BleState.Scanning && found.isEmpty()) {
                _state.value = BleState.Idle
            }
        }
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        try {
            adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        } catch (_: Exception) {
        }
        if (_state.value is BleState.Scanning) _state.value = BleState.Idle
    }

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val dev = result.device ?: return
            val name = result.scanRecord?.deviceName
                ?: runCatching { dev.name }.getOrNull()
                ?: return
            if (!name.startsWith(NAME_PREFIX)) return
            found[dev.address] = BleDeviceItem(name, dev.address, result.rssi)
            _scanResults.value = found.values.sortedByDescending { it.rssi }
        }

        override fun onScanFailed(errorCode: Int) {
            _state.value = BleState.Failed("扫描失败 (code=$errorCode)")
        }
    }

    // ---------------- 连接 ----------------

    @SuppressLint("MissingPermission")
    suspend fun connect(address: String): Boolean {
        if (!hasPermission()) {
            _state.value = BleState.Failed("缺少蓝牙权限")
            return false
        }
        stopScan()
        val a = adapter ?: return false
        val dev = a.getRemoteDevice(address)
        val name = runCatching { dev.name }.getOrNull() ?: "ESP32-CRID"
        closeGatt()
        manualClose = false
        val done = CompletableDeferred<Boolean>()
        connectDone = done
        _state.value = BleState.Connecting(name)
        gatt = try {
            dev.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        } catch (e: Exception) {
            _state.value = BleState.Failed("连接失败: ${e.message}")
            return false
        }
        return try {
            val ok = withTimeout(20_000) { done.await() }
            if (!ok) closeGatt()
            ok
        } catch (e: Exception) {
            closeGatt()
            _state.value = BleState.Failed("连接超时或失败: ${e.message ?: "未知"}")
            false
        }
    }

    @SuppressLint("MissingPermission")
    fun disconnect() {
        manualClose = true
        connectDone?.complete(false)
        pending?.completeExceptionally(IOException("已断开"))
        pending = null
        closeGatt()
        _state.value = BleState.Idle
    }

    @SuppressLint("MissingPermission")
    private fun closeGatt() {
        try {
            gatt?.disconnect()
            gatt?.close()
        } catch (_: Exception) {
        }
        gatt = null
        rxChar = null
        txChar = null
        mtu = 23
        mtuDone = null
        notifyDone = null
        writeDone = null
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    failConnect(g, "连接失败 (status=$status)")
                    return
                }
                val started = runCatching { g.discoverServices() }.getOrDefault(false)
                if (!started) failConnect(g, "无法启动服务发现")
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                val wasConnected = _state.value is BleState.Connected
                closeGatt()
                connectDone?.complete(false)
                if (manualClose) {
                    _state.value = BleState.Idle
                } else {
                    val hint = if (status == 133) "连接失败，请重启手机蓝牙后再试" else "设备已断开"
                    _state.value = BleState.Failed(if (wasConnected) hint else "连接失败 ($status)")
                }
                pending?.completeExceptionally(IOException("蓝牙已断开"))
                pending = null
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                failConnect(g, "发现服务失败 ($status)")
                return
            }
            val svc = g.getService(SERVICE)
            val rx = svc?.getCharacteristic(RX_CHAR)
            val tx = svc?.getCharacteristic(TX_CHAR)
            if (rx == null || tx == null) {
                failConnect(g, "设备未提供 CRID 蓝牙服务")
                return
            }
            rxChar = rx
            txChar = tx
            scope.launch { setupChannel(g) }
        }

        override fun onMtuChanged(g: BluetoothGatt, newMtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) mtu = newMtu
            mtuDone?.complete(newMtu)
        }

        override fun onDescriptorWrite(
            g: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int,
        ) {
            if (descriptor.uuid == CCCD) notifyDone?.complete(status)
        }

        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            writeDone?.complete(status)
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION")
            handleFrame(characteristic.value ?: return)
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            handleFrame(value)
        }
    }

    /**
     * 串行完成 MTU 协商与 CCCD 写入，全部成功后才把状态置为已连接。
     * 之前在这里同时发 requestMtu 和 writeDescriptor，Android 只允许一个在途操作，
     * 后续写入会被 GATT 栈直接拒绝（表现为“蓝牙写入失败”）。
     */
    @SuppressLint("MissingPermission")
    private suspend fun setupChannel(g: BluetoothGatt) {
        try {
            requestMtuInternal(g, TARGET_MTU)
            if (g !== gatt) return

            val tx = txChar ?: run {
                failConnect(g, "蓝牙通道缺少 TX 特征")
                return
            }
            if (!g.setCharacteristicNotification(tx, true)) {
                failConnect(g, "无法开启蓝牙通知（特征不支持 notify）")
                return
            }
            val desc = tx.getDescriptor(CCCD) ?: run {
                failConnect(g, "设备缺少通知描述符（CCCD）")
                return
            }
            val d = CompletableDeferred<Int>()
            notifyDone = d
            val started = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                g.writeDescriptor(desc, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) ==
                    BluetoothGatt.GATT_SUCCESS
            } else {
                @Suppress("DEPRECATION")
                desc.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                @Suppress("DEPRECATION")
                g.writeDescriptor(desc)
            }
            if (!started) {
                failConnect(g, "开启蓝牙通知被拒绝，请重新连接设备")
                return
            }
            val st = withTimeoutOrNull(3_000) { d.await() }
            when {
                st == null -> failConnect(g, "开启蓝牙通知超时，请重新连接设备")
                st != BluetoothGatt.GATT_SUCCESS -> failConnect(g, "开启蓝牙通知失败 (status=$st)")
                else -> finishConnect(g)
            }
        } catch (e: Exception) {
            failConnect(g, "蓝牙初始化失败: ${e.message}")
        }
    }

    @SuppressLint("MissingPermission")
    private suspend fun requestMtuInternal(g: BluetoothGatt, target: Int) {
        if (mtu >= target) return
        val d = CompletableDeferred<Int>()
        mtuDone = d
        val started = runCatching { g.requestMtu(target) }.getOrDefault(false)
        if (started) withTimeoutOrNull(2_500) { d.await() }
        mtuDone = null
    }

    @SuppressLint("MissingPermission")
    private fun failConnect(g: BluetoothGatt, msg: String) {
        connectDone?.complete(false)
        _state.value = BleState.Failed(msg)
        try {
            g.disconnect()
        } catch (_: Exception) {
        }
    }

    @SuppressLint("MissingPermission")
    private fun finishConnect(g: BluetoothGatt) {
        if (g !== gatt) return
        if (connectDone?.isCompleted == false) {
            connectDone?.complete(true)
            val name = runCatching { g.device.name }.getOrNull() ?: "ESP32-CRID"
            _state.value = BleState.Connected(name, g.device.address)
        }
    }

    // ---------------- 回包解析 ----------------

    private fun handleFrame(bytes: ByteArray) {
        if (bytes.isEmpty()) return
        if (ignoreFrames) return
        when (bytes[0].toInt()) {
            0x01 -> synchronized(this) {
                if (bytes.size >= 5) {
                    expected = ((bytes[1].toInt() and 0xFF) shl 24) or
                        ((bytes[2].toInt() and 0xFF) shl 16) or
                        ((bytes[3].toInt() and 0xFF) shl 8) or
                        (bytes[4].toInt() and 0xFF)
                    buffer.reset()
                    armed = true
                }
            }
            0x02 -> synchronized(this) {
                // 没有 START 打头的分片属于上一轮请求的尾巴，丢掉
                if (armed) {
                    if (bytes.size > 1) buffer.write(bytes, 1, bytes.size - 1)
                    if (expected > 0 && buffer.size() >= expected) {
                        armed = false
                        val d = pending
                        pending = null
                        d?.complete(String(buffer.toByteArray(), Charsets.UTF_8))
                    }
                }
            }
            0x03 -> synchronized(this) {
                if (armed) {
                    armed = false
                    val d = pending
                    pending = null
                    d?.complete(String(buffer.toByteArray(), Charsets.UTF_8))
                }
            }
            0x04 -> synchronized(this) {
                if (armed) {
                    armed = false
                    val code = if (bytes.size > 1) bytes[1].toInt() else 0
                    val msg = when (code) {
                        1 -> "路径不被允许（蓝牙只支持 GET /api/*）"
                        2 -> "设备网络客户端失败"
                        3 -> "设备打开本地 HTTP 失败"
                        4 -> "设备返回非 200"
                        else -> "设备错误码 $code"
                    }
                    val d = pending
                    pending = null
                    d?.completeExceptionally(IOException(msg))
                }
            }
        }
    }

    // ---------------- 请求 ----------------

    /** 串行发送一个完整 GET 路径，等待完整回包。默认 8 秒超时（导出 CSV 这类大回包要调大）。 */
    suspend fun request(path: String, timeoutMs: Long = 8_000): String = requestMutex.withLock {
        val g = gatt ?: throw IOException("蓝牙未连接")
        if (_state.value !is BleState.Connected) throw IOException("蓝牙未连接")
        val rx = rxChar ?: throw IOException("蓝牙通道未就绪")

        val data = path.toByteArray(Charsets.UTF_8)
        if (data.size > mtu - 3) {
            requestMtuInternal(g, TARGET_MTU)
            if (data.size > mtu - 3) {
                throw IOException(
                    "请求路径 ${data.size} 字节，超过当前蓝牙单次写入上限 ${mtu - 3}（MTU=$mtu）"
                )
            }
        }

        // 上一轮如果超时或写入重试过，设备很可能已经多发了一份响应。先闭眼 120ms
        // 把这些孤儿分片放掉——设备处理一个请求通常在 50ms 内，等一下就流干净了。
        if (resyncNeeded) {
            ignoreFrames = true
            delay(120)
            synchronized(this) {
                buffer.reset()
                expected = -1
                armed = false
                ignoreFrames = false
            }
            resyncNeeded = false
        }

        val done = CompletableDeferred<String>()
        synchronized(this) {
            buffer.reset()
            expected = -1
            armed = false          // 发请求之前先把门闩落下，旧的残留分片一律不收
            pending = done
        }
        try {
            writePath(g, rx, data)
            withTimeout(timeoutMs) { done.await() }
        } catch (e: TimeoutCancellationException) {
            resyncNeeded = true
            throw IOException("蓝牙响应超时（MTU=$mtu）")
        } catch (e: Exception) {
            resyncNeeded = true
            throw e
        } finally {
            synchronized(this) {
                if (pending === done) pending = null
                armed = false
            }
        }
    }

    /**
     * 发一条带请求体的 POST（目前用于写配置 / 恢复出厂）。
     *
     * 固件原本只认「整包就是一条路径」的老格式，POST 需要分帧：
     *   头帧  0x12 | 路径长度(1) | 请求体长度(2, 小端) | 路径 | 请求体开头
     *   续帧  0x13 | 请求体后续
     * 设备端收满 body_len 字节后才会转成一次 HTTP POST，所以帧必须按顺序、
     * 不能重发（重发会让同一段 body 被写两遍）。这里每帧都走带响应的写，
     * 失败就整条放弃，由上层提示重试。
     * 响应仍是 0x01/0x02/0x03 那套通知，和 GET 共用同一个重组逻辑。
     */
    suspend fun post(path: String, body: String, timeoutMs: Long = 15_000): String =
        requestMutex.withLock {
            val g = gatt ?: throw IOException("蓝牙未连接")
            if (_state.value !is BleState.Connected) throw IOException("蓝牙未连接")
            val rx = rxChar ?: throw IOException("蓝牙通道未就绪")

            val pb = path.toByteArray(Charsets.UTF_8)
            val bb = body.toByteArray(Charsets.UTF_8)
            if (pb.size > 95) throw IOException("请求路径过长（${pb.size} 字节）")
            if (bb.size > 1536) throw IOException("请求体过长（${bb.size} 字节）")

            if (mtu - 3 < 64) requestMtuInternal(g, TARGET_MTU)

            if (resyncNeeded) {
                ignoreFrames = true
                delay(120)
                synchronized(this) {
                    buffer.reset()
                    expected = -1
                    armed = false
                    ignoreFrames = false
                }
                resyncNeeded = false
            }

            val done = CompletableDeferred<String>()
            synchronized(this) {
                buffer.reset()
                expected = -1
                armed = false
                pending = done
            }

            try {
                val chunk = (mtu - 3).coerceIn(20, 244)

                // 头帧：控制字节 + 长度 + 路径，剩下的空间优先灌请求体
                val head = ByteArrayOutputStream()
                head.write(0x12)
                head.write(pb.size)
                head.write(bb.size and 0xFF)
                head.write((bb.size shr 8) and 0xFF)
                head.write(pb)

                var off = 0
                val room = chunk - head.size()
                if (room > 0 && bb.isNotEmpty()) {
                    val n = minOf(room, bb.size)
                    head.write(bb, 0, n)
                    off = n
                }
                writeFrame(g, rx, head.toByteArray())

                // 续帧
                while (off < bb.size) {
                    val n = minOf(chunk - 1, bb.size - off)
                    val frame = ByteArray(n + 1)
                    frame[0] = 0x13
                    System.arraycopy(bb, off, frame, 1, n)
                    writeFrame(g, rx, frame)
                    off += n
                }

                withTimeout(timeoutMs) { done.await() }
            } catch (e: TimeoutCancellationException) {
                resyncNeeded = true
                throw IOException("蓝牙写配置超时（MTU=$mtu）")
            } catch (e: Exception) {
                resyncNeeded = true
                throw e
            } finally {
                synchronized(this) {
                    if (pending === done) pending = null
                    armed = false
                }
            }
        }

    /** 单次带响应写入，失败抛异常（POST 分帧不能像 GET 那样盲目重发） */
    @SuppressLint("MissingPermission")
    private suspend fun writeFrame(
        g: BluetoothGatt,
        c: BluetoothGattCharacteristic,
        data: ByteArray,
    ) {
        val st = writeOnce(g, c, data, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
        if (st != BluetoothGatt.GATT_SUCCESS) {
            throw IOException("蓝牙写入失败（status=$st，帧长 ${data.size}）")
        }
    }

    /**
     * 固件把每次写入当作一条完整路径，必须一次写完（受 MTU-3 限制）。
     * 优先带响应写入；被 GATT 栈拒绝时再退回无响应写入。
     */
    @SuppressLint("MissingPermission")
    private suspend fun writePath(
        g: BluetoothGatt,
        c: BluetoothGattCharacteristic,
        data: ByteArray,
    ) {
        val canWrite = (c.properties and BluetoothGattCharacteristic.PROPERTY_WRITE) != 0
        val canNoRsp =
            (c.properties and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0
        if (!canWrite && !canNoRsp) {
            throw IOException("RX 特征不可写 (props=${c.properties})")
        }

        var lastStatus = BluetoothGatt.GATT_FAILURE
        if (canWrite) {
            lastStatus = writeOnce(g, c, data, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
            if (lastStatus == BluetoothGatt.GATT_SUCCESS) return
            delay(150)
            lastStatus = writeOnce(g, c, data, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
            if (lastStatus == BluetoothGatt.GATT_SUCCESS) return
        }
        if (canNoRsp) {
            lastStatus = writeOnce(
                g, c, data, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            )
            if (lastStatus == BluetoothGatt.GATT_SUCCESS) {
                delay(30)
                return
            }
        }
        throw IOException(
            "蓝牙写入失败 (status=$lastStatus, MTU=$mtu, props=${c.properties})"
        )
    }

    @SuppressLint("MissingPermission")
    private suspend fun writeOnce(
        g: BluetoothGatt,
        c: BluetoothGattCharacteristic,
        value: ByteArray,
        writeType: Int,
    ): Int {
        val d = CompletableDeferred<Int>()
        writeDone = d
        val started: Int = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeCharacteristic(c, value, writeType)
        } else {
            @Suppress("DEPRECATION")
            c.writeType = writeType
            @Suppress("DEPRECATION")
            c.value = value
            @Suppress("DEPRECATION")
            if (g.writeCharacteristic(c)) BluetoothGatt.GATT_SUCCESS else BluetoothGatt.GATT_FAILURE
        }
        if (started != BluetoothGatt.GATT_SUCCESS) {
            writeDone = null
            return started
        }
        if (writeType == BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE) {
            writeDone = null
            return BluetoothGatt.GATT_SUCCESS
        }
        return try {
            withTimeoutOrNull(3_000) { d.await() } ?: BluetoothGatt.GATT_SUCCESS
        } finally {
            writeDone = null
        }
    }
}

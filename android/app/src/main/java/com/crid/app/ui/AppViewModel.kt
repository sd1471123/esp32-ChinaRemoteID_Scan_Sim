package com.crid.app.ui

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.crid.app.data.Config
import com.crid.app.data.DeviceRepository
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import java.io.File

class AppViewModel(app: Application) : AndroidViewModel(app) {

    val repo = DeviceRepository(app)
    val state = repo.state

    fun startPolling() = repo.startPolling()
    fun stopPolling() = repo.stopPolling()

    fun startScan() = repo.startScan()
    fun stopScan() = repo.stopScan()
    fun connect(address: String) = repo.connect(address)
    fun disconnect() = repo.disconnect()
    fun clearMessage() = repo.clearMessage()

    fun saveConfig(cfg: Config, onDone: (Boolean) -> Unit = {}) {
        viewModelScope.launch { onDone(repo.saveConfig(cfg)) }
    }

    fun syncTime(onDone: (Boolean) -> Unit = {}) {
        viewModelScope.launch { onDone(repo.syncTime()) }
    }

    fun setLogEnabled(on: Boolean) {
        viewModelScope.launch { repo.setLogEnabled(on) }
    }

    fun deleteLog(mac: String, onDone: (Boolean) -> Unit = {}) {
        viewModelScope.launch { onDone(repo.deleteLog(mac)) }
    }

    fun clearLog(onDone: (Boolean) -> Unit = {}) {
        viewModelScope.launch { onDone(repo.clearLog()) }
    }

    fun resetConfig(onDone: (Boolean) -> Unit = {}) {
        viewModelScope.launch { onDone(repo.resetConfig()) }
    }

    fun refreshConfig() {
        viewModelScope.launch { repo.loadConfig() }
    }

    fun refreshLogInfo() {
        viewModelScope.launch { repo.loadLogInfo() }
    }

    fun refreshOtaInfo() {
        viewModelScope.launch { repo.loadOtaInfo() }
    }

    fun loadLogPoints(mac: String, offset: Int, limit: Int, onDone: (com.crid.app.data.LogPage?) -> Unit) {
        viewModelScope.launch { onDone(repo.loadLogPoints(mac, offset, limit)) }
    }

    fun exportCsv(mac: String?, onDone: (File?) -> Unit) {
        viewModelScope.launch { onDone(repo.exportCsv(mac)) }
    }

    /** 把用户选择的 .bin 拷到缓存目录后上传 */
    fun uploadOta(uri: Uri, onDone: (Boolean) -> Unit = {}) {
        viewModelScope.launch(Dispatchers.IO) {
            val app = getApplication<Application>()
            val file = File(app.cacheDir, "firmware_ota.bin")
            try {
                app.contentResolver.openInputStream(uri)?.use { input ->
                    file.outputStream().use { out -> input.copyTo(out, 8192) }
                } ?: throw IllegalStateException("无法读取所选文件")
                onDone(repo.otaUpload(file))
            } catch (e: Exception) {
                onDone(false)
            }
        }
    }
}

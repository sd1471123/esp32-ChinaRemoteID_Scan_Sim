# ESP32-CRID 统一构建脚本
# 用法: powershell -File tools\build.ps1  (可选参数: flash / menuconfig 等，原样传给 idf.py)
#
# 说明:
# - 正常启用 ccache（不要再设 CCACHE_DISABLE=1，否则全量重编 953 个单元非常慢）
# - 之前 ccache"锁死"及链接慢(80s)的根因已定位：火绒 HipsDaemon 拦截 ld 的海量小文件读。
#   建议：火绒 -> 信任区 添加 E:\esp32-crid-1.1.0、C:\Users\pc1\.espressif、D:\esp-idf-v6.1
#   （工具 defender_exclude.ps1 是 Windows Defender 版的等效操作）
# - 项目编译单元 ~953 个，首次全量编译约几分钟属正常，之后增量/命中缓存应 <30s

# PositionalBinding=$false 是关键：否则第一个位置参数会被绑到 $Port 上，
# 跑 `build.ps1 set-target esp32c5` 时 "set-target" 会被当成串口名，
# idf.py 实际收到的是 `-p set-target esp32c5`，报 unknown target。
[CmdletBinding(PositionalBinding=$false)]
param(
  # 显式声明 -p/-Port：否则 PowerShell 会把未识别的 "-p COM3" 直接丢弃，
  # 导致 idf.py 收不到端口、esptool 自动挑串口（两块板同时插着时会挑错芯片）
  [Alias('p')][string]$Port,
  [Parameter(ValueFromRemainingArguments=$true)][string[]]$IDFArgs = @('build')
)

$ErrorActionPreference = 'Stop'

if ($Port) { $IDFArgs = @('-p', $Port) + $IDFArgs }
Write-Host "idf.py $($IDFArgs -join ' ')" -ForegroundColor DarkGray

# 每次编译前重新生成内置网页的 gzip 版（页面改动后自动同步）
$py = Get-ChildItem 'C:\Users\pc1\.workbuddy\binaries\python\versions' -Recurse -Filter python.exe -ErrorAction SilentlyContinue |
      Where-Object { $_.FullName -match '3\.13' } | Select-Object -First 1 -ExpandProperty FullName
if (-not $py) { $py = 'python' }

# 火绒 HIPS 会在上一个进程刚写完 page_gz.h 时短暂持有句柄，
# 此时 gen_gzpage.py 覆盖写会抛 PermissionError(13)。重试几次即可，不必让整次构建失败。
$gzOk = $false
for ($try = 1; $try -le 4; $try++) {
  $ErrorActionPreference = 'Continue'   # 让 python 的 traceback 变成可判断的退出码而不是终止错误
  & $py (Join-Path $PSScriptRoot 'gen_gzpage.py')
  $gzRc = $LASTEXITCODE
  $ErrorActionPreference = 'Stop'
  if ($gzRc -eq 0) { $gzOk = $true; break }
  if ($try -lt 4) {
    Write-Host "page_gz.h 写入被占用（第 $try 次），400ms 后重试..." -ForegroundColor Yellow
    Start-Sleep -Milliseconds 400
  }
}
if (-not $gzOk) {
  Write-Host "`n*** 内置网页生成失败（page_gz.h 被占用或 tools/gen_gzpage.py 出错） ***" -ForegroundColor Red
  exit 3
}

$cc = Get-ChildItem 'C:\Users\pc1\.espressif\tools\ccache' -Recurse -Filter ccache.exe |
      Select-Object -First 1 -ExpandProperty FullName
if ($cc) { & $cc --zero-stats > $null }
else { Write-Warning 'ccache not found; building without cache' }

# export.ps1 会把激活信息写到 stderr；在 $ErrorActionPreference='Stop' 下，
# 一旦调用方的输出被重定向（例如 build_trust.ps1 用 & 调用、或整体 *> 到日志），
# 这些 stderr 会变成 NativeCommandError 终止脚本，表现为「刚输出 page_gz.h 就退出」。
# 临时切回 Continue 再合并丢弃即可，不影响真正的报错（那由 idf.py 的退出码透出）。
$__eap = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& D:\esp-idf-v6.1\export.ps1 2>&1 | Out-Null
$ErrorActionPreference = $__eap
# 用 .NET API 清掉：Remove-Item Env:X 在部分环境（安全删除钩子）会 fail-closed 抛错
[System.Environment]::SetEnvironmentVariable('CCACHE_DISABLE', $null)

# 全局 ccache.conf 设了 sloppiness=...time_macros，会让 __DATE__/__TIME__ 被忽略，
# 导致 esp_app_desc.c 永远命中旧缓存 -> 串口启动日志里的 Compile time 是几天前的旧值，
# 无法用启动日志判断板上固件版本。这里对本项目覆盖掉 time_macros（只多编 1 个文件）。
$env:CCACHE_SLOPPINESS = 'locale, pch_defines'

# __DATE__/__TIME__ 变化 ninja 感知不到（源文件 mtime 没变就不会重编），
# 所以每次构建前删掉 app 描述的目标文件，强制重编译这一个单元（开销可忽略）。
# 注意必须跟着 -B 走：多目标构建时产物在 build_c5\ 而不是 build\，
# 只删 build\ 会让 C5 的 Compile time 一直是旧值。
$buildDir = 'build'
for ($i = 0; $i -lt $IDFArgs.Count - 1; $i++) {
  if ($IDFArgs[$i] -eq '-B') { $buildDir = $IDFArgs[$i + 1]; break }
}
# 先 Test-Path 再删：管道为空时 Remove-Item 会报 missing path operand，
# 而部分环境的安全删除钩子会把它变成终止错误
$__descObj = Join-Path $buildDir 'esp-idf\esp_app_format\CMakeFiles\__idf_esp_app_format.dir\esp_app_desc.c.obj'
if (Test-Path $__descObj) { Remove-Item -Force $__descObj }

$t = [System.Diagnostics.Stopwatch]::StartNew()
idf.py @IDFArgs
$rc = $LASTEXITCODE
$t.Stop()

if ($cc -and ($IDFArgs -notcontains 'fullclean') -and ($IDFArgs -notcontains 'menuconfig')) {
  Write-Host "`n=== build time: $([math]::Round($t.Elapsed.TotalSeconds,1)) s ===" -ForegroundColor Cyan
  & $cc --show-stats | Select-String 'Hits|Misses|Cache size'
}

# 必须把 idf.py 的退出码透出，否则烧录/编译失败也会被当成成功
if ($rc -ne 0) { Write-Host "`n*** idf.py failed (exit=$rc) ***" -ForegroundColor Red }
exit $rc

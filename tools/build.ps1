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
& $py (Join-Path $PSScriptRoot 'gen_gzpage.py')

$cc = Get-ChildItem 'C:\Users\pc1\.espressif\tools\ccache' -Recurse -Filter ccache.exe |
      Select-Object -First 1 -ExpandProperty FullName
if ($cc) { & $cc --zero-stats > $null }
else { Write-Warning 'ccache not found; building without cache' }

& D:\esp-idf-v6.1\export.ps1 | Out-Null
Remove-Item Env:CCACHE_DISABLE -ErrorAction SilentlyContinue

# 全局 ccache.conf 设了 sloppiness=...time_macros，会让 __DATE__/__TIME__ 被忽略，
# 导致 esp_app_desc.c 永远命中旧缓存 -> 串口启动日志里的 Compile time 是几天前的旧值，
# 无法用启动日志判断板上固件版本。这里对本项目覆盖掉 time_macros（只多编 1 个文件）。
$env:CCACHE_SLOPPINESS = 'locale, pch_defines'

# __DATE__/__TIME__ 变化 ninja 感知不到（源文件 mtime 没变就不会重编），
# 所以每次构建前删掉 app 描述的目标文件，强制重编译这一个单元（开销可忽略）。
Get-ChildItem 'build\esp-idf\esp_app_format\CMakeFiles\__idf_esp_app_format.dir\esp_app_desc.c.obj' -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue

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

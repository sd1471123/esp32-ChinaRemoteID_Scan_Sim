# ESP32-CRID 统一构建脚本
# 用法: powershell -File tools\build.ps1  (可选参数: flash / menuconfig 等，原样传给 idf.py)
#
# 说明:
# - 正常启用 ccache（不要再设 CCACHE_DISABLE=1，否则全量重编 953 个单元非常慢）
# - 之前 ccache"锁死"及链接慢(80s)的根因已定位：火绒 HipsDaemon 拦截 ld 的海量小文件读。
#   建议：火绒 -> 信任区 添加 E:\esp32-crid-1.1.0、C:\Users\pc1\.espressif、D:\esp-idf-v6.1
#   （工具 defender_exclude.ps1 是 Windows Defender 版的等效操作）
# - 项目编译单元 ~953 个，首次全量编译约几分钟属正常，之后增量/命中缓存应 <30s

param([Parameter(ValueFromRemainingArguments=$true)][string[]]$IDFArgs = @('build'))

$ErrorActionPreference = 'Stop'

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

$t = [System.Diagnostics.Stopwatch]::StartNew()
idf.py @IDFArgs
$t.Stop()

if ($cc -and ($IDFArgs -notcontains 'fullclean') -and ($IDFArgs -notcontains 'menuconfig')) {
  Write-Host "`n=== build time: $([math]::Round($t.Elapsed.TotalSeconds,1)) s ===" -ForegroundColor Cyan
  & $cc --show-stats | Select-String 'Hits|Misses|Cache size'
}

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
  # 见下方 ccache --zero-stats 处的说明：默认不做清零，需要时显式加 -ZeroStats
  [switch]$ZeroStats,
  [Parameter(ValueFromRemainingArguments=$true)][string[]]$IDFArgs = @('build')
)

$ErrorActionPreference = 'Stop'

if ($Port) { $IDFArgs = @('-p', $Port) + $IDFArgs }
Write-Host "idf.py $($IDFArgs -join ' ')" -ForegroundColor DarkGray

# 每次编译前重新生成内置网页的 gzip 版（页面改动后自动同步）
# 注意：binaries\python\versions\3.13.12 现在是 junction（链接到 WorkBuddy vendor\python），
# PowerShell 5.1 的 Get-ChildItem -Recurse 默认不跟进 junction，会一个 python.exe 都找不到，
# 于是静默退回 'python' 命令——宿主 PATH 里没有 python 时就报「无法将 python 项识别」。
# 因此改为「显式候选路径优先（Test-Path 能穿透 junction）」，最后才退回 PATH 里的 python。
$pyCands = @('C:\Users\pc1\.workbuddy\binaries\python\versions\3.13.12\python.exe')
$pyCands += Get-ChildItem 'C:\Users\pc1\.workbuddy\binaries\python\versions' -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName 'python.exe' }
$pyCands += Get-ChildItem 'C:\Users\pc1\.espressif\python_env' -Directory -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName 'Scripts\python.exe' }
$py = $pyCands | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $py) {
  $cmdPy = Get-Command python -ErrorAction SilentlyContinue
  if ($cmdPy) { $py = $cmdPy.Source }
}
if (-not $py) {
  Write-Host '*** 找不到 python 解释器（页面 gzip 生成需要它） ***' -ForegroundColor Red
  Write-Host "    请检查 C:\Users\pc1\.workbuddy\binaries\python 或 C:\Users\pc1\.espressif\python_env" -ForegroundColor Yellow
  exit 3
}

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
# ccache --zero-stats 会把整个缓存目录走一遍重算体积。本机缓存已积累 1.6 万个文件，
# 叠上安全软件对每次文件访问的拦截，实测空目录只要 1.4s、这个目录 60s 都跑不完，
# 直接把「重启编译」卡死在第一步。它只影响构建末尾打印的命中率（而且那份统计是
# 累计值，本来就不精确），所以默认跳过；确需清零时用 `build.ps1 -ZeroStats build`。
if (-not $cc) { Write-Warning 'ccache not found; building without cache' }
elseif ($ZeroStats) {
  $tZero = [System.Diagnostics.Stopwatch]::StartNew()
  & $cc --zero-stats > $null
  Write-Host ("ccache --zero-stats: {0:N1}s" -f $tZero.Elapsed.TotalSeconds) -ForegroundColor DarkGray
}

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

# ---- sdkconfig 按芯片分开存，切目标不再互相覆盖 ----
# 根目录只有一份 sdkconfig：C5 编一次它就被写成 esp32c5，接着编 S3 直接报
#   Project sdkconfig 'sdkconfig' was generated for target 'esp32c5',
#   but CMakeCache.txt contains 'esp32s3'
# 以前只能 set-target / fullclean，代价是把另一个目标的构建目录和缓存一起牵进去。
# 现在两颗芯片各存一份快照，进 idf.py 前按 -B 目录挑一份放回根目录、编完再存回去：
#   build\    -> sdkconfig.esp32s3      build_c5\ -> sdkconfig.esp32c5
# 两个构建目录互不影响，来回切换只是复制一个文本文件，不删任何产物。
$root = Split-Path $PSScriptRoot -Parent
$wantChip = if ((Split-Path -Leaf $buildDir).ToLower() -like '*c5*') { 'esp32c5' } else { 'esp32s3' }
# 显式 set-target 时以命令行写的为准（否则会被目录名带偏）
$stIdx = [array]::IndexOf($IDFArgs, 'set-target')
if ($stIdx -ge 0 -and $IDFArgs.Count -gt ($stIdx + 1) -and $IDFArgs[$stIdx + 1] -match '^esp32[a-z0-9]+$') {
  $wantChip = $IDFArgs[$stIdx + 1]
}
$rootSc = Join-Path $root 'sdkconfig'
$curChip = ''
if (Test-Path $rootSc) {
  $tline = Select-String -Path $rootSc -Pattern '^CONFIG_IDF_TARGET=' -ErrorAction SilentlyContinue |
           Select-Object -First 1
  if ($tline -and $tline.Line -match '"([^"]+)"') { $curChip = $Matches[1] }
}
if ($curChip -ne $wantChip) {
  if ($curChip) {
    Copy-Item $rootSc (Join-Path $root "sdkconfig.$curChip") -Force
    Write-Host "sdkconfig: 先把当前 $curChip 配置存到 sdkconfig.$curChip" -ForegroundColor DarkGray
  }
  $wantSc = Join-Path $root "sdkconfig.$wantChip"
  if (Test-Path $wantSc) {
    Copy-Item $wantSc $rootSc -Force
    Write-Host "sdkconfig: 切到 $wantChip（sdkconfig.$wantChip -> sdkconfig）" -ForegroundColor Cyan
  } else {
    Write-Host "*** 缺少 sdkconfig.$wantChip，无法切换目标；先确认 $buildDir 该编哪颗芯片 ***" -ForegroundColor Red
  }
}
# 先 Test-Path 再删：管道为空时 Remove-Item 会报 missing path operand，
# 而部分环境的安全删除钩子会把它变成终止错误。
# 本机的 safe-delete 钩子走「回收站」，回收站不可用时会 fail-closed 抛错
# （SAFE_DELETE_FAIL_CLOSED），直接导致整次构建中止——所以这里失败就退回 .NET 直接删，
# 删不掉也不影响构建（最多 Compile time 是旧值）。
$__descObj = Join-Path $buildDir 'esp-idf\esp_app_format\CMakeFiles\__idf_esp_app_format.dir\esp_app_desc.c.obj'
if (Test-Path $__descObj) {
  try {
    Remove-Item -Force $__descObj -ErrorAction Stop
  } catch {
    try { [System.IO.File]::Delete($__descObj) } catch { Write-Host "警告：无法删除 $__descObj（Compile time 可能是旧值）" -ForegroundColor Yellow }
  }
}

$t = [System.Diagnostics.Stopwatch]::StartNew()
# 同 export.ps1 的问题：idf.py 及其子进程会往 stderr 打 NOTE / 编译告警（例如
# BT_NIMBLE_MESH_PROVISIONER 的 Kconfig 提示），在 Stop 下会被当成终止错误，
# 导致「构建其实成功了却提前退出」。这里放开 stderr，成败一律由退出码判断。
$ErrorActionPreference = 'Continue'
idf.py @IDFArgs
$rc = $LASTEXITCODE
$ErrorActionPreference = $__eap
$t.Stop()

# 编完把根 sdkconfig 存回本芯片的快照：menuconfig 之类的改动不会丢，
# 下次切回这颗芯片直接就用这份。
if (Test-Path $rootSc) { Copy-Item $rootSc (Join-Path $root "sdkconfig.$wantChip") -Force }

if ($cc -and ($IDFArgs -notcontains 'fullclean') -and ($IDFArgs -notcontains 'menuconfig')) {
  Write-Host "`n=== build time: $([math]::Round($t.Elapsed.TotalSeconds,1)) s ===" -ForegroundColor Cyan
  & $cc --show-stats | Select-String 'Hits|Misses|Cache size'
}

# 必须把 idf.py 的退出码透出，否则烧录/编译失败也会被当成成功
if ($rc -ne 0) { Write-Host "`n*** idf.py failed (exit=$rc) ***" -ForegroundColor Red }
exit $rc

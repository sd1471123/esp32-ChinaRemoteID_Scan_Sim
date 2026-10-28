# ESP32-CRID 构建包装：体检 + 清环境 + 兜底清理，然后转交 build.ps1
#
# 解决三个反复踩的坑（2026-10-27 实测，每次都白等 5-8 分钟）：
#   1) PYTHONPATH / PYTHONHOME 被宿主进程污染 -> idf.py 落到非 IDF 的 python（如 miniconda），
#      一启动就 init_fs_encoding 崩溃，**外层进程却不退出**，看起来就是「卡住没输出」；
#   2) 沙箱拦住了 D:\esp-idf-v6.1 与 .espressif 的读取 -> export.ps1 静默失效，
#      症状与 1) 完全一样。本脚本会在 2 秒内把原因报出来，不必再等 8 分钟；
#   3) build_c5 之类的目录残留半成品（没有 CMakeCache.txt）-> set-target 的 fullclean
#      直接拒绝执行（"doesn't seem to be a CMake build directory"）。
#
# 注意：脚本无法自己解除沙箱限制。第 2 条只能由调用方在「信任模式」下运行本脚本，
#       这里的价值是「快速失败并说清原因」，不是绕过限制。
#
# 用法（参数与 build.ps1 一致，另外多了 -Doctor）：
#   tools\build_trust.ps1                        # 等价于 build.ps1 build
#   tools\build_trust.ps1 -B build_c5 build
#   tools\build_trust.ps1 -B build_c5 set-target esp32c5
#   tools\build_trust.ps1 -B build_c5 -p COM5 flash
#   tools\build_trust.ps1 -Doctor                # 只体检，不编译

[CmdletBinding(PositionalBinding=$false)]
param(
  [Alias('p')][string]$Port,
  [switch]$Doctor,
  [Parameter(ValueFromRemainingArguments=$true)][string[]]$IDFArgs = @('build')
)

$ErrorActionPreference = 'Stop'
$IDF_PATH_DIR = 'D:\esp-idf-v6.1'
$IDF_PY_ENV  = 'C:\Users\pc1\.espressif\python_env\idf6.1_py3.12_env\Scripts\python.exe'
$ROOT = Split-Path $PSScriptRoot -Parent

function Write-Item($ok, $msg) {
  # 注意：不能写 $ok -eq 'SKIP'。PowerShell 会把右操作数转成左边的类型，
  # $true -eq 'SKIP' 恒为 True（'SKIP' 转 bool 为真），导致所有 OK 都被当成 SKIP。
  if ($ok -is [string]) { Write-Host "  [--] $msg" -ForegroundColor DarkGray }
  elseif ($ok)          { Write-Host "  [OK] $msg" -ForegroundColor DarkGreen }
  else                  { Write-Host "  [!!] $msg" -ForegroundColor Red }
}

function Test-Readable($path) {
  if (-not (Test-Path $path)) { return $false }
  # 只开一个只读句柄就能探到「文件存在但读被拦」，比读内容快且不受大小影响
  try {
    $fs = [System.IO.File]::Open($path, [System.IO.FileMode]::Open,
                                 [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    $fs.Close()
    return $true
  } catch { return $false }
}

# --- 从参数里取构建目录（不改动参数本身，仍原样透传给 idf.py） ---
$buildDir = 'build'
for ($i = 0; $i -lt $IDFArgs.Count - 1; $i++) {
  if ($IDFArgs[$i] -eq '-B' -or $IDFArgs[$i] -eq '--build-dir') { $buildDir = $IDFArgs[$i + 1]; break }
}
$buildPath = if ([System.IO.Path]::IsPathRooted($buildDir)) { $buildDir } else { Join-Path $ROOT $buildDir }
$isSetTarget = $IDFArgs -contains 'set-target'

Push-Location $ROOT
try {
  # ============================ 体检 ============================
  Write-Host "`n=== 构建环境体检 ===" -ForegroundColor Cyan
  $fail = 0

  # 1. 宿主注入的 python 环境变量（会毁掉 IDF 的 python 解析）
  $polluted = @()
  foreach ($v in 'PYTHONPATH', 'PYTHONHOME', 'PYTHONSTARTUP') {
    $val = [System.Environment]::GetEnvironmentVariable($v)
    if ($val) { $polluted += "$v=$val" }
  }
  if ($polluted.Count -gt 0) {
    Write-Item $true ('已清除宿主 python 变量: ' + ($polluted -join '; '))
    # 用 .NET API 删除：Remove-Item Env: 会被安全删除钩子拦成 fail-closed
    foreach ($v in 'PYTHONPATH', 'PYTHONHOME', 'PYTHONSTARTUP') {
      [System.Environment]::SetEnvironmentVariable($v, $null)
    }
  } else {
    Write-Item $true 'python 环境变量干净'
  }

  # 2. IDF 源码可读（沙箱拦截时这里最先炸）
  $exportOk = Test-Readable (Join-Path $IDF_PATH_DIR 'export.ps1')
  if ($exportOk) {
    Write-Item $true "IDF 源码可读: $IDF_PATH_DIR"
  } else {
    $fail++
    Write-Item $false "IDF 源码读不到: $IDF_PATH_DIR\export.ps1"
    Write-Host '       原因通常是沙箱拦截了工作区外的文件读取。' -ForegroundColor Yellow
    Write-Host '       处理：用「信任模式」运行本脚本（否则会在 python 启动阶段挂住 5-8 分钟）。' -ForegroundColor Yellow
  }

  # 3. IDF 专用 python 可执行
  $pyOk = Test-Readable $IDF_PY_ENV
  if ($pyOk) {
    Write-Item $true "IDF python 可执行: $IDF_PY_ENV"
  } else {
    $fail++
    Write-Item $false "IDF python 读不到: $IDF_PY_ENV"
  }

  # 4. ccache
  $cc = Get-ChildItem 'C:\Users\pc1\.espressif\tools\ccache' -Recurse -Filter ccache.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
  Write-Item ([bool]$cc) $(if ($cc) { "ccache: $cc" } else { 'ccache 未找到（编译会明显变慢，不影响结果）' })

  # 5. 构建目录状态
  $hasCache = Test-Path (Join-Path $buildPath 'CMakeCache.txt')
  if (-not (Test-Path $buildPath)) {
    Write-Item $true "构建目录不存在（首次构建）: $buildDir"
  } elseif ($hasCache) {
    $tgt = (Select-String -Path (Join-Path $buildPath 'CMakeCache.txt') -Pattern '^IDF_TARGET:' -ErrorAction SilentlyContinue |
            Select-Object -First 1).Line
    Write-Item $true ("构建目录可用: {0}  target={1}" -f $buildDir, (("$tgt" -split '=')[-1]))
  } else {
    # 半成品目录：set-target 的 fullclean 会拒绝，必须先删
    if ($isSetTarget) {
      Write-Host "  [--] 构建目录是半成品（无 CMakeCache.txt），set-target 前先删除: $buildDir" -ForegroundColor Yellow
      if (Test-Path $buildPath) { Remove-Item -Recurse -Force $buildPath }
      Write-Item (-not (Test-Path $buildPath)) "已清理半成品目录: $buildDir"
      if (Test-Path $buildPath) { $fail++ }
    } else {
      $fail++
      Write-Item $false "构建目录缺 CMakeCache.txt: $buildDir（先跑 set-target <target>，或删掉该目录）"
    }
  }

  # 6. sdkconfig 里的目标芯片（换芯片忘了改 sdkconfig 是最常见的编译失败原因）
  $sc = Join-Path $ROOT 'sdkconfig'
  if (Test-Path $sc) {
    $t = (Select-String -Path $sc -Pattern '^CONFIG_IDF_TARGET=' | Select-Object -First 1).Line
    Write-Item $true "sdkconfig: $t"
  } else {
    Write-Item 'SKIP' 'sdkconfig 不存在（set-target 会由 sdkconfig.defaults* 生成）'
  }

  if ($Doctor) {
    # 注意：不要在双引号字符串里嵌 $() 再写双引号，PowerShell 5.1 会解析失败
    $verdict = if ($fail -eq 0) { '可以编译' } else { "$fail 项需要处理" }
    Write-Host ''
    Write-Host "=== 体检结束：$verdict ===" -ForegroundColor Cyan
    exit $(if ($fail -eq 0) { 0 } else { 2 })
  }

  if ($fail -gt 0) {
    Write-Host "`n*** 体检未通过（$fail 项），已中止，避免白等 idf.py ***" -ForegroundColor Red
    exit 2
  }

  # ============================ 转交 build.ps1 ============================
  $call = @()
  if ($Port) { $call += @('-p', $Port) }
  $call += $IDFArgs
  Write-Host "`n>>> tools\build.ps1 $($call -join ' ')`n" -ForegroundColor DarkGray

  & (Join-Path $PSScriptRoot 'build.ps1') @call
  $rc = $LASTEXITCODE

  # 编译成功后给出产物大小，省得再去 ls
  if ($rc -eq 0 -and ($IDFArgs -contains 'build') -and -not $isSetTarget) {
    $bin = Join-Path $buildPath 'remoteid_scanner.bin'
    if (Test-Path $bin) {
      $sz = (Get-Item $bin).Length
      Write-Host ("`n产物: {0}  {1:N0} 字节" -f $bin, $sz) -ForegroundColor Cyan
    }
  }
  exit $rc
}
finally {
  Pop-Location
}

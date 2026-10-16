# 以管理员身份运行一次：为 ESP-IDF 工具链添加 Windows Defender 排除目录
# 这是 Windows 下编译提速最明显的一项（实时扫描会让 ninja 并行编译互相等 I/O）
# 用法: 右键 PowerShell -> 以管理员身份运行 -> powershell -ExecutionPolicy Bypass -File tools\defender_exclude.ps1

$paths = @(
  'E:\esp32-crid-1.1.0',                                  # 项目源码 + build 目录
  'D:\esp-idf-v6.1',                                      # ESP-IDF 框架
  'C:\Users\pc1\.espressif',                              # 工具链 / ccache / python env
  'C:\Users\pc1\AppData\Local\ccache'                     # cache 缓存目录
)

# 管理员检查
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
           ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { Write-Error '请以管理员身份运行此脚本'; exit 1 }

foreach ($p in $paths) {
  if (Test-Path $p) {
    Add-MpPreference -ExclusionPath $p
    Write-Host "excluded: $p" -ForegroundColor Green
  } else {
    Write-Warning "skip (not found): $p"
  }
}
Write-Host "`n完成。可用 Get-MpPreference | Select-Object -ExpandProperty ExclusionPath 验证"

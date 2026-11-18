<#
WowType 的 Windows 安装 / 卸载脚本（第一阶段，没有图形安装程序）。

  安装：右键「以管理员身份运行 PowerShell」，在解开的 WowType-<版本>-windows-x64 目录里：
        powershell -ExecutionPolicy Bypass -File .\install.ps1
  卸载：powershell -ExecutionPolicy Bypass -File .\install.ps1 -Uninstall

装到 %ProgramFiles%\WowType（商店应用、开始菜单的搜索框只能载入这种位置的输入法），用 regsvr32 注册，
注册时就默认启用；装好以后用 Win + 空格切到 WowType。正在用着输入法的应用会锁着旧的 wowtype.dll：
旧的改名留着（下次安装时删），新的照样装上，这些应用重开以后用新的。用户数据（%LOCALAPPDATA%\WowType）不动。
#>
param(
    [switch]$Uninstall,
    # 装到哪（测试用）；默认 %ProgramFiles%\WowType
    [string]$Target = (Join-Path $env:ProgramFiles 'WowType')
)

$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw '要用管理员身份运行（注册输入法要写 HKEY_LOCAL_MACHINE）'
}

function Invoke-Regsvr32([string[]]$arguments) {
    $p = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList $arguments -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "regsvr32 $($arguments -join ' ') 失败：$($p.ExitCode)" }
}

$dll = Join-Path $Target 'wowtype.dll'
$startMenuLink = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\WowType 设置.lnk'

if ($Uninstall) {
    if (Test-Path $dll) {
        Invoke-Regsvr32 @('/u', '/s', "`"$dll`"")
    }
    Remove-Item -Force $startMenuLink -ErrorAction SilentlyContinue
    # 正在用着的 DLL 删不掉：留给重启以后
    Get-ChildItem -Path $Target -ErrorAction SilentlyContinue | ForEach-Object {
        try { Remove-Item -Recurse -Force $_.FullName } catch { Write-Warning "$($_.Exception.Message)（重启以后再删）" }
    }
    try { Remove-Item -Force $Target } catch {}
    Write-Host '卸载好了。用户数据留在' (Join-Path $env:LOCALAPPDATA 'WowType')
    return
}

foreach ($f in @('wowtype.dll', 'data\lexicon.tsv', 'data\english.txt')) {
    if (-not (Test-Path (Join-Path $source $f))) { throw "安装包里没有 $f" }
}

New-Item -ItemType Directory -Force -Path (Join-Path $Target 'data') | Out-Null
# 上次换下来的旧 DLL：现在没人用了就删掉
Get-ChildItem -Path $Target -Filter 'wowtype.dll.old-*' -ErrorAction SilentlyContinue | ForEach-Object {
    try { Remove-Item -Force $_.FullName } catch {}
}
if (Test-Path $dll) {
    # 被应用载入着的 DLL 不能覆盖，但能改名
    Rename-Item -Path $dll -NewName ("wowtype.dll.old-" + (Get-Date -Format 'yyyyMMddHHmmss'))
}
Copy-Item -Force (Join-Path $source 'wowtype.dll') $dll
Copy-Item -Force -Recurse (Join-Path $source 'data\*') (Join-Path $Target 'data')
foreach ($f in @('wowtype-settings.exe', 'install.ps1', 'LICENSE', 'README.txt')) {
    if (Test-Path (Join-Path $source $f)) { Copy-Item -Force (Join-Path $source $f) $Target }
}

Invoke-Regsvr32 @('/s', "`"$dll`"")
# 开始菜单里放一个「WowType 设置」
$settings = Join-Path $Target 'wowtype-settings.exe'
if (Test-Path $settings) {
    # 放不上快捷方式（精简过的系统没有开始菜单目录）不算装失败
    try {
        $folder = Split-Path $startMenuLink
        New-Item -ItemType Directory -Force -Path $folder | Out-Null
        # WScript.Shell 存不了中文名字的快捷方式：先用英文名存，再改名
        $ascii = Join-Path $folder 'WowType-settings.lnk'
        $shell = New-Object -ComObject WScript.Shell
        $link = $shell.CreateShortcut($ascii)
        $link.TargetPath = $settings
        $link.Save()
        Move-Item -Force -LiteralPath $ascii -Destination $startMenuLink
    } catch {
        Write-Warning "开始菜单的快捷方式没放上：$($_.Exception.Message)。设置窗口在 $settings"
    }
}
Write-Host "装好了：$Target"
Write-Host '在「设置 → 时间和语言 → 语言和区域」的中文（简体）键盘里能看到 WowType，用 Win + 空格切过去。'
Write-Host '设置：开始菜单里的「WowType 设置」，或者任务栏上「中」按钮右键。卸载：.\install.ps1 -Uninstall'

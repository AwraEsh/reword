$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') {
    throw 'This acceptance script requires an isolated GitHub Actions Windows runner.'
}
$Root = Split-Path -Parent $PSScriptRoot
$Temporary = Join-Path ([IO.Path]::GetTempPath()) ('Reword acceptance ' + [guid]::NewGuid())
$Installed = Join-Path $Temporary 'Installed Reword'
$Saved = @{}
foreach ($Name in @('PATH', 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_QPA_PLATFORM', 'QT_QUICK_BACKEND', 'REWORD_DATA_DIR')) {
    $Saved[$Name] = [Environment]::GetEnvironmentVariable($Name, 'Process')
}
New-Item -ItemType Directory -Force $Temporary | Out-Null
function Invoke-Checked($Executable, $Arguments) {
    $Process = Start-Process -FilePath $Executable -ArgumentList $Arguments -PassThru
    if (-not $Process.WaitForExit(30000)) {
        $Process.Kill()
        throw 'The package acceptance process did not finish within 30 seconds.'
    }
    $Process.Refresh()
    if ($Process.ExitCode -ne 0) { throw "Package acceptance process exited with code $($Process.ExitCode)." }
}
try {
    # /D must be the final NSIS argument; its remainder can contain spaces.
    Invoke-Checked "$Root/dist/Reword-Setup-0.1.0.exe" "/S /D=$Installed"
    if (-not (Test-Path "$Installed/reword.exe")) { throw 'Installer did not create reword.exe.' }
    if (-not (Test-Path "$Installed/Uninstall.exe")) { throw 'Uninstaller is missing.' }
    # Remove access to Qt/vcpkg/developer runtimes before launching the installed app.
    $env:PATH = "$env:WINDIR/System32;$env:WINDIR"
    foreach ($Name in @('QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH')) {
        [Environment]::SetEnvironmentVariable($Name, $null, 'Process')
    }
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_QUICK_BACKEND = 'software'
    $env:REWORD_DATA_DIR = Join-Path $Temporary 'Synthetic Data'
    Invoke-Checked "$Installed/reword.exe" '--smoke-test'
    Set-Content -Path "$Installed/user-added.txt" -Value 'Synthetic file retained by uninstall.'
    Invoke-Checked "$Installed/Uninstall.exe" '/S'
    $Deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ((Test-Path "$Installed/reword.exe") -and [DateTime]::UtcNow -lt $Deadline) {
        Start-Sleep -Milliseconds 100
    }
    if (Test-Path "$Installed/reword.exe") { throw 'Uninstall did not remove the shipped application.' }
    if (-not (Test-Path "$Installed/user-added.txt")) { throw 'Uninstall deleted a user-added file.' }
    Write-Host 'PASS: per-user install, bundled-runtime launch on clean PATH, and uninstall preserving user-added files.'
}
finally {
    foreach ($Name in $Saved.Keys) {
        [Environment]::SetEnvironmentVariable($Name, $Saved[$Name], 'Process')
    }
    # This is a generated acceptance directory on a disposable CI runner.
    if (Test-Path $Temporary) { Remove-Item -LiteralPath $Temporary -Recurse -Force }
}

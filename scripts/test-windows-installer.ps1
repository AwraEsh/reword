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
    $Stage = Join-Path $Root 'dist/windows'
    $Shipped = @(Get-ChildItem $Stage -File -Recurse)
    foreach ($File in $Shipped) {
        $Relative = $File.FullName.Substring($Stage.Length + 1)
        $Destination = Join-Path $Installed $Relative
        if (-not (Test-Path -LiteralPath $Destination)) { throw "Installer omitted a shipped file: $Relative" }
        if ((Get-FileHash -LiteralPath $Destination).Hash -ne (Get-FileHash -LiteralPath $File.FullName).Hash) {
            throw "Installed file does not match its staged source: $Relative"
        }
    }
    $UninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Reword'
    if (-not (Test-Path $UninstallKey)) { throw 'Per-user uninstall registration is missing.' }
    # Remove access to Qt/vcpkg/developer runtimes before launching the installed app.
    $env:PATH = "$env:WINDIR/System32;$env:WINDIR"
    foreach ($Name in @('QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH')) {
        [Environment]::SetEnvironmentVariable($Name, $null, 'Process')
    }
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_QUICK_BACKEND = 'software'
    $env:REWORD_DATA_DIR = Join-Path $Temporary 'Synthetic Data'
    Invoke-Checked "$Installed/reword.exe" '--smoke-test'
    $AddedFile = Join-Path $Installed 'user-added.txt'
    Set-Content -LiteralPath $AddedFile -Value 'Synthetic file retained by uninstall.'
    Invoke-Checked "$Installed/Uninstall.exe" '/S'
    $Deadline = [DateTime]::UtcNow.AddSeconds(15)
    # NSIS launches a temporary uninstaller process. Waiting on its original
    # launcher or on the first deleted file does not mean uninstall has finished.
    # Its registry key is deleted at the end of our uninstall section.
    while ((Test-Path $UninstallKey) -and [DateTime]::UtcNow -lt $Deadline) {
        Start-Sleep -Milliseconds 100
    }
    if (Test-Path $UninstallKey) { throw 'Uninstall did not finish its registration cleanup.' }
    $Remaining = @(Get-ChildItem $Installed -File -Recurse | Where-Object { $_.FullName -ne $AddedFile })
    if ($Remaining.Count) { throw 'Uninstall left one or more shipped files behind.' }
    if (-not (Test-Path "$Installed/user-added.txt")) { throw 'Uninstall deleted a user-added file.' }
    Write-Host 'PASS: per-user install, bundled-runtime launch on clean PATH, and uninstall preserving user-added files.'
}
finally {
    foreach ($Name in $Saved.Keys) {
        [Environment]::SetEnvironmentVariable($Name, $Saved[$Name], 'Process')
    }
    # This is a generated acceptance directory on a disposable CI runner.
    try {
        if (Test-Path $Temporary) { Remove-Item -LiteralPath $Temporary -Recurse -Force }
    }
    catch {
        Write-Warning 'The disposable acceptance directory could not be removed. Original test results are preserved.'
    }
}

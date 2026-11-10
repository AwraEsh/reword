$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') {
    throw 'This acceptance script requires an isolated GitHub Actions Windows runner.'
}
$Root = Split-Path -Parent $PSScriptRoot
$Temporary = Join-Path ([IO.Path]::GetTempPath()) ('Reword acceptance ' + [guid]::NewGuid())
$Installed = Join-Path $Temporary 'Installed Reword'
$Installer = Join-Path $Root 'dist/Reword-Setup-0.1.0.exe'
$ProbeRoot = Join-Path $Temporary 'Chrome Probe'
$ProbeReport = Join-Path $Root 'dist/windows-installed-chrome'
$BrowserKeys = @{
    chrome = 'HKCU:\Software\Google\Chrome\NativeMessagingHosts\org.reword.browser'
    chromium = 'HKCU:\Software\Chromium\NativeMessagingHosts\org.reword.browser'
}
$Chrome = @("$env:ProgramFiles/Google/Chrome/Application/chrome.exe", "${env:ProgramFiles(x86)}/Google/Chrome/Application/chrome.exe") |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (!$Chrome) { throw 'Chrome is missing from the Windows test runner.' }
$Node = (Get-Command node.exe).Source
$Saved = @{}
foreach ($Name in @('PATH', 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_QPA_PLATFORM',
                   'QT_QUICK_BACKEND', 'REWORD_DATA_DIR', 'REWORD_PROBE_EXTENSION', 'REWORD_PROBE_BROWSER_HOST',
                   'REWORD_PROBE_WINDOWS_FIXTURE_PATH', 'REWORD_PROBE_PWSH')) {
    $Saved[$Name] = [Environment]::GetEnvironmentVariable($Name, 'Process')
}
New-Item -ItemType Directory -Force $Temporary | Out-Null
function Get-BrowserRegistration($Key) {
    if (Test-Path -LiteralPath $Key) { (Get-Item -LiteralPath $Key).GetValue('') }
}
function Assert-BrowserRegistrations {
    foreach ($Browser in $BrowserKeys.Keys) {
        $Expected = Join-Path $env:REWORD_DATA_DIR "browser-hosts/$Browser.json"
        if ((Get-BrowserRegistration $BrowserKeys[$Browser]) -ne $Expected) {
            throw "The installer did not preserve the expected $Browser registration."
        }
        $Manifest = Get-Content -LiteralPath $Expected -Raw | ConvertFrom-Json
        if ($Manifest.path -ne (Join-Path $Installed 'reword-browser-host.exe')) {
            throw "The $Browser registration does not point to the installed host."
        }
    }
}
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
    Invoke-Checked $Installer "/S /D=$Installed"
    if (-not (Test-Path "$Installed/reword.exe")) { throw 'Installer did not create reword.exe.' }
    if (-not (Test-Path "$Installed/reword-browser-host.exe" -PathType Leaf) -or
        -not (Test-Path "$Installed/browser/chromium/manifest.json" -PathType Leaf)) {
        throw 'The installed browser host or extension is missing.'
    }
    foreach ($Key in $BrowserKeys.Values) {
        if (Get-BrowserRegistration $Key) { throw 'The default install registered a browser.' }
    }
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
    $Database = Join-Path $env:REWORD_DATA_DIR 'reword.sqlite'
    if (-not (Test-Path -LiteralPath $Database -PathType Leaf)) {
        throw 'The installed application did not initialize its local database.'
    }
    $Stream = [IO.File]::OpenRead($Database)
    try {
        $Header = New-Object byte[] 16
        if ($Stream.Read($Header, 0, 16) -ne 16 -or
            [Text.Encoding]::ASCII.GetString($Header) -ne "SQLite format 3`0") {
            throw 'The installed application did not create a valid SQLite database.'
        }
    }
    finally { $Stream.Dispose() }
    $DataHash = (Get-FileHash -LiteralPath $Database).Hash
    $AddedFile = Join-Path $Installed 'user-added.txt'
    Set-Content -LiteralPath $AddedFile -Value 'Synthetic file retained by uninstall.'

    New-Item -ItemType Directory -Force $ProbeRoot | Out-Null
    $env:REWORD_DATA_DIR = Join-Path $ProbeRoot 'reword'
    Invoke-Checked $Installer "/S /ConnectChrome /ConnectChromium /D=$Installed"
    Assert-BrowserRegistrations
    Invoke-Checked $Installer "/S /D=$Installed"
    Assert-BrowserRegistrations
    if ((Get-FileHash -LiteralPath $Database).Hash -ne $DataHash -or !(Test-Path -LiteralPath $AddedFile)) {
        throw 'Reinstall changed the synthetic settings or removed a user-added file.'
    }
    $env:REWORD_PROBE_EXTENSION = '1'
    $env:REWORD_PROBE_BROWSER_HOST = Join-Path $Installed 'reword-browser-host.exe'
    $env:REWORD_PROBE_WINDOWS_FIXTURE_PATH = $Saved['PATH']
    $env:REWORD_PROBE_PWSH = Join-Path $PSHOME 'pwsh.exe'
    & $Node "$Root/tests/browser_transport/probe.mjs" $ProbeRoot $ProbeReport $Chrome "$Root/build/Release/reword-browser-host.exe" gui
    if ($LASTEXITCODE -ne 0) {
        if (Test-Path "$ProbeReport/report.json") { Get-Content "$ProbeReport/report.json" }
        throw 'The installed Windows Chrome probe failed.'
    }

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
    foreach ($Browser in $BrowserKeys.Keys) {
        if ((Get-BrowserRegistration $BrowserKeys[$Browser]) -or
            (Test-Path (Join-Path $env:REWORD_DATA_DIR "browser-hosts/$Browser.json"))) {
            throw "Uninstall left its $Browser native messaging registration behind."
        }
    }
    if ((Get-FileHash -LiteralPath $Database).Hash -ne $DataHash -or
        !(Test-Path (Join-Path $env:REWORD_DATA_DIR 'reword.sqlite') -PathType Leaf)) {
        throw 'Uninstall changed or removed a Reword data store.'
    }
    $ReportPath = Join-Path $ProbeReport 'report.json'
    $Report = Get-Content -LiteralPath $ReportPath -Raw | ConvertFrom-Json -AsHashtable
    $Report['installerPreservedRegistrationsOnReinstall'] = $true
    $Report['uninstallerRemovedRegistrationsAndPreservedData'] = $true
    $Report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $ReportPath -Encoding utf8
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

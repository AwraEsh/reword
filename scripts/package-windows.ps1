$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Stage = Join-Path $Root 'dist/windows'
New-Item -ItemType Directory -Force $Stage | Out-Null
Copy-Item "$Root/build/Release/reword.exe" $Stage
windeployqt --release --no-translations --qmldir "$Root/qml" "$Stage/reword.exe"
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
Copy-Item "$env:VCPKG_INSTALLATION_ROOT/installed/x64-windows/bin/*hunspell*.dll" $Stage
New-Item -ItemType Directory -Force "$Stage/dictionaries" | Out-Null
Copy-Item "$Root/assets/dictionaries/*" "$Stage/dictionaries"
New-Item -ItemType Directory -Force "$Stage/licenses" | Out-Null
Copy-Item "$Root/THIRD_PARTY.md","$Root/LICENSE" "$Stage/licenses"
# Ship the VC runtime beside the executable; no machine-wide runtime installer.
$Crt = Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/*/VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT' -Directory | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $Crt) { throw 'VC runtime directory not found' }
Copy-Item "$($Crt.FullName)/*.dll" $Stage
# Only uninstall files shipped by us; preserve anything the user adds later.
$Uninstall = @()
Get-ChildItem $Stage -File -Recurse | ForEach-Object {
  $Relative = $_.FullName.Substring($Stage.Length + 1)
  $Uninstall += 'Delete "$INSTDIR\' + $Relative + '"'
}
Get-ChildItem $Stage -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
  $Relative = $_.FullName.Substring($Stage.Length + 1)
  $Uninstall += 'RMDir "$INSTDIR\' + $Relative + '"'
}
$Uninstall | Set-Content -Encoding utf8 "$Root/dist/uninstall-files.nsh"
$Nsis = 'C:/Program Files (x86)/NSIS/makensis.exe' 
if (-not (Test-Path $Nsis)) { choco install nsis -y --no-progress }
& $Nsis "/DROOT=$Root" "$Root/packaging/windows.nsi"
if ($LASTEXITCODE -ne 0) { throw 'Installer creation failed' }

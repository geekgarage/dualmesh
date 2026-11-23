# SPDX-License-Identifier: MIT
# DualMesh - gør en ny Windows-PC klar til udvikling (samme opsætning som den gamle PC).
#
# Kør i PowerShell (ikke som administrator; winget beder selv om lov, hvor det kræves):
#   powershell -ExecutionPolicy Bypass -File setup_pc.ps1            # repo i Dokumenter\Claude, værktøjer i C:\Claude\tools
#   powershell -ExecutionPolicy Bypass -File setup_pc.ps1 -Root D:\Claude -Tools D:\Claude\tools
#
# Installerer: Git, Python 3.11, CMake, Ninja, WinLibs GCC (seneste, 16.2 i nov 2026; med clang/gdb), Node.js LTS
# (fejlrapport-sidens test), GitHub CLI, PlatformIO 6.2.0 i <Tools>\platformio, Python-venvs til
# MQTT-test, Meshtastic/MeshCore-CLI og fontgeneratorerne, og MQTT-hjælpescripts i
# <Tools>\mqtt. Henter repoet til <Root>\dualmesh, hvis det ikke findes, bygger firmwaren
# (henter ESP32-værktøjskæden) og kører hosttestene. Kan køres igen uden skade.
param(
  [string]$Root = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Claude'),
  # Værktøjerne ligger for sig: kort sti og uden for OneDrive (22. nov 2026). Med Arduino-ESP32
  # 2.0.14 får GCC flere hundrede -I-stier med PlatformIO-mappen foran; en lang sti sprænger
  # Windows' grænse på 32 767 tegn ("CreateProcess: No such file or directory").
  [string]$Tools = 'C:\Claude\tools',
  [string]$Repo = 'https://github.com/geekgarage/dualmesh.git'
)
$ErrorActionPreference = 'Stop'
# PlatformIO kan ikke lide mellemrum eller æøå i stien.
if ($Tools -match '[^\x21-\x7E]' -or $Tools.Length -gt 30) {
  Write-Warning "Værktøjsstien '$Tools' er for lang (> 30 tegn) eller har mellemrum/specialtegn. Brug -Tools med en anden sti."
  exit 1
}
if ($Root -match '[^\x21-\x7E]') {
  Write-Warning "Stien '$Root' indeholder mellemrum eller specialtegn, som PlatformIO ikke kan lide. Brug -Root med en anden sti."
  exit 1
}
# Ligger værktøjerne i OneDrive, synkroniseres flere GB og tusindvis af filer.
if ($env:OneDrive -and $Tools.StartsWith($env:OneDrive, [StringComparison]::OrdinalIgnoreCase)) {
  Write-Warning "Værktøjerne ville ligge i OneDrive ($Tools): ESP32-værktøjerne (flere GB) bliver synkroniseret."
  if ((Read-Host 'Fortsæt alligevel? (j/n)') -ne 'j') { exit 1 }
}
Write-Host "Projekt i $Root, værktøjer i $Tools"
$Proj = Join-Path $Root 'dualmesh'
New-Item -ItemType Directory -Force $Root, $Tools | Out-Null
New-Item -ItemType Directory -Force $Tools | Out-Null

function Step($t) { Write-Host "`n=== $t" -ForegroundColor Cyan }
function RefreshPath {
  $m = [Environment]::GetEnvironmentVariable('Path', 'Machine')
  $u = [Environment]::GetEnvironmentVariable('Path', 'User')
  $env:Path = "$m;$u"
}
function WingetInstall($id, $extra = @()) {
  $have = winget list --id $id -e --accept-source-agreements 2>$null | Select-String $id
  if ($have) { Write-Host "$id findes allerede"; return }
  winget install --id $id -e --silent --accept-source-agreements --accept-package-agreements @extra
}

Step 'Programmer (winget)'
WingetInstall 'Git.Git'
WingetInstall 'GitHub.cli'   # gh (log ind bagefter med: gh auth login)
WingetInstall 'Python.Python.3.11'
WingetInstall 'Kitware.CMake'
WingetInstall 'Ninja-build.Ninja'
WingetInstall 'OpenJS.NodeJS.LTS'
WingetInstall 'BrechtSanders.WinLibs.POSIX.UCRT.LLVM' @('--location', (Join-Path $Tools 'winlibs'))
RefreshPath
# GCC fra WinLibs på brugerens PATH (hosttest bruger g++ og Ninja).
$gccBin = Join-Path $Tools 'winlibs\mingw64\bin'
if (Test-Path $gccBin) {
  $u = [Environment]::GetEnvironmentVariable('Path', 'User')
  if ($u -notlike "*$gccBin*") { [Environment]::SetEnvironmentVariable('Path', "$u;$gccBin", 'User') }
  RefreshPath
}
$py = 'python'
if (Get-Command py -ErrorAction SilentlyContinue) { $py = 'py' }
$pyArgs = if ($py -eq 'py') { @('-3.11') } else { @() }

Step 'Repoet'
if (-not (Test-Path (Join-Path $Proj '.git'))) {
  git clone $Repo $Proj
} else {
  git -C $Proj fetch --all
  Write-Host "Repoet findes allerede i $Proj (fetch udført)"
}

Step 'PlatformIO 6.2.0'
$pioCore = Join-Path $Tools 'platformio'
[Environment]::SetEnvironmentVariable('PLATFORMIO_CORE_DIR', $pioCore, 'User')
$env:PLATFORMIO_CORE_DIR = $pioCore
$penv = Join-Path $pioCore 'penv'
if (-not (Test-Path (Join-Path $penv 'Scripts\python.exe'))) { & $py @pyArgs -m venv $penv }
& (Join-Path $penv 'Scripts\python.exe') -m pip install -q --upgrade pip
& (Join-Path $penv 'Scripts\python.exe') -m pip install -q 'platformio==6.2.0' pyserial
$pio = Join-Path $penv 'Scripts\pio.exe'

Step 'Python-venvs'
$venvs = [ordered]@{
  'mqtt-venv' = @('amqtt==0.12.2', 'paho-mqtt==2.2.0')
  'mtvenv'    = @('meshtastic==2.7.11', 'meshcore-cli==1.6.10', 'esptool==5.6.0', 'bleak==3.1.0', 'pyserial')
  'fontvenv'  = @('fonttools==4.68.0', 'freetype-py==2.5.1', 'pillow==13.0.0')
}
foreach ($v in $venvs.Keys) {
  $dir = Join-Path $Tools $v
  if (-not (Test-Path (Join-Path $dir 'Scripts\python.exe'))) { & $py @pyArgs -m venv $dir }
  & (Join-Path $dir 'Scripts\python.exe') -m pip install -q --upgrade pip
  & (Join-Path $dir 'Scripts\python.exe') -m pip install -q @($venvs[$v])
  Write-Host "$v klar"
}

Step 'MQTT-hjælpescripts'
$mq = Join-Path $Tools 'mqtt'
New-Item -ItemType Directory -Force $mq | Out-Null
Copy-Item (Join-Path $Proj 'tools\dev\mqtt\*.py') $mq -Force
Write-Host "Kopieret til $mq"

Step 'Byg firmwaren (første gang hentes ESP32-værktøjskæden, flere minutter)'
Push-Location $Proj
RefreshPath   # python skal findes under bygget (tools/git_rev.py)
& $pio run -e tdeck_plus
if ($LASTEXITCODE -ne 0) { Write-Warning 'Firmware-bygget fejlede' }

Step 'Hosttest (simulator og test)'
cmake -S host -B build -G Ninja -DCMAKE_CXX_COMPILER=g++
cmake --build build
cmd /c 'build\sim.exe > build\sim_ny.txt'   # cmd: PowerShells > skriver UTF-16/BOM
git diff --no-index --stat host/sim_resultat.txt build/sim_ny.txt
if ($LASTEXITCODE -eq 0) { Write-Host 'Simulering uændret' } else { Write-Warning 'Simuleringen afviger fra host/sim_resultat.txt' }
.\build\test_phy.exe
.\build\test_presets.exe
.\build\test_report.exe
Pop-Location

Step 'Færdig'
Write-Host "Projekt: $Proj"
Write-Host "PlatformIO: $pio  (PLATFORMIO_CORE_DIR=$pioCore)"
Write-Host 'Åbn en NY terminal (så PATH og PLATFORMIO_CORE_DIR er opdateret), og start Claude Code i projektmappen.'

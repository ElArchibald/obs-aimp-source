[CmdletBinding()]
param(
    [string]$ObsDir = 'E:\Workspace\OBS-Studio-Bin',
    [string]$PluginName = 'obs-aimp-source'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
Set-Location $ProjectRoot

$targetDir = Join-Path $ObsDir "plugins\$PluginName"
Write-Host "Installing $PluginName to $targetDir..." -ForegroundColor Cyan

if (-not (Test-Path $targetDir)) {
    New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
}
$targetDataDir = Join-Path $targetDir "data"
if (-not (Test-Path $targetDataDir)) {
    New-Item -ItemType Directory -Path $targetDataDir -Force | Out-Null
}

# 1. Locate DLL and PDB
$dllCandidates = @(
    (Join-Path $ProjectRoot "build_vs\rundir\RelWithDebInfo\$PluginName\$PluginName.dll"),
    (Join-Path $ProjectRoot "build_vs\RelWithDebInfo\$PluginName.dll"),
    (Join-Path $ProjectRoot "build_vs\Release\$PluginName.dll"),
    (Join-Path $ProjectRoot "build_vs\Debug\$PluginName.dll"),
    (Join-Path $ProjectRoot "build\$PluginName.dll"),
    (Join-Path $ProjectRoot "build\rundir\RelWithDebInfo\$PluginName\$PluginName.dll"),
    (Join-Path $ProjectRoot "build\RelWithDebInfo\$PluginName.dll"),
    (Join-Path $ProjectRoot "build\Release\$PluginName.dll")
) | Where-Object { Test-Path $_ } | Sort-Object { (Get-Item $_).LastWriteTime } -Descending

$dllPath = $dllCandidates | Select-Object -First 1
if (-not $dllPath) {
    Write-Error "Could not find $PluginName.dll in build output! Please build the project first."
    exit 1
}

$pdbPath = [System.IO.Path]::ChangeExtension($dllPath, ".pdb")
Write-Host "Found plugin binary: $dllPath" -ForegroundColor Gray

# 2. Locate data resources (prefer source data/ directory as canonical source)
$dataCandidates = @(
    (Join-Path $ProjectRoot "data"),
    (Join-Path $ProjectRoot "build\rundir\RelWithDebInfo\$PluginName"),
    (Join-Path $ProjectRoot "build\data")
)
$dataPath = $dataCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

# 3. Install DLL (with hot-replacement support if locked by OBS)
$destDll = Join-Path $targetDir "$PluginName.dll"
$destPdb = Join-Path $targetDir "$PluginName.pdb"

try {
    Copy-Item -Path $dllPath -Destination $destDll -Force -ErrorAction Stop
    if (Test-Path $pdbPath) {
        Copy-Item -Path $pdbPath -Destination $destPdb -Force -ErrorAction SilentlyContinue
    }
}
catch {
    Write-Host "Target DLL is currently locked by OBS, hot-replacing..." -ForegroundColor Yellow
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $oldDll = Join-Path $targetDir "$PluginName.old.$timestamp.dll"
    $oldPdb = Join-Path $targetDir "$PluginName.old.$timestamp.pdb"

    if (Test-Path $destDll) {
        Move-Item -Path $destDll -Destination $oldDll -Force
    }
    if (Test-Path $destPdb) {
        Move-Item -Path $destPdb -Destination $oldPdb -Force -ErrorAction SilentlyContinue
    }

    Copy-Item -Path $dllPath -Destination $destDll -Force
    if (Test-Path $pdbPath) {
        Copy-Item -Path $pdbPath -Destination $destPdb -Force -ErrorAction SilentlyContinue
    }
}

# 4. Clean up older unlocked .old files
Get-ChildItem -Path $targetDir -Filter "*old*" -ErrorAction SilentlyContinue | ForEach-Object {
    try {
        Remove-Item -Path $_.FullName -Force -ErrorAction Stop
    }
    catch {
        # Still locked by a running OBS process, ignore
    }
}

# 5. Copy data resources (themes, locale, icons)
if ($dataPath -and (Test-Path $dataPath)) {
    Write-Host "Copying data resources from $dataPath..." -ForegroundColor Gray
    Copy-Item -Path "$dataPath\*" -Destination $targetDataDir -Recurse -Force
}

Write-Host "Plugin $PluginName installed successfully!" -ForegroundColor Green

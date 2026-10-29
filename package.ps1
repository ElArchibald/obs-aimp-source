[CmdletBinding()]
param(
    [ValidateSet('RelWithDebInfo', 'Release', 'Debug')]
    [string]$Configuration = 'RelWithDebInfo',
    [string]$OutputDir,
    [string]$ZipName,
    [switch]$Upload
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
Set-Location $ProjectRoot

# 1. Read build specification
$buildSpecPath = Join-Path $ProjectRoot "buildspec.json"
$pluginName = "obs-aimp-source"
$version = "1.0.0"
$displayName = "AIMP Source for OBS"

if (Test-Path $buildSpecPath) {
    try {
        $buildSpec = Get-Content $buildSpecPath -Raw | ConvertFrom-Json
        if ($buildSpec.name) { $pluginName = $buildSpec.name }
        if ($buildSpec.version) { $version = $buildSpec.version }
        if ($buildSpec.displayName) { $displayName = $buildSpec.displayName }
    } catch {
        Write-Warning "Could not parse buildspec.json, using defaults."
    }
}

if (-not $OutputDir) {
    $OutputDir = Join-Path $ProjectRoot "release"
}
if (-not $ZipName) {
    $ZipName = "$pluginName-$version-windows-x64.zip"
}

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host " Packaging $displayName ($pluginName)" -ForegroundColor Cyan
Write-Host " Version: $version" -ForegroundColor Cyan
Write-Host " Configuration: $Configuration" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

# 2. Locate compiled DLL and PDB
$dllCandidates = @(
    (Join-Path $ProjectRoot "build_vs\$Configuration\$pluginName.dll"),
    (Join-Path $ProjectRoot "build_vs\RelWithDebInfo\$pluginName.dll"),
    (Join-Path $ProjectRoot "build_vs\Release\$pluginName.dll"),
    (Join-Path $ProjectRoot "build_vs\rundir\$Configuration\$pluginName\$pluginName.dll"),
    (Join-Path $ProjectRoot "build\$Configuration\$pluginName.dll"),
    (Join-Path $ProjectRoot "build\RelWithDebInfo\$pluginName.dll"),
    (Join-Path $ProjectRoot "build\Release\$pluginName.dll")
) | Where-Object { Test-Path $_ } | Sort-Object { (Get-Item $_).LastWriteTime } -Descending

$dllPath = $dllCandidates | Select-Object -First 1
if (-not $dllPath) {
    Write-Error "Could not find $pluginName.dll in build output! Please run build.ps1 first."
    exit 1
}

$pdbPath = [System.IO.Path]::ChangeExtension($dllPath, ".pdb")
if (-not (Test-Path $pdbPath)) {
    Write-Warning "PDB file not found at: $pdbPath"
    $pdbPath = $null
}

Write-Host "Found DLL: $dllPath" -ForegroundColor Green
if ($pdbPath) {
    Write-Host "Found PDB: $pdbPath" -ForegroundColor Green
}

# 3. Locate data directory
$dataPath = Join-Path $ProjectRoot "data"
if (-not (Test-Path $dataPath)) {
    Write-Error "Data folder not found at: $dataPath"
    exit 1
}
Write-Host "Found data folder: $dataPath" -ForegroundColor Green

# 4. Prepare staging directory: <OutputDir>/staging/obs-aimp-source
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

$stagingRoot = Join-Path $OutputDir "staging"
if (Test-Path $stagingRoot) {
    Remove-Item -Path $stagingRoot -Recurse -Force
}

$stagingPluginDir = Join-Path $stagingRoot $pluginName
$stagingDataDir = Join-Path $stagingPluginDir "data"
New-Item -ItemType Directory -Path $stagingDataDir -Force | Out-Null

Write-Host "`nStaging files in '$stagingPluginDir'..." -ForegroundColor Cyan

# Copy DLL and PDB directly into obs-aimp-source/
Copy-Item -Path $dllPath -Destination $stagingPluginDir -Force
Write-Host "  -> $(Split-Path $dllPath -Leaf)" -ForegroundColor Gray

if ($pdbPath -and (Test-Path $pdbPath)) {
    Copy-Item -Path $pdbPath -Destination $stagingPluginDir -Force
    Write-Host "  -> $(Split-Path $pdbPath -Leaf)" -ForegroundColor Gray
}

# Copy data/ directory directly into obs-aimp-source/data/
Copy-Item -Path "$dataPath\*" -Destination $stagingDataDir -Recurse -Force
$dataItemCount = (Get-ChildItem -Recurse $stagingDataDir).Count
Write-Host "  -> data/ ($dataItemCount items)" -ForegroundColor Gray

# 5. Create ZIP archive
$zipFilePath = Join-Path $OutputDir $ZipName
if (Test-Path $zipFilePath) {
    Remove-Item -Path $zipFilePath -Force
}

Write-Host "`nCreating ZIP archive: $zipFilePath..." -ForegroundColor Cyan
Compress-Archive -Path $stagingPluginDir -DestinationPath $zipFilePath -CompressionLevel Optimal -Force

# Clean up staging directory
Remove-Item -Path $stagingRoot -Recurse -Force

# 6. Verify ZIP archive structure
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($zipFilePath)
Write-Host "`nArchive Structure Verification:" -ForegroundColor Green
$entries = $zip.Entries | Select-Object -ExpandProperty FullName
$entries | Select-Object -First 20 | ForEach-Object { Write-Host "   $_" -ForegroundColor DarkGray }
if ($entries.Count -gt 20) {
    $remaining = $entries.Count - 20
    Write-Host "   ... and $remaining more entries ($($entries.Count) total)" -ForegroundColor DarkGray
}
$zip.Dispose()

$zipItem = Get-Item $zipFilePath
$zipSizeMb = [math]::Round($zipItem.Length / 1MB, 2)
$sha256 = (Get-FileHash -Path $zipFilePath -Algorithm SHA256).Hash.ToLower()

Write-Host "`n==========================================" -ForegroundColor Green
Write-Host " Archive successfully created!" -ForegroundColor Green
Write-Host " File:   $zipFilePath" -ForegroundColor White
Write-Host " Size:   $zipSizeMb MB ($($zipItem.Length) bytes)" -ForegroundColor White
Write-Host " SHA256: $sha256" -ForegroundColor White
Write-Host "==========================================" -ForegroundColor Green

# 7. Upload to GitHub Releases if requested
if ($Upload) {
    Write-Host "`nUploading archive to GitHub Releases..." -ForegroundColor Cyan

    $token = $env:GITHUB_TOKEN
    if (-not $token) { $token = $env:GH_TOKEN }
    if (-not $token) {
        $cred = @("protocol=https", "host=github.com") | git credential fill 2>$null
        $token = ($cred | Where-Object { $_ -match "^password=" }) -replace "^password=", ""
    }

    if (-not $token) {
        Write-Error "GitHub token not found! Set GITHUB_TOKEN or authenticate git with GitHub."
        exit 1
    }

    $remoteUrl = git remote get-url origin 2>$null
    $owner = "ElArchibald"
    $repo = "obs-aimp-source"
    if ($remoteUrl -match 'github\.com[:/]([^/]+)/([^/\.]+)') {
        $owner = $Matches[1]
        $repo = $Matches[2]
    }

    $tag = "v$version"
    $headers = @{
        "Authorization" = "token $token"
        "Accept"        = "application/vnd.github.v3+json"
        "User-Agent"    = "PowerShell-Packager"
    }

    $release = $null
    try {
        $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$owner/$repo/releases/tags/$tag" -Headers $headers -Method Get
        Write-Host "Found existing release for tag '$tag' (ID: $($release.id))" -ForegroundColor Gray
    } catch {
        Write-Host "Creating release '$displayName $version' for tag '$tag'..." -ForegroundColor Cyan
        $bodyParts = [System.Collections.Generic.List[string]]::new()
        $bodyParts.Add('## ' + $displayName + ' ' + $version)
        $bodyParts.Add('')
        $bodyParts.Add('Native AIMP audio capture, dock player control, and stylish track overlay for OBS Studio.')
        $bodyParts.Add('')
        $bodyParts.Add('### Features')
        $bodyParts.Add('- Dynamic Track Overlay: Hardware-accelerated overlay showing current track title, artist, album art, elapsed/total duration, and interactive progress.')
        $bodyParts.Add('- 4 Built-in Themes: default (Dark Glass), neon (Cyberpunk), compact (Streamer Pill), and card (Emerald Luxury).')
        $bodyParts.Add('- Smooth Marquee Scrolling: Auto-scrolling for long track titles and artists with customizable speed.')
        $bodyParts.Add('- Auto-Hide: Automatically fades out (~250ms fade) when playback is stopped or paused.')
        $bodyParts.Add('- Direct Process Audio Capture: High-quality digital loopback audio capture from AIMP via WASAPI without picking up other system sounds.')
        $bodyParts.Add('- Audio Monitoring & Mixer Control: Dedicated monitor audio channel with logarithmic dB volume fader (-inf dB to 0.0 dB).')
        $bodyParts.Add('- Native OBS Dock Panel: Full playback controls (Play/Pause, Stop, Prev, Next, Shuffle, Repeat), seek bar, player volume, and OBS source dB slider.')
        $bodyParts.Add('')
        $bodyParts.Add('### Installation')
        $bodyParts.Add('1. Download ' + $ZipName + '.')
        $bodyParts.Add('2. Extract the archive contents into your OBS Studio directory:')
        $bodyParts.Add('   - Extract the obs-aimp-source folder into <OBS-Install-Dir>\plugins\ (or into <OBS-Install-Dir>\obs-plugins\64bit\ and <OBS-Install-Dir>\data\obs-plugins\).')
        $bodyParts.Add('3. Restart OBS Studio.')
        $bodyParts.Add('4. Add **AIMP Source** to your scene.')
        $bodyParts.Add('5. Open the dock from **Docks** -> **AIMP Player Control**.')
        $bodyParts.Add('')
        $bodyParts.Add('### System Requirements')
        $bodyParts.Add('- OBS Studio: 31.0 or newer (64-bit)')
        $bodyParts.Add('- OS: Windows 10 / 11 (64-bit)')
        $bodyParts.Add('- Player: AIMP 5.0 or newer')
        $bodyParts.Add('')
        $bodyParts.Add('### Checksum (SHA-256)')
        $bodyParts.Add('```')
        $bodyParts.Add($ZipName + ': ' + $sha256)
        $bodyParts.Add('```')
        $body = [string]::Join("`n", $bodyParts)

        $releasePayload = @{
            tag_name         = $tag
            target_commitish = "main"
            name             = "$displayName $version"
            body             = $body
            draft            = $false
            prerelease       = $false
        } | ConvertTo-Json -Depth 5

        $payloadBytes = [System.Text.Encoding]::UTF8.GetBytes($releasePayload)
        $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$owner/$repo/releases" -Headers $headers -Method Post -Body $payloadBytes -ContentType "application/json; charset=utf-8"
        Write-Host "Created release ID: $($release.id)" -ForegroundColor Green
    }

    # Delete existing asset with same name if present
    $existingAsset = $release.assets | Where-Object { $_.name -eq $ZipName }
    if ($existingAsset) {
        Write-Host "Asset '$ZipName' already exists on release, replacing..." -ForegroundColor Yellow
        $deleteUrl = "https://api.github.com/repos/$owner/$repo/releases/assets/$($existingAsset.id)"
        Invoke-RestMethod -Uri $deleteUrl -Headers $headers -Method Delete | Out-Null
    }

    # Upload binary asset
    $uploadUri = "https://uploads.github.com/repos/$owner/$repo/releases/$($release.id)/assets?name=$ZipName"
    Write-Host "Uploading $ZipName ($zipSizeMb MB) to GitHub..." -ForegroundColor Cyan

    $fileBytes = [System.IO.File]::ReadAllBytes($zipFilePath)
    $uploadHeaders = @{
        "Authorization" = "token $token"
        "Accept"        = "application/vnd.github.v3+json"
        "Content-Type"  = "application/zip"
        "User-Agent"    = "PowerShell-Packager"
    }

    $uploadedAsset = Invoke-RestMethod -Uri $uploadUri -Headers $uploadHeaders -Method Post -Body $fileBytes

    Write-Host "`n==========================================" -ForegroundColor Green
    Write-Host " Release Asset Uploaded Successfully!" -ForegroundColor Green
    Write-Host " Release URL: $($release.html_url)" -ForegroundColor White
    Write-Host " Asset URL:   $($uploadedAsset.browser_download_url)" -ForegroundColor White
    Write-Host "==========================================" -ForegroundColor Green
}

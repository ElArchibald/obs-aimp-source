[CmdletBinding()]
param(
    [ValidateSet('RelWithDebInfo', 'Release', 'Debug')]
    [string]$Configuration = 'RelWithDebInfo',
    [ValidateSet('Ninja', 'VS', 'VisualStudio')]
    [string]$Generator = 'Ninja',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
Set-Location $ProjectRoot

# 1. Initialize Visual Studio 64-bit environment if cl.exe is not available
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsPath = $null
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    
    if (-not $vsPath -or -not (Test-Path $vsPath)) {
        $knownPaths = @(
            "C:\Program Files\Microsoft Visual Studio\18\Professional",
            "C:\Program Files\Microsoft Visual Studio\18\Community",
            "C:\Program Files\Microsoft Visual Studio\2022\Professional",
            "C:\Program Files\Microsoft Visual Studio\2022\Community"
        )
        foreach ($p in $knownPaths) {
            if (Test-Path "$p\VC\Auxiliary\Build\vcvars64.bat") {
                $vsPath = $p
                break
            }
        }
    }

    if ($vsPath) {
        $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path $vcvars) {
            Write-Host "Initializing MSVC environment from $vcvars..." -ForegroundColor Cyan
            cmd /c "call `"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
                if ($_ -match '^(.*?)=(.*)$') {
                    Set-Item -Path "env:\$($matches[1])" -Value $matches[2]
                }
            }
        }
    }
}

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Write-Error "cl.exe not found! Please ensure Visual Studio C++ tools are installed."
    exit 1
}

# 2. Add Pixi Qt6 tools to PATH if present
$pixiQtBin = "C:\Users\Alex\AppData\Local\runt-nightly\envs\runtimed-pixi-81de3cdf-8786-4bdb-80d9-fb762666e5f0\.pixi\envs\default\Library\bin"
if (Test-Path $pixiQtBin) {
    if ($env:PATH -notlike "*$pixiQtBin*") {
        $env:PATH = "$pixiQtBin;$env:PATH"
    }
}

if ($Generator -in @('VS', 'VisualStudio')) {
    # Visual Studio / build_vs workflow
    $buildDir = Join-Path $ProjectRoot "build_vs"
    if ($Clean -and (Test-Path $buildDir)) {
        Write-Host "Cleaning build_vs directory..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $buildDir
    }

    Write-Host "Configuring CMake (Visual Studio 2026, $Configuration)..." -ForegroundColor Cyan
    & cmake --preset windows-vs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake configure failed with exit code $LASTEXITCODE"
        Set-Location $ProjectRoot
        exit $LASTEXITCODE
    }

    Write-Host "Building with Visual Studio MSBuild ($Configuration)..." -ForegroundColor Cyan
    & cmake --build --preset windows-vs --config $Configuration
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Visual Studio build failed with exit code $LASTEXITCODE"
        Set-Location $ProjectRoot
        exit $LASTEXITCODE
    }
} else {
    # Ninja / build workflow
    $buildDir = Join-Path $ProjectRoot "build"
    if ($Clean -and (Test-Path $buildDir)) {
        Write-Host "Cleaning build directory..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $buildDir
    }

    if (-not (Test-Path $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
    }

    Set-Location $buildDir

    # Reset CMake cache if generator was Visual Studio
    $cacheFile = Join-Path $buildDir "CMakeCache.txt"
    if (Test-Path $cacheFile) {
        $cacheContent = Get-Content $cacheFile -Raw
        if ($cacheContent -match "CMAKE_GENERATOR:INTERNAL=Visual Studio") {
            Write-Host "Resetting CMake cache for Ninja generator..." -ForegroundColor Yellow
            Remove-Item -Force $cacheFile -ErrorAction SilentlyContinue
            Remove-Item -Recurse -Force (Join-Path $buildDir "CMakeFiles") -ErrorAction SilentlyContinue
        }
    }

    Write-Host "Configuring CMake (Ninja, $Configuration)..." -ForegroundColor Cyan
    $cmakeArgs = @(
        "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=$Configuration",
        ".."
    )
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "CMake configure failed with exit code $LASTEXITCODE"
        Set-Location $ProjectRoot
        exit $LASTEXITCODE
    }

    Write-Host "Building with Ninja..." -ForegroundColor Cyan
    & ninja
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Ninja build failed with exit code $LASTEXITCODE"
        Set-Location $ProjectRoot
        exit $LASTEXITCODE
    }
}

Write-Host "Build succeeded!" -ForegroundColor Green
Set-Location $ProjectRoot

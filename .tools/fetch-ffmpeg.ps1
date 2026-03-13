param(
    [Parameter(Mandatory = $true)]
    [string]$BinaryPath,
    [Parameter(Mandatory = $true)]
    [string]$FfmpegLicensePath,
    [string]$X264LicensePath,
    [string]$NoticePath,
    [string]$BuildInfoPath
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$vendorDir = Join-Path $root "third_party/ffmpeg"
$binDir = Join-Path $vendorDir "bin"

New-Item -ItemType Directory -Force -Path $binDir | Out-Null

$resolvedBinaryPath = (Resolve-Path $BinaryPath).Path
$resolvedFfmpegLicensePath = (Resolve-Path $FfmpegLicensePath).Path
Copy-Item -LiteralPath $resolvedBinaryPath -Destination (Join-Path $binDir "ffmpeg.exe") -Force
Copy-Item -LiteralPath $resolvedFfmpegLicensePath -Destination (Join-Path $vendorDir "LICENSE.txt") -Force

if (-not [string]::IsNullOrWhiteSpace($X264LicensePath)) {
    $resolvedX264LicensePath = (Resolve-Path $X264LicensePath).Path
    Copy-Item -LiteralPath $resolvedX264LicensePath -Destination (Join-Path $vendorDir "x264-LICENSE.txt") -Force
}

if (-not [string]::IsNullOrWhiteSpace($NoticePath)) {
    $resolvedNoticePath = (Resolve-Path $NoticePath).Path
    Copy-Item -LiteralPath $resolvedNoticePath -Destination (Join-Path $vendorDir "NOTICE.txt") -Force
}

if (-not [string]::IsNullOrWhiteSpace($BuildInfoPath)) {
    $resolvedBuildInfoPath = (Resolve-Path $BuildInfoPath).Path
    Copy-Item -LiteralPath $resolvedBuildInfoPath -Destination (Join-Path $vendorDir "BUILD.txt") -Force
}

Write-Host "Bundled custom ffmpeg.exe staged at $binDir"

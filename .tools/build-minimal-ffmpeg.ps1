param(
    [string]$MsysRoot = "C:\msys64",
    [ValidateSet("ucrt64", "mingw64")]
    [string]$Toolchain = "ucrt64",
    [string]$FfmpegVersion = "8.0.1",
    [string]$X264Branch = "stable",
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),
    [switch]$KeepWorkDir
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$workDir = Join-Path $root "temp/ffmpeg-minimal-build"
$srcDir = Join-Path $workDir "src"
$prefixDir = Join-Path $workDir "prefix"
$ffmpegArchivePath = Join-Path $workDir "ffmpeg-$FfmpegVersion.tar.xz"
$ffmpegSourceDir = Join-Path $srcDir "ffmpeg-$FfmpegVersion"
$x264SourceDir = Join-Path $srcDir "x264"
$bashExe = Join-Path $MsysRoot "usr\bin\bash.exe"
$stageScript = Join-Path $PSScriptRoot "fetch-ffmpeg.ps1"
$toolchainPrefix = "/$Toolchain"
$msystemName = $Toolchain.ToUpperInvariant()

function Convert-ToMsysPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WindowsPath
    )

    $fullPath = [System.IO.Path]::GetFullPath($WindowsPath)
    $normalized = $fullPath -replace '\\', '/'
    if ($normalized -match '^([A-Za-z]):/(.*)$') {
        return "/$($Matches[1].ToLowerInvariant())/$($Matches[2])"
    }
    return $normalized
}

if (-not (Test-Path $bashExe)) {
    throw "MSYS2 bash was not found at '$bashExe'. Install MSYS2 or pass -MsysRoot."
}

foreach ($requiredCommand in @("git", "tar")) {
    if ($null -eq (Get-Command $requiredCommand -ErrorAction SilentlyContinue)) {
        throw "Required command '$requiredCommand' was not found on PATH."
    }
}

New-Item -ItemType Directory -Force -Path $workDir | Out-Null
New-Item -ItemType Directory -Force -Path $srcDir | Out-Null
New-Item -ItemType Directory -Force -Path $prefixDir | Out-Null

if (-not (Test-Path $ffmpegArchivePath)) {
    Invoke-WebRequest -Uri "https://ffmpeg.org/releases/ffmpeg-$FfmpegVersion.tar.xz" -OutFile $ffmpegArchivePath
}

if (-not (Test-Path $ffmpegSourceDir)) {
    tar -xf $ffmpegArchivePath -C $srcDir
}

if (-not (Test-Path $x264SourceDir)) {
    git clone --depth 1 --branch $X264Branch https://code.videolan.org/videolan/x264.git $x264SourceDir
}

$buildScriptPath = Join-Path $workDir "build-minimal-ffmpeg.sh"
$msysPrefixDir = Convert-ToMsysPath $prefixDir
$msysX264SourceDir = Convert-ToMsysPath $x264SourceDir
$msysFfmpegSourceDir = Convert-ToMsysPath $ffmpegSourceDir
$buildScript = @'
set -euo pipefail

TOOLCHAIN_PREFIX="$1"
PREFIX="$2"
X264_DIR="$3"
FFMPEG_DIR="$4"
JOBS="$5"

export PATH="$TOOLCHAIN_PREFIX/bin:/usr/bin:$PATH"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
export CC="${CC:-gcc}"
export CXX="${CXX:-g++}"
export AR="${AR:-ar}"
export RANLIB="${RANLIB:-ranlib}"
export STRIP="${STRIP:-strip}"

cd "$X264_DIR"
make distclean >/dev/null 2>&1 || true
./configure \
  --prefix="$PREFIX" \
  --host=x86_64-w64-mingw32 \
  --enable-static \
  --bit-depth=8 \
  --chroma-format=420 \
  --disable-opencl \
  --disable-lavf \
  --disable-cli
make -j"$JOBS"
make install

cd "$FFMPEG_DIR"
make distclean >/dev/null 2>&1 || true
./configure \
  --prefix="$PREFIX" \
  --pkg-config-flags="--static" \
  --extra-cflags="-I$PREFIX/include" \
  --extra-ldflags="-L$PREFIX/lib -static -static-libgcc" \
  --extra-libs="-Wl,-Bstatic -lwinpthread -Wl,-Bdynamic -lpthread" \
  --disable-autodetect \
   --disable-debug \
  --disable-doc \
  --disable-network \
  --disable-avdevice \
  --disable-ffplay \
  --disable-ffprobe \
  --disable-decoders \
  --disable-demuxers \
  --disable-encoders \
  --disable-muxers \
  --disable-parsers \
  --disable-bsfs \
  --disable-protocols \
  --disable-indevs \
  --disable-outdevs \
  --disable-filters \
  --disable-hwaccels \
  --disable-swresample \
  --enable-small \
  --enable-gpl \
  --enable-static \
  --disable-shared \
  --enable-libx264 \
  --enable-protocol=file \
  --enable-protocol=pipe \
  --enable-demuxer=rawvideo \
  --enable-decoder=rawvideo \
  --enable-muxer=mp4 \
  --enable-muxer=mov \
  --enable-encoder=libx264 \
  --enable-filter=aformat \
  --enable-filter=anull \
  --enable-filter=atrim \
  --enable-filter=crop \
  --enable-filter=format \
  --enable-filter=hflip \
  --enable-filter=null \
  --enable-filter=rotate \
  --enable-filter=scale \
  --enable-filter=transpose \
  --enable-filter=trim \
  --enable-filter=vflip \
  --enable-swscale
make -j"$JOBS"
test -f ffmpeg.exe
strip ffmpeg.exe || true
'@
Set-Content -LiteralPath $buildScriptPath -Value $buildScript

$env:MSYSTEM = $msystemName
$env:CHERE_INVOKING = "1"
& $bashExe $buildScriptPath $toolchainPrefix $msysPrefixDir $msysX264SourceDir $msysFfmpegSourceDir $Jobs
if ($LASTEXITCODE -ne 0) {
    throw "The MSYS2 FFmpeg build failed with exit code $LASTEXITCODE."
}

$noticePath = Join-Path $workDir "NOTICE.txt"
$buildInfoPath = Join-Path $workDir "BUILD.txt"

@"
Custom minimal FFmpeg bundle for Radiary

Source repositories:
- FFmpeg: https://ffmpeg.org/releases/ffmpeg-$FfmpegVersion.tar.xz
- x264: https://code.videolan.org/videolan/x264.git (branch: $X264Branch)

Enabled runtime surface:
- protocol: file
- protocol: pipe
- demuxer: rawvideo
- muxer: mp4
- muxer: mov
- encoder: libx264
- library: swscale

Notes:
- This bundle is intended only for Radiary raw BGRA stdin export to MP4/MOV.
- The resulting binary can be embedded into Radiary.exe when RADIARY_EMBED_FFMPEG is ON.
- Review FFmpeg and x264 licensing before redistribution.
"@ | Set-Content -LiteralPath $noticePath

@"
Radiary custom minimal FFmpeg build
FFmpeg version: $FfmpegVersion
x264 branch: $X264Branch

x264 configure:
./configure --prefix=<prefix> --host=x86_64-w64-mingw32 --enable-static --bit-depth=8 --chroma-format=420 --disable-opencl --disable-lavf --disable-cli

FFmpeg configure:
./configure --prefix=<prefix> --pkg-config-flags=--static --extra-cflags=-I<prefix>/include --extra-ldflags="-L<prefix>/lib -static -static-libgcc" --extra-libs="-Wl,-Bstatic -lwinpthread -Wl,-Bdynamic -lpthread" --disable-autodetect --disable-debug --disable-doc --disable-network --disable-avdevice --disable-ffplay --disable-ffprobe --disable-decoders --disable-demuxers --disable-encoders --disable-muxers --disable-parsers --disable-bsfs --disable-protocols --disable-indevs --disable-outdevs --disable-filters --disable-hwaccels --disable-swresample --enable-small --enable-gpl --enable-static --disable-shared --enable-libx264 --enable-protocol=file --enable-protocol=pipe --enable-demuxer=rawvideo --enable-decoder=rawvideo --enable-muxer=mp4 --enable-muxer=mov --enable-encoder=libx264 --enable-filter=aformat --enable-filter=anull --enable-filter=atrim --enable-filter=crop --enable-filter=format --enable-filter=hflip --enable-filter=null --enable-filter=rotate --enable-filter=scale --enable-filter=transpose --enable-filter=trim --enable-filter=vflip --enable-swscale
"@ | Set-Content -LiteralPath $buildInfoPath

& powershell -NoProfile -ExecutionPolicy Bypass -File $stageScript `
    -BinaryPath (Join-Path $ffmpegSourceDir "ffmpeg.exe") `
    -FfmpegLicensePath (Join-Path $ffmpegSourceDir "COPYING.GPLv2") `
    -X264LicensePath (Join-Path $x264SourceDir "COPYING") `
    -NoticePath $noticePath `
    -BuildInfoPath $buildInfoPath
if ($LASTEXITCODE -ne 0) {
    throw "Staging the custom FFmpeg bundle failed with exit code $LASTEXITCODE."
}

if (-not $KeepWorkDir) {
    Remove-Item -LiteralPath $workDir -Recurse -Force
}

Write-Host "Custom minimal FFmpeg bundle staged in third_party/ffmpeg"

# =============================================================================
#  Ghost Engine MPV Setup
# =============================================================================
#  Installs mpv with the Ghost Engine configuration: audio-only playlists that
#  switch themselves, and Discord Rich Presence that shows what you are
#  playing.
#
#  This script ships no binaries. mpv, yt-dlp and node are downloaded from the
#  projects that publish them, and yt-dlp is checked against the SHA-256 sum it
#  publishes alongside the release. Nobody has to take our word for what is in
#  an .exe we handed them.
#
#  Nothing beyond Windows itself is required: `curl`/`Invoke-WebRequest` to
#  download, `tar.exe` (bsdtar, shipped since Windows 10 1803) to unpack the
#  .7z mpv is released as, and `Get-FileHash` to verify. Measured, not assumed:
#  bsdtar 3.8.4 unpacks shinchiro's mpv archive with exit code 0.
#
#  Undo it with uninstall.bat.
# =============================================================================
[CmdletBinding()]
param(
    [string]$InstallDir = "C:\mpv",
    [switch]$SkipNode,
    [switch]$SkipRichPresence,
    [switch]$Unattended
)

$ErrorActionPreference = "Stop"
# Invoke-WebRequest renders a progress bar per chunk on Windows PowerShell,
# which turns a 30 MB download into a multi-minute one. Silencing it is the
# single biggest speed difference in this script.
$ProgressPreference = "SilentlyContinue"

$MPV_RELEASES  = "https://api.github.com/repos/shinchiro/mpv-winbuild-cmake/releases?per_page=1"
$YTDLP_RELEASE = "https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest"
$NODE_EXE      = "https://nodejs.org/dist/latest/win-x64/node.exe"
$DISCORD       = "https://discord.gg/w6cR8JU3qP"
$BOT_INVITE    = "https://discord.com/oauth2/authorize?client_id=1530546215303909518&permissions=117760&scope=bot"

function Say([string]$Text, [string]$Colour = "Gray") { Write-Host "  $Text" -ForegroundColor $Colour }
function Step([string]$Text) { Write-Host ""; Write-Host "  $Text" -ForegroundColor Cyan }
function Ask([string]$Question, [bool]$Default = $true) {
    if ($Unattended) { return $Default }
    $hint = if ($Default) { "[Y/n]" } else { "[y/N]" }
    $answer = Read-Host "  $Question $hint"
    if ([string]::IsNullOrWhiteSpace($answer)) { return $Default }
    return $answer -match '^(y|e|yes|evet)$'
}

function Get-Json([string]$Url) {
    Invoke-RestMethod -Uri $Url -Headers @{
        "Accept"     = "application/vnd.github+json"
        "User-Agent" = "Ghost-Engine-MPV-Setup"
    }
}

function Fetch([string]$Url, [string]$Path, [string]$Label) {
    Say "downloading $Label ..."
    Invoke-WebRequest -Uri $Url -OutFile $Path -UseBasicParsing
    $mb = [math]::Round((Get-Item $Path).Length / 1MB, 1)
    Say "  $mb MB" "DarkGray"
}

Write-Host ""
Write-Host "  ==============================================" -ForegroundColor Magenta
Write-Host "   Ghost Engine MPV Setup" -ForegroundColor Magenta
Write-Host "  ==============================================" -ForegroundColor Magenta

# -- what we need from Windows ------------------------------------------------
$tar = Join-Path $env:SystemRoot "System32\tar.exe"
if (-not (Test-Path $tar)) {
    Say "This needs tar.exe, which Windows has shipped since Windows 10 1803." "Red"
    Say "On an older Windows, install 7-Zip and unpack mpv by hand instead." "Red"
    exit 1
}

# This script lives in lib/ so that the folder people unpack has two
# things in it they might click and both are .bat files. The payload is
# one level up.
$root = Split-Path $PSScriptRoot -Parent
$payload = Join-Path $root "mpv"
if (-not (Test-Path (Join-Path $payload "mpv.conf"))) {
    Say "Could not find the mpv folder next to this script." "Red"
    Say "Unpack the whole release zip and run install.bat from inside it." "Red"
    exit 1
}

Step "Installing to: $InstallDir"
if (-not $Unattended) {
    $typed = Read-Host "  Press Enter to accept, or type another path"
    if (-not [string]::IsNullOrWhiteSpace($typed)) { $InstallDir = $typed.Trim('"') }
}
New-Item -ItemType Directory -Force $InstallDir | Out-Null

$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("ghost-mpv-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Force $temp | Out-Null

try {
    # -- mpv ------------------------------------------------------------------
    Step "1/5  mpv"
    if (Test-Path (Join-Path $InstallDir "mpv.exe")) {
        if (-not (Ask "mpv.exe is already there. Download it again?" $false)) {
            Say "keeping the mpv you have" "DarkGray"
            $skipMpv = $true
        }
    }
    if (-not $skipMpv) {
        $release = (Get-Json $MPV_RELEASES)[0]
        $asset = $release.assets |
            Where-Object { $_.name -like "mpv-x86_64-*.7z" -and $_.name -notlike "*dev*" } |
            Select-Object -First 1
        if (-not $asset) { throw "No x86_64 mpv build in release $($release.tag_name)." }

        Say "build $($release.tag_name)" "DarkGray"
        $archive = Join-Path $temp $asset.name
        Fetch $asset.browser_download_url $archive "mpv"

        Say "unpacking ..."
        $unpacked = Join-Path $temp "mpv"
        New-Item -ItemType Directory -Force $unpacked | Out-Null
        & $tar -xf $archive -C $unpacked
        if ($LASTEXITCODE -ne 0) { throw "tar could not unpack the mpv archive." }

        Copy-Item (Join-Path $unpacked "*") $InstallDir -Recurse -Force
        Say "mpv installed" "Green"
    }

    # -- yt-dlp ---------------------------------------------------------------
    Step "2/5  yt-dlp  (this is what actually resolves YouTube links)"
    $ytRelease = Get-Json $YTDLP_RELEASE
    Say "release $($ytRelease.tag_name)" "DarkGray"
    $ytAsset = $ytRelease.assets | Where-Object { $_.name -eq "yt-dlp.exe" } | Select-Object -First 1
    $sumAsset = $ytRelease.assets | Where-Object { $_.name -eq "SHA2-256SUMS" } | Select-Object -First 1
    if (-not $ytAsset) { throw "yt-dlp.exe is missing from release $($ytRelease.tag_name)." }

    $ytTemp = Join-Path $temp "yt-dlp.exe"
    Fetch $ytAsset.browser_download_url $ytTemp "yt-dlp"

    if ($sumAsset) {
        $sumFile = Join-Path $temp "SHA2-256SUMS"
        Invoke-WebRequest -Uri $sumAsset.browser_download_url -OutFile $sumFile -UseBasicParsing
        $expected = (Get-Content $sumFile |
            Where-Object { $_ -match '\s+yt-dlp\.exe$' } |
            Select-Object -First 1) -split '\s+' | Select-Object -First 1
        $actual = (Get-FileHash $ytTemp -Algorithm SHA256).Hash.ToLower()
        if ($expected -and $actual -eq $expected.ToLower()) {
            Say "checksum verified against the published SHA-256" "Green"
        } else {
            # A mismatch means the file is not the one yt-dlp released. It does
            # not get installed, and the reason is printed rather than guessed.
            throw "yt-dlp.exe does not match its published checksum.`n       expected $expected`n       got      $actual"
        }
    } else {
        Say "no SHA2-256SUMS in this release, so nothing to verify against" "Yellow"
    }
    Copy-Item $ytTemp (Join-Path $InstallDir "yt-dlp.exe") -Force
    Say "yt-dlp installed" "Green"

    # -- node -----------------------------------------------------------------
    Step "3/5  node  (optional)"
    Say "YouTube increasingly asks the player to run JavaScript. Without node," "DarkGray"
    Say "some videos fail to resolve." "DarkGray"
    if (-not $SkipNode -and (Ask "Install node?" $true)) {
        Fetch $NODE_EXE (Join-Path $InstallDir "node.exe") "node"
        Say "node installed" "Green"
    } else {
        Say "skipped" "DarkGray"
    }

    # -- the Ghost Engine layer ----------------------------------------------
    Step "4/5  Ghost Engine configuration"
    Copy-Item (Join-Path $payload "*") $InstallDir -Recurse -Force
    Say "mpv.conf, scripts\ghost.lua, scripts\rpc_exporter.lua, ghost_rpc.py" "DarkGray"
    Say "configuration installed" "Green"

    # -- Rich Presence --------------------------------------------------------
    Step "5/5  Discord Rich Presence  (optional)"
    Say "mpv starts the presence agent itself; it needs one Python package." "DarkGray"
    if (-not $SkipRichPresence -and (Ask "Set up Rich Presence?" $true)) {
        # Written out rather than with ??, which Windows PowerShell 5.1 - the
        # one that ships with Windows - does not have.
        $py = Get-Command py.exe -ErrorAction SilentlyContinue
        if (-not $py) { $py = Get-Command python.exe -ErrorAction SilentlyContinue }
        if ($py) {
            & $py.Source -m pip install --quiet --disable-pip-version-check pypresence
            if ($LASTEXITCODE -eq 0) { Say "pypresence installed" "Green" }
            else { Say "pip failed. Run 'py -m pip install pypresence' yourself." "Yellow" }
        } else {
            Say "No Python found. Install it from python.org with 'Add Python to" "Yellow"
            Say "PATH' ticked, then run ghost_rpc.bat once." "Yellow"
        }
    } else {
        Say "skipped - playback still works, only the Discord card is missing" "DarkGray"
    }

    # -- file associations ----------------------------------------------------
    Write-Host ""
    if (Ask "Open .m3u and video files with mpv from now on?" $false) {
        & (Join-Path $InstallDir "mpv.exe") --register
        Say "registered" "Green"
    }

} finally {
    Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
}

$size = [math]::Round((Get-ChildItem $InstallDir -Recurse -File | Measure-Object Length -Sum).Sum / 1MB, 0)

Write-Host ""
Write-Host "  ==============================================" -ForegroundColor Green
Write-Host "   Done - $InstallDir  ($size MB)" -ForegroundColor Green
Write-Host "  ==============================================" -ForegroundColor Green
Write-Host ""
Say "Drag a .m3u onto mpv.exe, or double-click one if you registered the"
Say "file types. An audio export switches itself to audio-only and opens a"
Say "window you can seek and change the volume in."
Write-Host ""
Say "Add the bot to your server:" "Cyan"
Say "  $BOT_INVITE" "White"
Say "Questions, or something broke:" "Cyan"
Say "  $DISCORD" "White"
Write-Host ""
if (-not $Unattended) { Read-Host "  Press Enter to close" | Out-Null }

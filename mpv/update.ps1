# =============================================================================
#  Updates yt-dlp in place.
# =============================================================================
#  yt-dlp is the piece that resolves YouTube links, and it breaks whenever
#  YouTube changes its player. scripts/updatecheck.lua notices and puts a line
#  on screen; this is what that line asks you to run.
#
#  Same contract as the installer: fetched from yt-dlp's own release and
#  checked against the SHA-256 they publish with it. A mismatch replaces
#  nothing.
# =============================================================================
param([switch]$Unattended)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Say([string]$T, [string]$C = "Gray") { Write-Host "  $T" -ForegroundColor $C }

$target = Join-Path $PSScriptRoot "yt-dlp.exe"
$api = "https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest"

Write-Host ""
Say "yt-dlp update" "Magenta"
Write-Host ""

if (Test-Path $target) {
    $current = (& $target --version 2>$null) -join ""
    Say "installed: $current" "DarkGray"
}

$release = Invoke-RestMethod -Uri $api -Headers @{
    "Accept" = "application/vnd.github+json"; "User-Agent" = "Ghost-Engine-MPV-Setup"
}
Say "latest   : $($release.tag_name)" "DarkGray"

if ($current -and $current -eq $release.tag_name) {
    Say "Already current - nothing to do." "Green"
    Write-Host ""
    if (-not $Unattended) { Read-Host "  Press Enter to close" | Out-Null }
    exit 0
}

$exe = $release.assets | Where-Object { $_.name -eq "yt-dlp.exe" } | Select-Object -First 1
$sum = $release.assets | Where-Object { $_.name -eq "SHA2-256SUMS" } | Select-Object -First 1
if (-not $exe) { Say "yt-dlp.exe is missing from that release." "Red"; exit 1 }

$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("ytdlp-" + [guid]::NewGuid().ToString("N").Substring(0,8) + ".exe")
Say "downloading ..."
Invoke-WebRequest -Uri $exe.browser_download_url -OutFile $temp -UseBasicParsing

if ($sum) {
    $sumFile = "$temp.sums"
    Invoke-WebRequest -Uri $sum.browser_download_url -OutFile $sumFile -UseBasicParsing
    $expected = (Get-Content $sumFile | Where-Object { $_ -match '\s+yt-dlp\.exe$' } |
                 Select-Object -First 1) -split '\s+' | Select-Object -First 1
    $actual = (Get-FileHash $temp -Algorithm SHA256).Hash.ToLower()
    Remove-Item $sumFile -Force -ErrorAction SilentlyContinue
    if (-not $expected -or $actual -ne $expected.ToLower()) {
        Remove-Item $temp -Force -ErrorAction SilentlyContinue
        Say "Checksum mismatch - nothing was replaced." "Red"
        Say "  expected $expected" "DarkGray"
        Say "  got      $actual" "DarkGray"
        exit 1
    }
    Say "checksum verified" "Green"
}

# mpv holds no lock on yt-dlp between tracks, but a copy that is mid-download
# does. Closing mpv first is the reliable path, so say so rather than failing
# with a permissions error nobody can read.
try {
    Move-Item $temp $target -Force
} catch {
    Remove-Item $temp -Force -ErrorAction SilentlyContinue
    Say "Could not replace yt-dlp.exe - close mpv and run this again." "Red"
    exit 1
}

# Let the freshness check ask again next time rather than trusting yesterday's
# answer now that the answer has changed.
Remove-Item (Join-Path $PSScriptRoot ".ytdlp-checked") -Force -ErrorAction SilentlyContinue

Say "updated to $($release.tag_name)" "Green"
Write-Host ""
if (-not $Unattended) { Read-Host "  Press Enter to close" | Out-Null }

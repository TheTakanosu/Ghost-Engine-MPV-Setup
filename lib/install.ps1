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
$NODE_INDEX    = "https://nodejs.org/dist/index.json"
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

# pypresence needs 3.9; the agent is tested on 3.14.
$MIN_PYTHON     = [version]"3.9"
$PYTHON_VERSION = "3.14.10"
$PYTHON_EXE     = "https://www.python.org/ftp/python/$PYTHON_VERSION/python-$PYTHON_VERSION-amd64.exe"

# A Python that actually runs, with its version, or $null. Asking it to print
# its version is the test because Windows ships a python.exe in WindowsApps
# that is only a Microsoft Store shortcut: Get-Command finds it, it runs
# nothing, and the old check took it for Python.
function Find-Python {
    foreach ($name in "py.exe", "python.exe") {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if (-not $cmd) { continue }
        # Windows PowerShell turns a native program's stderr into a terminating
        # error under "Stop", and the Store shortcut writes to stderr.
        try { $out = & $cmd.Source -c "import sys; print('%d.%d.%d' % sys.version_info[:3])" 2>$null }
        catch { continue }
        if ($LASTEXITCODE -eq 0 -and "$out" -match '^\d+\.\d+\.\d+$') {
            return [pscustomobject]@{ Path = $cmd.Source; Version = [version]"$out" }
        }
    }
    return $null
}

# Per-user install, so no administrator prompt. The installer is checked the
# way Windows checks it: a valid Authenticode signature from the Python
# Software Foundation, or it is not run.
function Install-Python([string]$Temp) {
    $setup = Join-Path $Temp "python-setup.exe"
    Fetch $PYTHON_EXE $setup "Python $PYTHON_VERSION"
    $sig = Get-AuthenticodeSignature $setup
    if ($sig.Status -ne "Valid" -or $sig.SignerCertificate.Subject -notmatch "Python Software Foundation") {
        Say "The Python installer is not signed by the Python Software Foundation; not running it." "Red"
        return $null
    }
    Say "signature verified" "Green"
    Say "installing Python (a minute or so) ..."
    $p = Start-Process $setup -Wait -PassThru -ArgumentList "/quiet", "InstallAllUsers=0", "PrependPath=1", "Include_launcher=1", "Include_test=0"
    if ($p.ExitCode -ne 0) {
        Say "The Python installer stopped with code $($p.ExitCode)." "Red"
        return $null
    }
    # The installer adds Python to PATH for programs started from now on;
    # this window still has the old PATH, so read it back from the registry.
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
                [Environment]::GetEnvironmentVariable("Path", "User")
    $py = Find-Python
    if ($py) { Say "Python $($py.Version) installed" "Green" }
    return $py
}

# yt-dlp refuses node older than 22 (MIN_SUPPORTED_VERSION in its
# _jsruntime.py). Anything at or above that is left alone: node keeps old code
# working, so a newer one is not worth 100 MB, and an older one is the risk.
$MIN_NODE = 22

function Find-Node([string]$Dir) {
    foreach ($path in @((Join-Path $Dir "node.exe"),
                        (Get-Command node.exe -ErrorAction SilentlyContinue).Source)) {
        if (-not $path -or -not (Test-Path $path)) { continue }
        try { $v = & $path --version 2>$null } catch { continue }
        if ($LASTEXITCODE -eq 0 -and "$v" -match '^v(\d+)\.') {
            return [pscustomobject]@{ Path = $path; Version = "$v"; Major = [int]$Matches[1] }
        }
    }
    return $null
}

# The newest LTS line rather than the newest release: LTS is the one node
# itself calls stable. Checked against the SHA-256 node publishes with it.
function Install-Node([string]$Target, [string]$Temp) {
    $index = Invoke-RestMethod -Uri $NODE_INDEX -UseBasicParsing
    $lts = @($index | Where-Object { $_.lts })[0].version
    $base = "https://nodejs.org/dist/$lts"
    $file = Join-Path $Temp "node.exe"
    Fetch "$base/win-x64/node.exe" $file "node $lts (LTS)"
    $sums = Invoke-WebRequest -Uri "$base/SHASUMS256.txt" -UseBasicParsing
    $expected = (("$($sums.Content)" -split "`n") | Where-Object { $_ -match '\swin-x64/node\.exe\s*$' } |
                 Select-Object -First 1) -split '\s+' | Select-Object -First 1
    $actual = (Get-FileHash $file -Algorithm SHA256).Hash.ToLower()
    if (-not $expected -or $actual -ne $expected.ToLower()) {
        Say "node.exe does not match its published checksum - not installed." "Red"
        return
    }
    Say "checksum verified" "Green"
    Copy-Item $file $Target -Force
    Say "node installed" "Green"
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
    $existingMpv = Join-Path $InstallDir "mpv.exe"
    if (Test-Path $existingMpv) {
        # An old build is worth replacing: a December 2025 one lost its
        # playlist menu whenever the window was minimised during a song
        # change. So the default flips to yes once it is over a month old.
        $stale = $false
        $line = (& $existingMpv --version 2>$null | Select-String "built on" | Select-Object -First 1)
        if ($line -and "$line" -match 'built on (\w{3})\s+(\d{1,2})\s+(\d{4})') {
            $built = [datetime]::ParseExact("$($Matches[1]) $($Matches[2]) $($Matches[3])", "MMM d yyyy",
                                            [System.Globalization.CultureInfo]::InvariantCulture)
            $stale = ((Get-Date) - $built).TotalDays -gt 30
            Say "the mpv there was built $($built.ToString('yyyy-MM-dd'))$(if ($stale) { ' - over a month old' })" "DarkGray"
        }
        if (-not (Ask "mpv.exe is already there. Download it again?" $stale)) {
            Say "keeping the mpv you have" "DarkGray"
            $skipMpv = $true
        }
    }
    if (-not $skipMpv) {
        $release = (Get-Json $MPV_RELEASES)[0]
        $asset = $release.assets |
            # Not the -v3 build (needs AVX2) or the -dev one (libraries only).
            Where-Object { $_.name -match '^mpv-x86_64-\d{8}-git-[0-9a-f]+\.7z$' } |
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
    # The folder build, not the single yt-dlp.exe. The single file unpacks ~140
    # files into %TEMP% on every run, and Defender scans all of them again each
    # time: a second of CPU per song. The folder build is scanned once.
    $ytAsset = $ytRelease.assets | Where-Object { $_.name -eq "yt-dlp_win.zip" } | Select-Object -First 1
    $sumAsset = $ytRelease.assets | Where-Object { $_.name -eq "SHA2-256SUMS" } | Select-Object -First 1
    if (-not $ytAsset) { throw "yt-dlp_win.zip is missing from release $($ytRelease.tag_name)." }

    $ytTemp = Join-Path $temp "yt-dlp_win.zip"
    Fetch $ytAsset.browser_download_url $ytTemp "yt-dlp"

    if ($sumAsset) {
        $sumFile = Join-Path $temp "SHA2-256SUMS"
        Invoke-WebRequest -Uri $sumAsset.browser_download_url -OutFile $sumFile -UseBasicParsing
        $expected = (Get-Content $sumFile |
            Where-Object { $_ -match '\s+yt-dlp_win\.zip$' } |
            Select-Object -First 1) -split '\s+' | Select-Object -First 1
        $actual = (Get-FileHash $ytTemp -Algorithm SHA256).Hash.ToLower()
        if ($expected -and $actual -eq $expected.ToLower()) {
            Say "checksum verified against the published SHA-256" "Green"
        } else {
            # A mismatch means the file is not the one yt-dlp released. It does
            # not get installed, and the reason is printed rather than guessed.
            throw "yt-dlp_win.zip does not match its published checksum.`n       expected $expected`n       got      $actual"
        }
    } else {
        Say "no SHA2-256SUMS in this release, so nothing to verify against" "Yellow"
    }
    $ytUnpacked = Join-Path $temp "yt-dlp"
    New-Item -ItemType Directory -Force $ytUnpacked | Out-Null
    & $tar -xf $ytTemp -C $ytUnpacked
    if ($LASTEXITCODE -ne 0) { throw "tar could not unpack yt-dlp_win.zip." }
    $ytInternal = Join-Path $InstallDir "_internal"
    if (Test-Path $ytInternal) { Remove-Item $ytInternal -Recurse -Force }
    Copy-Item (Join-Path $ytUnpacked "*") $InstallDir -Recurse -Force
    Say "yt-dlp installed" "Green"

    # -- node -----------------------------------------------------------------
    Step "3/5  node  (optional)"
    Say "YouTube increasingly asks the player to run JavaScript. Without node," "DarkGray"
    Say "some videos fail to resolve." "DarkGray"
    # yt-dlp finds node beside it or on PATH, so either one already there
    # saves a 100 MB download, as long as yt-dlp accepts it.
    $node = Find-Node $InstallDir
    if ($SkipNode) {
        Say "skipped" "DarkGray"
    } elseif ($node -and $node.Major -ge $MIN_NODE) {
        Say "node $($node.Version) at $($node.Path)" "DarkGray"
        if (Ask "node is already there. Download it again?" $false) {
            Install-Node (Join-Path $InstallDir "node.exe") $temp
        } else {
            Say "keeping the node you have" "DarkGray"
        }
    } else {
        if ($node) { Say "node $($node.Version) is too old for yt-dlp (needs $MIN_NODE or newer)." "Yellow" }
        if (Ask "Install node?" $true) {
            Install-Node (Join-Path $InstallDir "node.exe") $temp
        } else {
            Say "skipped" "DarkGray"
        }
    }

    # -- the Ghost Engine layer ----------------------------------------------
    Step "4/5  Ghost Engine configuration"
    # mpv.conf goes through the merge, so running the installer again keeps
    # what someone changed in it (volume=20 stays 20).
    Copy-Item (Join-Path $payload "*") $InstallDir -Recurse -Force -Exclude "mpv.conf"
    . (Join-Path $payload "conf-merge.ps1")
    $confNote = Install-MpvConf (Join-Path $payload "mpv.conf") $InstallDir
    if ($confNote) { Say $confNote "Yellow" }
    Say "mpv.conf, scripts\*.lua, ghost_rpc.py" "DarkGray"
    Say "configuration installed" "Green"

    # -- Rich Presence --------------------------------------------------------
    Step "5/5  Discord Rich Presence  (optional)"
    Say "mpv starts the presence agent itself; it needs one Python package." "DarkGray"
    if (-not $SkipRichPresence -and (Ask "Set up Rich Presence?" $true)) {
        $py = Find-Python
        if ($py -and $py.Version -ge $MIN_PYTHON) {
            Say "Python $($py.Version)" "DarkGray"
        } else {
            # Saying yes to the presence and getting no presence is what
            # happened to people without Python: the agent needs it, and the
            # old message scrolled past. So it is offered here, defaulting to
            # yes, since the presence cannot work without it.
            if ($py) { Say "Python $($py.Version) is too old for the presence agent (needs $MIN_PYTHON or newer)." "Yellow" }
            else     { Say "The presence agent runs on Python, and there is none on this PC." "Yellow" }
            if (Ask "Install Python $PYTHON_VERSION from python.org?" $true) {
                $py = Install-Python $temp
            }
        }
        if ($py -and $py.Version -ge $MIN_PYTHON) {
            & $py.Path -m pip install --quiet --disable-pip-version-check pypresence
            if ($LASTEXITCODE -eq 0) { Say "pypresence installed" "Green" }
            else { Say "pip failed. Run 'py -m pip install pypresence' yourself." "Yellow" }
        } else {
            Say "No Rich Presence until Python is installed: python.org, with 'Add" "Yellow"
            Say "Python to PATH' ticked, then run ghost_rpc.bat once." "Yellow"
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

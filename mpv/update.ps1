# =============================================================================
#  Updates Ghost Engine, mpv and yt-dlp in place.
# =============================================================================
#  scripts/updatecheck.lua notices when either has fallen behind and puts a
#  line on screen; this is what that line asks you to run.
#
#  Ghost Engine: a newer release is installed straight away - running this is
#  the decision. A newer pre-release, a test build, is installed only if you
#  say yes. The zip is checked against the SHA-256 GitHub records for it.
#
#  mpv: replaced with the latest build when the one here is over a month
#  behind it, from the same Windows builds the installer uses.
#
#  yt-dlp: fetched from yt-dlp's own release and checked against the SHA-256
#  they publish with it, the same contract as the installer.
#
#  A checksum mismatch replaces nothing.
#
#  The app's Update window drives this too: -Check says what is behind, as
#  JSON, and changes nothing; -Only ghost,mpv,node,ytdlp updates just those
#  (all of them when left out); -Reopen starts the app again once done.
#  -Feed reads the release list from a file instead of GitHub, to try a
#  release end to end before it is published.
# =============================================================================
param([switch]$Unattended, [string[]]$Only, [switch]$Check, [string]$Reopen, [string]$Feed)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Say([string]$T, [string]$C = "Gray") { Write-Host "  $T" -ForegroundColor $C }
function Ask([string]$Question, [bool]$Default) {
    if ($Unattended) { return $Default }
    $hint = if ($Default) { "[Y/n]" } else { "[y/N]" }
    $answer = Read-Host "  $Question $hint"
    if ([string]::IsNullOrWhiteSpace($answer)) { return $Default }
    return $answer -match '^(y|e|yes|evet)$'
}
function Get-Json([string]$Url) {
    Invoke-RestMethod -Uri $Url -Headers @{
        "Accept" = "application/vnd.github+json"; "User-Agent" = "Ghost-Engine-MPV-Setup"
    }
}
function Temp-Path([string]$Name) {
    Join-Path ([System.IO.Path]::GetTempPath()) ("ghost-" + [guid]::NewGuid().ToString("N").Substring(0, 8) + $Name)
}

# Downloads a release asset and checks it against the SHA-256 GitHub keeps
# for it; throws on a mismatch, leaving nothing behind but the temp file.
function Get-Checked($Asset, [string]$To) {
    Invoke-WebRequest -Uri $Asset.browser_download_url -OutFile $To -UseBasicParsing
    $expected = "$($Asset.digest)" -replace '^sha256:', ''
    $actual = (Get-FileHash $To -Algorithm SHA256).Hash.ToLower()
    if (-not $expected -or $actual -ne $expected.ToLower()) {
        throw "checksum mismatch for $($Asset.name) (expected $expected, got $actual)"
    }
    Say "checksum verified" "Green"
}
# Copies over files that may be held for a moment: the app that has just
# closed, or Defender reading a file it just saw written.
function Copy-Retry([string]$From, [string]$To, [string[]]$Except) {
    for ($try = 1; ; $try++) {
        try { Copy-Item $From $To -Recurse -Force -Exclude $Except -ErrorAction Stop; return }
        catch { if ($try -ge 5) { throw }; Start-Sleep -Seconds 2 }
    }
}

# "v1.3.0-beta.2" -> core 1.3.0, pre "beta.2". A release sorts after its own
# pre-releases, which is the only ordering the tags here need.
function Read-Tag([string]$Tag) {
    $core, $pre = ($Tag.Trim() -replace '^v', '') -split '-', 2
    try { $v = [version]$core } catch { return $null }
    [pscustomobject]@{ Tag = $Tag.Trim(); Core = $v; Pre = "$pre" }
}
function Is-Newer($A, $B) {
    if (-not $B) { return $true }
    if ($A.Core -ne $B.Core) { return $A.Core -gt $B.Core }
    if ($A.Pre -eq $B.Pre) { return $false }
    if (-not $A.Pre) { return $true }
    if (-not $B.Pre) { return $false }
    return [string]::CompareOrdinal($A.Pre, $B.Pre) -gt 0
}

# -Only arrives as one "ghost,ytdlp" string through powershell -File.
$parts = @($Only | ForEach-Object { "$_" -split ',' } | ForEach-Object { $_.Trim().ToLower() } | Where-Object { $_ })
function Wants([string]$Part) { return (-not $parts.Count) -or ($parts -contains $Part) }

# What is installed and what is out, for each part; shared by -Check and the
# updates below, so the two can never disagree.
function Get-GhostReleases {
    try {
        # Stored first: Windows PowerShell hands a JSON array down a pipeline as
        # one object, and every release would have been filtered out together.
        $all = if ($Feed) { Get-Content $Feed -Raw | ConvertFrom-Json }
               else { Get-Json "https://api.github.com/repos/TheTakanosu/Ghost-Engine-MPV-Setup/releases?per_page=20" }
        return @($all | Where-Object { -not $_.draft })
    } catch {
        Say "Could not reach GitHub: $($_.Exception.Message)" "Yellow"
        return @()
    }
}
function Get-MpvBuilt([string]$Exe) {
    $line = (& $Exe --version 2>$null | Select-String "built on" | Select-Object -First 1)
    if ($line -and "$line" -match 'built on (\w{3})\s+(\d{1,2})\s+(\d{4})') {
        return [datetime]::ParseExact("$($Matches[1]) $($Matches[2]) $($Matches[3])", "MMM d yyyy",
                                      [System.Globalization.CultureInfo]::InvariantCulture)
    }
    return $null
}

$versionFile = Join-Path $PSScriptRoot "VERSION"
$installed = if (Test-Path $versionFile) { Read-Tag (Get-Content $versionFile -Raw) } else { $null }
$releases = Get-GhostReleases
$stable = $null; $preview = $null
foreach ($r in $releases) {
    $tag = Read-Tag $r.tag_name
    if (-not $tag) { continue }
    $tag | Add-Member -NotePropertyName Release -NotePropertyValue $r
    if ($r.prerelease) { if (Is-Newer $tag $preview) { $preview = $tag } }
    else               { if (Is-Newer $tag $stable)  { $stable  = $tag } }
}

if ($Check) {
    # One line of JSON on stdout. "newer" is whether updating would change
    # anything, by the same rules the updates below follow; a part that
    # could not be checked (offline) has no "latest".
    $report = [ordered]@{}
    $report.ghost = [ordered]@{
        installed = if ($installed) { $installed.Tag -replace '^v', '' } else { "" }
        latest = if ($stable) { $stable.Tag -replace '^v', '' } else { "" }
        newer = [bool]($stable -and (Is-Newer $stable $installed))
        notes = if ($stable) { "$($stable.Release.html_url)" } else { "" }
    }
    $ytExe = Join-Path $PSScriptRoot "yt-dlp.exe"
    $ytNow = if (Test-Path $ytExe) { (& $ytExe --version 2>$null) -join "" } else { "" }
    $ytLatest = try { (Get-Json "https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest").tag_name } catch { "" }
    $report.ytdlp = [ordered]@{
        installed = "$ytNow"; latest = "$ytLatest"
        newer = [bool]($ytLatest -and ($ytNow -ne $ytLatest -or -not (Test-Path (Join-Path $PSScriptRoot "_internal"))))
    }
    $mpvExe = Join-Path $PSScriptRoot "mpv.exe"
    if (Test-Path $mpvExe) {
        $built = Get-MpvBuilt $mpvExe
        $tagName = try { (Get-Json "https://api.github.com/repos/shinchiro/mpv-winbuild-cmake/releases?per_page=1")[0].tag_name } catch { "" }
        $latestDate = if ($tagName -match '^\d{8}$') { [datetime]::ParseExact($tagName, "yyyyMMdd", $null) } else { $null }
        $report.mpv = [ordered]@{
            installed = if ($built) { $built.ToString('yyyy-MM-dd') } else { "" }
            latest = if ($latestDate) { $latestDate.ToString('yyyy-MM-dd') } else { "" }
            newer = [bool]($latestDate -and (-not $built -or ($latestDate - $built).TotalDays -gt 30))
        }
    }
    $nodeExe = Join-Path $PSScriptRoot "node.exe"
    if (Test-Path $nodeExe) {
        $nodeVersion = try { "$(& $nodeExe --version 2>$null)" } catch { "" }
        $report.node = [ordered]@{
            installed = $nodeVersion -replace '^v', ''; latest = ""
            newer = [bool]($nodeVersion -match '^v(\d+)\.' -and [int]$Matches[1] -lt 22)
        }
    }
    $report | ConvertTo-Json -Compress -Depth 3
    exit 0
}

# Whatever goes wrong below, the app that closed itself for this is opened
# again, and a window it started stays up long enough to be read.
$failed = $false
try {
    Write-Host ""
    Say "Ghost Engine update" "Magenta"

    # -- Ghost Engine -------------------------------------------------------------
    if (Wants "ghost") {
        Write-Host ""
        Say "Ghost Engine" "Cyan"
        Say "installed: $(if ($installed) { $installed.Tag } else { 'unknown (before 1.2.5)' })" "DarkGray"
    }

    $target = $null
    if (-not (Wants "ghost")) {
        # Left out of -Only.
    } elseif ($stable -and (Is-Newer $stable $installed)) {
        Say "latest   : $($stable.Tag)" "DarkGray"
        $target = $stable
    } elseif ($preview -and (Is-Newer $preview $installed)) {
        Say "latest   : $(if ($stable) { $stable.Tag } else { 'none' })  - you have it" "DarkGray"
        Say "$($preview.Tag) is a PRE-RELEASE: a test build that may still have problems." "Yellow"
        if (Ask "Install the pre-release?" $false) { $target = $preview }
        else { Say "staying on $($installed.Tag)" "DarkGray" }
    } elseif ($releases.Count) {
        Say "Already current." "Green"
    }

    if ($target) {
        $asset = $target.Release.assets | Where-Object { $_.name -eq "Ghost-Engine-MPV-Setup.zip" } | Select-Object -First 1
        $appAsset = $target.Release.assets | Where-Object { $_.name -eq "GhostEngine-win64.zip" } | Select-Object -First 1
        $appExe = Join-Path $PSScriptRoot "GhostEngine.exe"
        if (-not $asset) {
            Say "$($target.Tag) has no Ghost-Engine-MPV-Setup.zip - nothing changed." "Red"
            $failed = $true
        } else {
            $zip = Temp-Path ".zip"; $unpacked = Temp-Path ""
            $appZip = Temp-Path ".zip"; $appDir = Temp-Path ""
            try {
                # Everything is downloaded and checked before anything here is
                # touched: a failed download or a bad checksum changes nothing.
                Say "downloading $($target.Tag) ..."
                Get-Checked $asset $zip
                Expand-Archive $zip $unpacked -Force
                $payload = Join-Path $unpacked "mpv"
                if (-not (Test-Path (Join-Path $payload "mpv.conf"))) { throw "the zip has no mpv folder" }

                # The app, built by GitHub for the same tag, where there is one
                # (releases before 1.3.0 had none) and the app is installed here.
                $withApp = $appAsset -and ((Test-Path $appExe) -or (Ask "Install the Ghost Engine app as well?" $true))
                if ($withApp) {
                    Say "downloading the app ..."
                    Get-Checked $appAsset $appZip
                    Expand-Archive $appZip $appDir -Force
                    if (-not (Test-Path (Join-Path $appDir "GhostEngine.exe"))) { throw "the app zip has no GhostEngine.exe" }

                    # The app closes itself before it starts this; one that has not
                    # gone yet gets a few seconds, then is closed.
                    $open = @(Get-Process GhostEngine -ErrorAction SilentlyContinue |
                        Where-Object { $_.Path -and (Split-Path $_.Path -Parent) -eq $PSScriptRoot })
                    if ($open.Count) {
                        $open | Wait-Process -Timeout 5 -ErrorAction SilentlyContinue
                        $open = @($open | Where-Object { -not $_.HasExited })
                        if ($open.Count -and -not (Ask "Ghost Engine is open. Close it to update it?" $true)) { throw "the app is open" }
                        $open | Stop-Process -Force -ErrorAction SilentlyContinue
                        $open | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
                    }
                }

                # cookies.txt, yt-dlp, node and mpv itself are not in the zip and
                # stay as they are. Lua scripts are read when mpv starts, so a
                # player that is open keeps the old ones until it is reopened.
                # VERSION is written last, below, so a half-done update is offered
                # again rather than taken for done.
                Copy-Retry (Join-Path $payload "*") $PSScriptRoot @("mpv.conf", "VERSION")
                # mpv.conf is merged rather than replaced: what someone changed in
                # theirs stays, what the release adds is added. The merge comes
                # from the new release, so it can learn things this script cannot.
                $mergeScript = Join-Path $payload "conf-merge.ps1"
                if (Test-Path $mergeScript) {
                    . $mergeScript
                    $confNote = Install-MpvConf (Join-Path $payload "mpv.conf") $PSScriptRoot
                    if ($confNote) { Say $confNote "Yellow" }
                } else {
                    # A release from before the merge existed: the old way.
                    $conf = Join-Path $PSScriptRoot "mpv.conf"
                    if ((Test-Path $conf) -and
                        (Get-FileHash $conf).Hash -ne (Get-FileHash (Join-Path $payload "mpv.conf")).Hash) {
                        Copy-Item $conf "$conf.bak" -Force
                        Say "your old mpv.conf is saved as mpv.conf.bak" "DarkGray"
                    }
                    Copy-Item (Join-Path $payload "mpv.conf") $PSScriptRoot -Force
                }

                if ($withApp) {
                    Copy-Retry (Join-Path $appDir "*") $PSScriptRoot @()
                    $menu = Join-Path ([Environment]::GetFolderPath("Programs")) "Ghost Engine.lnk"
                    if (-not (Test-Path $menu)) {
                        $link = (New-Object -ComObject WScript.Shell).CreateShortcut($menu)
                        $link.TargetPath = $appExe; $link.WorkingDirectory = $PSScriptRoot; $link.Save()
                    }
                    Say "app updated" "Green"
                }

                # The zip carries its own VERSION, but the tag is what was asked
                # for and what the next check compares against.
                [System.IO.File]::WriteAllText($versionFile, ($target.Tag -replace '^v', '') + "`n")
                Say "Ghost Engine updated to $($target.Tag)." "Green"
            } catch {
                Say "Ghost Engine was not updated: $($_.Exception.Message)" "Red"
                $failed = $true
            } finally {
                Remove-Item $zip, $unpacked, $appZip, $appDir -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }

    # -- mpv ------------------------------------------------------------------------
    # The installer keeps an mpv that is already there, so without this an install
    # runs the build it started with forever. That bit for real: a December 2025
    # build's console.lua crashed when the window was minimised during a song
    # change, and the playlist menu, console and stats stayed dead until mpv was
    # reopened. mpv fixed it six days after that build. A build more than a month
    # behind the latest is replaced; nightly churn alone is not worth 30 MB.
    $mpvExe = Join-Path $PSScriptRoot "mpv.exe"
    if ((Wants "mpv") -and (Test-Path $mpvExe)) {
        Write-Host ""
        Say "mpv" "Cyan"
        $built = Get-MpvBuilt $mpvExe
        if ($built) { Say "installed: built $($built.ToString('yyyy-MM-dd'))" "DarkGray" }
        try {
            $mpvRelease = (Get-Json "https://api.github.com/repos/shinchiro/mpv-winbuild-cmake/releases?per_page=1")[0]
        } catch {
            Say "Could not reach GitHub: $($_.Exception.Message)" "Yellow"
            $mpvRelease = $null
        }
        $latestDate = $null
        if ($mpvRelease -and $mpvRelease.tag_name -match '^\d{8}$') {
            $latestDate = [datetime]::ParseExact($mpvRelease.tag_name, "yyyyMMdd", $null)
            Say "latest   : built $($latestDate.ToString('yyyy-MM-dd'))" "DarkGray"
        }
        if ($latestDate -and $built -and ($latestDate - $built).TotalDays -le 30) {
            Say "Recent enough." "Green"
        } elseif ($latestDate) {
            $running = @(Get-Process mpv -ErrorAction SilentlyContinue |
                Where-Object { $_.Path -and (Split-Path $_.Path -Parent) -eq $PSScriptRoot })
            $asset = $mpvRelease.assets | Where-Object { $_.name -match '^mpv-x86_64-\d{8}-git-[0-9a-f]+\.7z$' } |
                Select-Object -First 1
            $tar = Join-Path $env:SystemRoot "System32\tar.exe"
            if ($running.Count) {
                Say "mpv is open, so it cannot be replaced. Close it and run this again." "Yellow"
            } elseif (-not $asset -or -not (Test-Path $tar)) {
                Say "No x86_64 build in that release (or no tar.exe) - mpv left as it is." "Yellow"
            } else {
                $archive = Temp-Path ".7z"
                $unpacked = Temp-Path ""
                try {
                    Say "downloading $($asset.name) ..."
                    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $archive -UseBasicParsing
                    New-Item -ItemType Directory -Force $unpacked | Out-Null
                    & $tar -xf $archive -C $unpacked
                    if ($LASTEXITCODE -ne 0) { throw "tar could not unpack the archive" }
                    if (-not (Test-Path (Join-Path $unpacked "mpv.exe"))) { throw "no mpv.exe in the archive" }
                    Copy-Item (Join-Path $unpacked "*") $PSScriptRoot -Recurse -Force
                    Say "mpv updated" "Green"
                } catch {
                    Say "mpv was not updated: $($_.Exception.Message)" "Red"
                    $failed = $true
                } finally {
                    Remove-Item $archive, $unpacked -Recurse -Force -ErrorAction SilentlyContinue
                }
            }
        }
    }

    # -- node -----------------------------------------------------------------------
    # Only the node.exe the installer put here, and only when yt-dlp would refuse
    # it (below 22, its MIN_SUPPORTED_VERSION). A working node is left alone: a
    # newer one buys nothing and is one more thing that could change.
    $nodeExe = Join-Path $PSScriptRoot "node.exe"
    if ((Wants "node") -and (Test-Path $nodeExe)) {
        $nodeVersion = $null
        try { $nodeVersion = "$(& $nodeExe --version 2>$null)" } catch { }
        if ($nodeVersion -match '^v(\d+)\.' -and [int]$Matches[1] -lt 22) {
            Write-Host ""
            Say "node" "Cyan"
            Say "installed: $nodeVersion - too old for yt-dlp (needs 22 or newer)" "Yellow"
            $file = Temp-Path ".exe"
            try {
                $index = Get-Json "https://nodejs.org/dist/index.json"
                $lts = @($index | Where-Object { $_.lts })[0].version
                $base = "https://nodejs.org/dist/$lts"
                Say "downloading node $lts (LTS) ..."
                Invoke-WebRequest -Uri "$base/win-x64/node.exe" -OutFile $file -UseBasicParsing
                $sums = Invoke-WebRequest -Uri "$base/SHASUMS256.txt" -UseBasicParsing
                $expected = (("$($sums.Content)" -split "`n") | Where-Object { $_ -match '\swin-x64/node\.exe\s*$' } |
                             Select-Object -First 1) -split '\s+' | Select-Object -First 1
                if (-not $expected -or (Get-FileHash $file -Algorithm SHA256).Hash.ToLower() -ne $expected.ToLower()) {
                    throw "checksum mismatch"
                }
                Move-Item $file $nodeExe -Force
                Say "node updated to $lts" "Green"
            } catch {
                Say "node was not updated: $($_.Exception.Message)" "Red"
                $failed = $true
            } finally {
                Remove-Item $file -Force -ErrorAction SilentlyContinue
            }
        }
    }

    # -- yt-dlp ---------------------------------------------------------------------
    $exePath = Join-Path $PSScriptRoot "yt-dlp.exe"
    $release = $null
    if (Wants "ytdlp") {
        Write-Host ""
        Say "yt-dlp" "Cyan"
        if (Test-Path $exePath) {
            $current = (& $exePath --version 2>$null) -join ""
            Say "installed: $current" "DarkGray"
        }
        try {
            $release = Get-Json "https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest"
        } catch {
            Say "Could not reach GitHub: $($_.Exception.Message)" "Yellow"
        }
    }

    if ($release) {
        Say "latest   : $($release.tag_name)" "DarkGray"
        # A current single-file yt-dlp.exe still gets replaced: that build makes
        # Defender rescan ~140 unpacked files on every song. The folder build keeps
        # them in _internal beside the exe.
        $folderBuild = Test-Path (Join-Path $PSScriptRoot "_internal")
        if ($current -and $current -eq $release.tag_name -and $folderBuild) {
            Say "Already current." "Green"
        } else {
            $exe = $release.assets | Where-Object { $_.name -eq "yt-dlp_win.zip" } | Select-Object -First 1
            $sum = $release.assets | Where-Object { $_.name -eq "SHA2-256SUMS" } | Select-Object -First 1
            if (-not $exe) {
                Say "yt-dlp_win.zip is missing from that release." "Red"
                $failed = $true
            } else {
                $temp = Temp-Path ".zip"
                Say "downloading ..."
                Invoke-WebRequest -Uri $exe.browser_download_url -OutFile $temp -UseBasicParsing

                $ok = $true
                if ($sum) {
                    $sumFile = "$temp.sums"
                    Invoke-WebRequest -Uri $sum.browser_download_url -OutFile $sumFile -UseBasicParsing
                    $expected = (Get-Content $sumFile | Where-Object { $_ -match '\s+yt-dlp_win\.zip$' } |
                                 Select-Object -First 1) -split '\s+' | Select-Object -First 1
                    $actual = (Get-FileHash $temp -Algorithm SHA256).Hash.ToLower()
                    Remove-Item $sumFile -Force -ErrorAction SilentlyContinue
                    if (-not $expected -or $actual -ne $expected.ToLower()) {
                        Remove-Item $temp -Force -ErrorAction SilentlyContinue
                        Say "Checksum mismatch - nothing was replaced." "Red"
                        $failed = $true
                        Say "  expected $expected" "DarkGray"
                        Say "  got      $actual" "DarkGray"
                        $ok = $false
                    } else {
                        Say "checksum verified" "Green"
                    }
                }

                # mpv holds no lock on yt-dlp between tracks, but a copy that is
                # mid-download does. Closing mpv first is the reliable path, so
                # say so rather than failing with a permissions error nobody can read.
                if ($ok) {
                    $unpacked = "$temp.dir"
                    New-Item -ItemType Directory -Force $unpacked | Out-Null
                    & (Join-Path $env:SystemRoot "System32\tar.exe") -xf $temp -C $unpacked
                    Remove-Item $temp -Force -ErrorAction SilentlyContinue
                    if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $unpacked "yt-dlp.exe"))) {
                        Say "Could not unpack yt-dlp_win.zip - nothing was replaced." "Red"
                        $failed = $true
                    } else {
                        # Swap in steps that can be undone: a running yt-dlp locks
                        # its files, and half a swap would leave no working copy.
                        $internal = Join-Path $PSScriptRoot "_internal"
                        $oldInternal = "$internal.old"
                        $oldExe = "$exePath.old"
                        Remove-Item $oldInternal -Recurse -Force -ErrorAction SilentlyContinue
                        Remove-Item $oldExe -Force -ErrorAction SilentlyContinue
                        try {
                            if (Test-Path $internal) { Move-Item $internal $oldInternal -ErrorAction Stop }
                            if (Test-Path $exePath) { Move-Item $exePath $oldExe -ErrorAction Stop }
                            Move-Item (Join-Path $unpacked "_internal") $internal -ErrorAction Stop
                            Move-Item (Join-Path $unpacked "yt-dlp.exe") $exePath -ErrorAction Stop
                            Remove-Item $oldInternal -Recurse -Force -ErrorAction SilentlyContinue
                            Remove-Item $oldExe -Force -ErrorAction SilentlyContinue
                            Say "updated to $($release.tag_name)" "Green"
                        } catch {
                            if (-not (Test-Path $exePath) -and (Test-Path $oldExe)) { Move-Item $oldExe $exePath }
                            if (Test-Path $oldInternal) {
                                Remove-Item $internal -Recurse -Force -ErrorAction SilentlyContinue
                                Move-Item $oldInternal $internal
                            }
                            Say "Could not replace yt-dlp - close mpv and run this again." "Red"
                            $failed = $true
                        }
                    }
                    Remove-Item $unpacked -Recurse -Force -ErrorAction SilentlyContinue
                }
            }
        }
    }

    # Let the freshness check ask again next time rather than trusting yesterday's
    # answer now that the answer may have changed. updatecheck.lua keeps its stamp
    # in mpv's config folder, %APPDATA%\mpv once that exists; older versions of
    # this script looked next to itself, so both are cleared.
    foreach ($dir in $PSScriptRoot, (Join-Path $env:APPDATA "mpv")) {
        Remove-Item (Join-Path $dir ".ytdlp-checked") -Force -ErrorAction SilentlyContinue
    }
} catch {
    Say "The update stopped: $($_.Exception.Message)" "Red"
    $failed = $true
} finally {
    Write-Host ""
    # The app closed itself to be updated: open it again, the new one if it was.
    if ($Reopen -and (Test-Path $Reopen)) {
        Start-Process -FilePath $Reopen -WorkingDirectory (Split-Path $Reopen -Parent)
    }
    # Only a window of its own waits: the app reads a hidden run line by line.
    if (-not $Unattended -or ($failed -and $Reopen)) {
        if ($failed) { Say "Something was not updated - the lines in red above say what." "Yellow" }
        Read-Host "  Press Enter to close" | Out-Null
    }
}

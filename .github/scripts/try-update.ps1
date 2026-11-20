# =============================================================================
#  Updates an install of the last release to the release being built, with
#  the last release's own update.ps1, before anything is published.
# =============================================================================
#  The update.ps1 people have is the one that will install this release; if
#  this release's files trip it up, nobody on the old version can be reached
#  again (Batin's rule: no update may ever ask for a fresh install). So the
#  old release is laid out as an install, its update.ps1 is pointed at this
#  release's files through -Feed, and afterwards the install must hold
#  exactly this release: its VERSION, its app, its scripts, with what the
#  user changed in mpv.conf and their cookies left alone. A second run must
#  find nothing to do.
#
#  release.yml runs it under Windows PowerShell 5.1 (what update.bat starts)
#  and PowerShell 7. Exit code 1 on any difference: no draft is made.
# =============================================================================
param(
    [Parameter(Mandatory)] [string]$OldDir,   # the last release's Ghost-Engine-MPV-Setup.zip and GhostEngine-win64.zip
    [Parameter(Mandatory)] [string]$OldTag,   # v1.5.0
    [Parameter(Mandatory)] [string]$NewDir,   # this build's dist folder
    [Parameter(Mandatory)] [string]$NewTag,   # v1.6.0, or anything for a run without a tag
    [ValidateSet("powershell", "pwsh")] [string]$Shell = "powershell"
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$problems = New-Object System.Collections.Generic.List[string]
function Fail([string]$What) { $problems.Add($What); Write-Host "  FAIL  $What" -ForegroundColor Red }
function Pass([string]$What) { Write-Host "  ok    $What" -ForegroundColor Green }
function Core([string]$Tag) { [version](($Tag -replace '^v', '') -replace '[-+].*$', '') }
function Hash([string]$Path) { (Get-FileHash $Path -Algorithm SHA256).Hash.ToLower() }

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("ghost-try-update-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
$inst = Join-Path $work "install"
New-Item -ItemType Directory -Force $inst | Out-Null
try {
    # -- the last release, laid out as install.ps1 leaves it --------------------
    $oldSetup = Join-Path $work "old-setup"
    Expand-Archive (Join-Path $OldDir "Ghost-Engine-MPV-Setup.zip") $oldSetup -Force
    Copy-Item (Join-Path $oldSetup "mpv\*") $inst -Recurse -Force
    Copy-Item (Join-Path $oldSetup "mpv\mpv.conf") (Join-Path $inst ".mpv.conf.shipped") -Force
    Expand-Archive (Join-Path $OldDir "GhostEngine-win64.zip") $inst -Force
    [System.IO.File]::WriteAllText((Join-Path $inst "VERSION"), ($OldTag -replace '^v', '') + "`n")
    # What a user has made their own: a setting changed, a line added, cookies.
    $conf = Join-Path $inst "mpv.conf"
    $text = Get-Content $conf -Raw
    if ($text -match '(?m)^volume=') { $text = $text -replace '(?m)^volume=.*$', 'volume=37' }
    else { $text += "`nvolume=37`n" }
    [System.IO.File]::WriteAllText($conf, $text.TrimEnd() + "`nsub-font-size=41`n")
    $cookies = Join-Path $inst "cookies.txt"
    [System.IO.File]::WriteAllText($cookies, "# Netscape HTTP Cookie File`n.youtube.com`tTRUE`t/`tTRUE`t0`tCI`ttry-update`n")
    $cookiesHash = Hash $cookies
    Write-Host "  laid out $OldTag in $inst"

    # -- this release, as the feed update.ps1 reads instead of GitHub -----------
    # A tag that is not past the old one (a run before VERSION was raised)
    # stands in as 99.0.0, and a pre-release as its plain version: what is
    # tried is the update path, which treats both the same.
    $feedTag = "v" + (Core $NewTag)
    if ((Core $feedTag) -le (Core $OldTag)) { $feedTag = "v99.0.0" }
    # The old release is listed too, newest first, as GitHub lists them: with
    # one release alone Windows PowerShell's function return made it a single
    # object without a Count, and "Already current" was never said.
    function Release-From([string]$Dir, [string]$Tag) {
        $assets = @()
        foreach ($name in "Ghost-Engine-MPV-Setup.zip", "GhostEngine-win64.zip", "SHA256SUMS") {
            $file = Join-Path $Dir $name
            if (-not (Test-Path $file)) { if ($name -ne "SHA256SUMS") { throw "$file is missing" } else { continue } }
            $uri = ([System.Uri](Resolve-Path $file).Path).AbsoluteUri
            $assets += [pscustomobject]@{ name = $name; browser_download_url = $uri; url = $uri; digest = "sha256:$(Hash $file)" }
        }
        [pscustomobject]@{
            tag_name = $Tag; name = $Tag; draft = $false; prerelease = $false
            html_url = "https://github.com/TheTakanosu/Ghost-Engine-MPV-Setup/releases/tag/$Tag"; assets = $assets
        }
    }
    $feed = Join-Path $work "feed.json"
    ConvertTo-Json @((Release-From $NewDir $feedTag), (Release-From $OldDir $OldTag)) -Depth 5 | Set-Content $feed -Encoding utf8

    # -- the old update.ps1 runs ------------------------------------------------
    $update = Join-Path $inst "update.ps1"
    $check = & $Shell -NoProfile -ExecutionPolicy Bypass -File $update -Check -Only ghost -Feed $feed 2>&1 | Out-String
    $json = ($check -split "`r?`n" | Where-Object { $_ -match '^\{' } | Select-Object -Last 1)
    if ($json -and ($json | ConvertFrom-Json).ghost.newer) { Pass "-Check offers $feedTag" }
    else { Fail "-Check did not offer $feedTag : $check" }

    $out = & $Shell -NoProfile -ExecutionPolicy Bypass -File $update -Unattended -Only ghost -Feed $feed 2>&1 | Out-String
    Write-Host $out
    if ($out -match 'was not updated|update stopped|checksum mismatch') { Fail "update.ps1 reported a failure" }

    # -- the install now holds this release ---------------------------------------
    $version = (Get-Content (Join-Path $inst "VERSION") -Raw).Trim()
    if ($version -eq ($feedTag -replace '^v', '')) { Pass "VERSION is $version" } else { Fail "VERSION is '$version', not $($feedTag -replace '^v', '')" }

    $newApp = Join-Path $work "new-app"
    Expand-Archive (Join-Path $NewDir "GhostEngine-win64.zip") $newApp -Force
    $wrong = @(Get-ChildItem $newApp -Recurse -File | Where-Object {
        $there = Join-Path $inst $_.FullName.Substring($newApp.Length + 1)
        -not (Test-Path $there) -or (Hash $there) -ne (Hash $_.FullName)
    })
    if ($wrong.Count) { Fail "app files not this build's: $(($wrong | Select-Object -First 5).Name -join ', ')" }
    else { Pass "the app is this build's ($((Get-ChildItem $newApp -Recurse -File).Count) files)" }

    $newSetup = Join-Path $work "new-setup"
    Expand-Archive (Join-Path $NewDir "Ghost-Engine-MPV-Setup.zip") $newSetup -Force
    $payload = Join-Path $newSetup "mpv"
    $wrong = @(Get-ChildItem $payload -Recurse -File | Where-Object { $_.Name -notin "mpv.conf", "VERSION" } | Where-Object {
        $there = Join-Path $inst $_.FullName.Substring($payload.Length + 1)
        -not (Test-Path $there) -or (Hash $there) -ne (Hash $_.FullName)
    })
    if ($wrong.Count) { Fail "scripts not this release's: $(($wrong | Select-Object -First 5).Name -join ', ')" }
    else { Pass "update.ps1, scripts and the rest are this release's" }

    $confNow = Get-Content $conf -Raw
    if ($confNow -match '(?m)^volume=37\s*$' -and $confNow -match '(?m)^sub-font-size=41\s*$') { Pass "the user's mpv.conf lines stayed" }
    else { Fail "the user's mpv.conf lines were lost" }
    if ((Test-Path $cookies) -and (Hash $cookies) -eq $cookiesHash) { Pass "cookies.txt untouched" } else { Fail "cookies.txt changed or gone" }

    # -- and the new update.ps1 finds nothing more to do ------------------------
    $again = & $Shell -NoProfile -ExecutionPolicy Bypass -File $update -Unattended -Only ghost -Feed $feed 2>&1 | Out-String
    if ($again -match 'Already current') { Pass "a second run is already current" } else { Fail "a second run did not say Already current: $again" }
} finally {
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}

if ($problems.Count) {
    Write-Host "::error::updating $OldTag to this release with $Shell failed: $($problems -join '; ')"
    exit 1
}
Write-Host "  $OldTag -> $feedTag with $Shell : all checks passed" -ForegroundColor Green

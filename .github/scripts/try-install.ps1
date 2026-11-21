# =============================================================================
#  Installs the release being built from nothing, the way install.bat does,
#  before anything is published.
# =============================================================================
#  1.5.0's setup, tried in Windows Sandbox, stopped at node on a PC without
#  it: node, Python and 7zr were fetched by plain link, which a helper read
#  as empty ("Uri is null"). The same bug had shipped in 1.4.0. A clean
#  runner has node on PATH, which would hide that path again, so node is
#  taken off PATH here and must be downloaded. Afterwards the folder must
#  hold mpv, yt-dlp, node, this release's scripts and VERSION, and this
#  build's app.
#
#  Exit code 1 on anything missing: no draft is made.
# =============================================================================
param(
    [Parameter(Mandatory)] [string]$NewDir,   # this build's dist folder
    [string]$InstallDir
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$problems = New-Object System.Collections.Generic.List[string]
function Fail([string]$What) { $problems.Add($What); Write-Host "  FAIL  $What" -ForegroundColor Red }
function Pass([string]$What) { Write-Host "  ok    $What" -ForegroundColor Green }
function Hash([string]$Path) { (Get-FileHash $Path -Algorithm SHA256).Hash.ToLower() }

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("ghost-try-install-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
if (-not $InstallDir) { $InstallDir = Join-Path $work "mpv" }
$oldPath = $env:Path
try {
    $setup = Join-Path $work "setup"
    Expand-Archive (Join-Path $NewDir "Ghost-Engine-MPV-Setup.zip") $setup -Force
    $appZip = (Resolve-Path (Join-Path $NewDir "GhostEngine-win64.zip")).Path

    # No node to be found: the download is what is tried.
    $env:Path = (($env:Path -split ';') | Where-Object {
        $_ -and -not (Test-Path (Join-Path $_ "node.exe"))
    }) -join ';'
    if (Get-Command node.exe -ErrorAction SilentlyContinue) { throw "node is still on PATH" }

    # install.bat itself, so its own line (Windows PowerShell 5.1) is what runs.
    # With the module path a double-click gives it: started from PowerShell 7,
    # 5.1 took 7's modules and had no Get-FileHash (the 1.6.0 run).
    $oldModules = $env:PSModulePath
    $env:PSModulePath = [Environment]::GetEnvironmentVariable("PSModulePath", "Machine")
    $out = & cmd.exe /c "`"$(Join-Path $setup 'install.bat')`" -Unattended -InstallDir `"$InstallDir`" -AppZip `"$appZip`"" 2>&1 | Out-String
    $code = $LASTEXITCODE
    $env:PSModulePath = $oldModules
    Write-Host $out
    $env:Path = $oldPath

    if ($code -eq 0) { Pass "install.bat exited with 0" } else { Fail "install.bat exited with $code" }
    if ($out -match 'was not installed|did not finish|not match') { Fail "setup reported a step that failed" }

    foreach ($file in "mpv.exe", "yt-dlp.exe", "node.exe", "GhostEngine.exe", "mpv.conf", "VERSION", "update.ps1") {
        if (Test-Path (Join-Path $InstallDir $file)) { Pass "$file is there" } else { Fail "$file is missing" }
    }
    $node = Join-Path $InstallDir "node.exe"
    if (Test-Path $node) {
        $v = "$(& $node --version 2>$null)"
        if ($v -match '^v(\d+)\.' -and [int]$Matches[1] -ge 22) { Pass "node $v runs" } else { Fail "node does not run ('$v')" }
    }
    $yt = Join-Path $InstallDir "yt-dlp.exe"
    if (Test-Path $yt) {
        $v = (& $yt --version 2>$null) -join ""
        if ($v -match '^\d{4}\.') { Pass "yt-dlp $v runs" } else { Fail "yt-dlp does not run ('$v')" }
    }

    $payload = Join-Path $setup "mpv"
    $wrong = @(Get-ChildItem $payload -Recurse -File | Where-Object { $_.Name -ne "mpv.conf" } | Where-Object {
        $there = Join-Path $InstallDir $_.FullName.Substring($payload.Length + 1)
        -not (Test-Path $there) -or (Hash $there) -ne (Hash $_.FullName)
    })
    if ($wrong.Count) { Fail "not this release's: $(($wrong | Select-Object -First 5).Name -join ', ')" }
    else { Pass "scripts and VERSION are this release's" }

    $app = Join-Path $work "app"
    Expand-Archive $appZip $app -Force
    if ((Test-Path (Join-Path $InstallDir "GhostEngine.exe")) -and
        (Hash (Join-Path $InstallDir "GhostEngine.exe")) -eq (Hash (Join-Path $app "GhostEngine.exe"))) {
        Pass "GhostEngine.exe is this build's"
    } else { Fail "GhostEngine.exe is not this build's" }
} finally {
    $env:Path = $oldPath
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}

if ($problems.Count) {
    Write-Host "::error::a fresh install of this release failed: $($problems -join '; ')"
    exit 1
}
Write-Host "  fresh install: all checks passed" -ForegroundColor Green

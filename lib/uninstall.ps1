# =============================================================================
#  Removes what install.ps1 put in place.
# =============================================================================
#  Deliberately narrow: it deletes the install folder and nothing else. No
#  registry sweep, no "clean up related files" - an uninstaller that goes
#  looking for things to delete is how people lose data they wanted.
# =============================================================================
param([string]$InstallDir = "C:\mpv", [switch]$Unattended)
$ErrorActionPreference = "Stop"

function Say([string]$T, [string]$C = "Gray") { Write-Host "  $T" -ForegroundColor $C }

Write-Host ""
Say "Ghost Engine MPV - uninstall" "Magenta"
Write-Host ""

if (-not (Test-Path $InstallDir)) { Say "Nothing installed at $InstallDir" "Yellow"; exit 0 }

Say "This will delete: $InstallDir"
Say "Anything you put in that folder yourself goes with it." "Yellow"
if (-not $Unattended) {
    $answer = Read-Host "  Type the word DELETE to confirm"
    if ($answer -ne "DELETE") { Say "Cancelled - nothing was removed." "Yellow"; exit 0 }
}

# The presence agent holds no lock on the folder, but it does keep running
# after mpv closes if it was started by hand; stop it so nothing lingers.
Get-CimInstance Win32_Process -Filter "Name LIKE 'python%'" |
    Where-Object { $_.CommandLine -like "*ghost_rpc*" } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }

Get-Process GhostEngine -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and (Split-Path $_.Path -Parent) -eq $InstallDir } |
    Stop-Process -Force -ErrorAction SilentlyContinue
# The shortcuts install.bat made, by name, only where they point into the folder.
foreach ($place in [Environment]::GetFolderPath("Programs"), [Environment]::GetFolderPath("Desktop")) {
    $lnk = Join-Path $place "Ghost Engine.lnk"
    if ((Test-Path $lnk) -and (New-Object -ComObject WScript.Shell).CreateShortcut($lnk).TargetPath -like "$InstallDir*") {
        Remove-Item $lnk -Force
        Say "removed $lnk" "DarkGray"
    }
}

$mpv = Join-Path $InstallDir "mpv.exe"
# Waited for: mpv.exe is a windowed program, so calling it straight returned
# at once and the folder was then deleted under it ("access denied").
if (Test-Path $mpv) {
    Start-Process $mpv -ArgumentList "--unregister" -Wait -WindowStyle Hidden
    Say "file associations removed" "DarkGray"
}

Remove-Item $InstallDir -Recurse -Force
Say "removed $InstallDir" "Green"
Write-Host ""
if (-not $Unattended) { Read-Host "  Press Enter to close" | Out-Null }

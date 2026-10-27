# =============================================================================
#  Installs a new mpv.conf without losing what the user changed in theirs.
# =============================================================================
#  Dot-sourced by lib/install.ps1 and update.ps1. Three versions take part:
#
#    base - the mpv.conf Ghost Engine installed last time, kept as
#           .mpv.conf.shipped next to it
#    new  - the mpv.conf this release ships
#    user - the mpv.conf on disk now
#
#  For every setting: a value the user changed stays; a value they left alone
#  follows the release; a setting the release adds is added; one it drops is
#  dropped unless the user changed it; one the user deleted stays deleted.
#  This is what git does with a three-way merge, one "key=value" line at a
#  time, and it means volume=20 survives an update that also adds new options.
#
#  An mpv.conf.bak left by the 1.2.5/1.2.6 update.bat is the user's file when
#  the mpv.conf beside it is untouched; see Install-MpvConf.
#
#  Before .mpv.conf.shipped existed (installs before 1.2.7) there is no base.
#  mpv.conf had never changed in any release until then, so the new one stands
#  in for it: every difference is then the user's and is kept.
# =============================================================================

# One entry per setting, keyed by "[profile]key" so the same option in two
# profiles stays two settings. Later duplicates win, as they do in mpv.
function Read-MpvConf([string]$Text) {
    $settings = [ordered]@{}
    $section = ""
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -match '^\s*\[([^\]]+)\]\s*$') { $section = $Matches[1]; continue }
        if ($line -match '^\s*([A-Za-z0-9][A-Za-z0-9_/.-]*)\s*=\s*(.*?)\s*$') {
            $settings["[$section]$($Matches[1])"] = $Matches[2]
        }
    }
    return $settings
}

function Merge-MpvConf([string]$Base, [string]$New, [string]$User) {
    $b = Read-MpvConf $Base
    $n = Read-MpvConf $New
    $u = Read-MpvConf $User
    $report = [pscustomobject]@{ Kept = @(); Updated = @(); Added = @(); Removed = @() }

    # Values to rewrite in the user's file, keys to drop from it, and keys to
    # append (grouped by profile).
    $set = @{}; $drop = @{}; $append = [ordered]@{}

    foreach ($key in $n.Keys) {
        $name = $key -replace '^\[[^\]]*\]', ''
        if ($u.Contains($key)) {
            if ($u[$key] -eq $n[$key]) { continue }
            if ($b.Contains($key) -and $u[$key] -eq $b[$key]) {
                $set[$key] = $n[$key]; $report.Updated += "$name=$($n[$key])"
            } else {
                $report.Kept += "$name=$($u[$key])"
            }
        } elseif (-not $b.Contains($key)) {
            $section = ([regex]::Match($key, '^\[([^\]]*)\]')).Groups[1].Value
            if (-not $append.Contains($section)) { $append[$section] = @() }
            $append[$section] += "$name=$($n[$key])"
            $report.Added += "$name=$($n[$key])"
        }
        # In base, missing from user: the user deleted it. Leave it deleted.
    }
    foreach ($key in $b.Keys) {
        if (-not $n.Contains($key) -and $u.Contains($key) -and $u[$key] -eq $b[$key]) {
            $drop[$key] = $true
            $report.Removed += ($key -replace '^\[[^\]]*\]', '')
        }
    }

    # Rewrite the user's file line by line, so their comments and order stay.
    $out = New-Object System.Collections.Generic.List[string]
    $section = ""
    $lines = $User -split "`r?`n"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line -match '^\s*\[([^\]]+)\]\s*$') {
            # Leaving a section: what the release added to it goes at its end.
            if ($append.Contains($section)) { $append[$section] | ForEach-Object { $out.Add($_) }; $append.Remove($section) }
            $section = $Matches[1]
            $out.Add($line); continue
        }
        if ($line -match '^(\s*)([A-Za-z0-9][A-Za-z0-9_/.-]*)(\s*=\s*)(.*?)\s*$') {
            $key = "[$section]$($Matches[2])"
            if ($drop.ContainsKey($key)) { continue }
            if ($set.ContainsKey($key)) { $line = $Matches[1] + $Matches[2] + $Matches[3] + $set[$key] }
        }
        $out.Add($line)
    }
    # A trailing empty line from the split is kept out of the way of appends.
    while ($out.Count -and $out[$out.Count - 1] -eq "") { $out.RemoveAt($out.Count - 1) }
    if ($append.Contains($section)) { $append[$section] | ForEach-Object { $out.Add($_) }; $append.Remove($section) }
    foreach ($s in $append.Keys) {
        $out.Add(""); if ($s) { $out.Add("[$s]") }
        $append[$s] | ForEach-Object { $out.Add($_) }
    }

    $report | Add-Member -NotePropertyName Text -NotePropertyValue (($out -join "`r`n") + "`r`n")
    return $report
}

# Puts $NewConf in place as $Dir\mpv.conf and records it as the next base.
# Returns a line describing what happened, or $null when nothing needs saying.
function Install-MpvConf([string]$NewConf, [string]$Dir) {
    $target = Join-Path $Dir "mpv.conf"
    $shipped = Join-Path $Dir ".mpv.conf.shipped"
    $bak = "$target.bak"
    $checked = Join-Path $Dir ".mpv.conf.bak-checked"
    $new = [System.IO.File]::ReadAllText($NewConf)
    $message = $null

    if (-not (Test-Path $target)) {
        [System.IO.File]::WriteAllText($target, $new)
    } else {
        $user = [System.IO.File]::ReadAllText($target)
        $base = if (Test-Path $shipped) { [System.IO.File]::ReadAllText($shipped) } else { $new }
        $norm = { param($t) ($t -replace "`r`n", "`n").TrimEnd() }
        $recovered = $false

        # The update.bat of 1.2.5 and 1.2.6 knew no merge: it saved a changed
        # mpv.conf as mpv.conf.bak and put the shipped one in its place. That
        # left an untouched mpv.conf with the user's real one beside it. Their
        # changes are taken from the .bak, once per install, so someone who
        # later goes back to the defaults on purpose is not undone by an old
        # backup.
        if (-not (Test-Path $checked) -and (Test-Path $bak) -and
            (& $norm $user) -eq (& $norm $base)) {
            $old = [System.IO.File]::ReadAllText($bak)
            if ((& $norm $old) -ne (& $norm $user)) { $user = $old; $recovered = $true }
        }

        if (-not $recovered -and (& $norm $user) -eq (& $norm $base)) {
            # Untouched since we wrote it: the new one goes in as shipped,
            # comments and all.
            [System.IO.File]::WriteAllText($target, $new)
        } else {
            $merged = Merge-MpvConf $base $new $user
            if ($recovered) {
                # The .bak stays as it is: it is the user's file.
                [System.IO.File]::WriteAllText($target, $merged.Text)
            } elseif ((& $norm $merged.Text) -ne (& $norm $user)) {
                Copy-Item $target $bak -Force
                [System.IO.File]::WriteAllText($target, $merged.Text)
            }
            $parts = @()
            if ($recovered)      { $parts += "brought back your changes from mpv.conf.bak" }
            if ($merged.Kept)    { $parts += "kept your " + ($merged.Kept -join ", ") }
            if ($merged.Updated) { $parts += "updated " + ($merged.Updated -join ", ") }
            if ($merged.Added)   { $parts += "added " + ($merged.Added -join ", ") }
            if ($merged.Removed) { $parts += "removed " + ($merged.Removed -join ", ") }
            if ($parts) { $message = "mpv.conf: " + ($parts -join "; ") }
        }
    }
    [System.IO.File]::WriteAllText($shipped, $new)
    [System.IO.File]::WriteAllText($checked, "")
    return $message
}

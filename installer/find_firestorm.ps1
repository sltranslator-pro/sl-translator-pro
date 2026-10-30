# Used by installer.nsi (FindFirestormByScan). Prints one Firestorm folder or nothing.
$ok = { param($d) $d -and (Test-Path (Join-Path $d 'skins\default\xui\en')) }
$sw = [Diagnostics.Stopwatch]::StartNew()
$limit = 15

$roots = @()
foreach ($drv in [IO.DriveInfo]::GetDrives() | Where-Object { $_.DriveType -eq 'Fixed' -and $_.IsReady }) {
    $r = $drv.RootDirectory.FullName
    $roots += @{ p = $r; d = 1 }
    $roots += @{ p = (Join-Path $r 'Program Files'); d = 1 }
    $roots += @{ p = (Join-Path $r 'Program Files (x86)'); d = 1 }
    $roots += @{ p = (Join-Path $r 'Games'); d = 2 }
}
$roots += @{ p = (Join-Path $env:LOCALAPPDATA 'Programs'); d = 1 }
$roots += @{ p = $env:USERPROFILE; d = 2 }

$found = New-Object System.Collections.Generic.List[string]
foreach ($root in $roots) {
    if ($sw.Elapsed.TotalSeconds -gt $limit) { break }
    if (-not (Test-Path $root.p)) { continue }
    Get-ChildItem -Path $root.p -Filter 'dinput8.dll' -File -Recurse -Depth $root.d -ErrorAction SilentlyContinue |
        ForEach-Object { $_.DirectoryName } | Where-Object { & $ok $_ } | ForEach-Object { $found.Add($_) }
    Get-ChildItem -Path $root.p -Filter 'Firestorm*.exe' -File -Recurse -Depth $root.d -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notmatch 'uninst|setup|installer' } |
        ForEach-Object { $_.DirectoryName } | Where-Object { & $ok $_ } | ForEach-Object { $found.Add($_) }
}
$found = $found | Select-Object -Unique
$best = $found | Where-Object { Test-Path (Join-Path $_ 'dinput8.dll') } | Select-Object -First 1
if (-not $best) { $best = $found | Select-Object -First 1 }
if ($best) { [Console]::Write($best) }

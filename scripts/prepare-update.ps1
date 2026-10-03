param(
    [Parameter(Mandatory = $true)][string]$ExecutablePath,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9a-f]{40}$')][string]$Commit,
    [Parameter(Mandatory = $true)][ValidatePattern('^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$')][string]$Version,
    [string]$OutputDirectory = 'dist/update'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path $PSScriptRoot -Parent
$resolvedCommit = & git -C $projectRoot rev-parse --verify HEAD
if ($LASTEXITCODE -ne 0 -or $resolvedCommit -cne $Commit) { throw 'Update commit must match the checked-out source.' }
$outputRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
if (Test-Path -LiteralPath $outputRoot) { throw 'Choose a new output directory for the update assets.' }
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$notesChanged = @(& git -C $projectRoot diff-tree --root --first-parent -m --no-commit-id --name-only -r $Commit -- UPDATE_NOTES.md)
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect the commit notes.' }
$notes = @()
if ($notesChanged -contains 'UPDATE_NOTES.md') {
    foreach ($line in Get-Content -LiteralPath (Join-Path $projectRoot 'UPDATE_NOTES.md') -Encoding utf8) {
        if ($line -match '^\s*[-*]\s+(.+?)\s*$') { $notes += $Matches[1] }
    }
} else {
    $commitLines = @(& git -C $projectRoot show -s --format=%B $Commit)
    if ($LASTEXITCODE -ne 0) { throw 'Could not read the commit message.' }
    foreach ($line in $commitLines) {
        $note = ($line -replace '^\s*[-*]\s+', '').Trim()
        if ($note.Length -and $note -notmatch '^(Co-authored-by|Signed-off-by|Reviewed-by|Acked-by|Tested-by):') { $notes += $note }
    }
}
if (!$notes.Count) { throw 'Provide a commit description or bullets in UPDATE_NOTES.md.' }
if ($notes.Count -gt 32 -or @($notes | Where-Object { $_.Length -gt 1000 -or $_ -match '[\x00-\x1F\x7F\u202A-\u202E\u2066-\u2069]' }).Count) {
    throw 'Use up to 32 update notes, each at most 1000 characters and without control characters.'
}
$sources = @(
    @{ Name = 'Input Overlay.exe'; Path = (Get-Item -LiteralPath $ExecutablePath).FullName },
    @{ Name = 'LICENSE'; Path = Join-Path $projectRoot 'LICENSE' },
    @{ Name = 'README.md'; Path = Join-Path $projectRoot 'README.md' },
    @{ Name = 'THIRD_PARTY_NOTICES.txt'; Path = Join-Path $projectRoot 'THIRD_PARTY_NOTICES.txt' }
)
$files = foreach ($source in $sources) {
    $assetName = if ($source.Name -eq 'Input Overlay.exe') { 'Input-Overlay.exe' } else { $source.Name }
    $destination = Join-Path $outputRoot $assetName
    Copy-Item -LiteralPath $source.Path -Destination $destination -ErrorAction Stop
    $item = Get-Item -LiteralPath $destination
    $maximum = if ($source.Name -eq 'Input Overlay.exe') { 33554432 } else { 2097152 }
    if (!$item.Length -or $item.Length -gt $maximum) { throw 'Update assets must be nonempty and within the executable/document size limits.' }
    [ordered]@{ name = $source.Name; sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant(); size = $item.Length }
}
$manifest = [ordered]@{ schema = 1; commit = $Commit; version = $Version; notes = @($notes); files = @($files) }
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $outputRoot 'update.json'), ($manifest | ConvertTo-Json -Depth 5), $utf8)
[IO.File]::WriteAllText((Join-Path $outputRoot 'release-notes.md'), (($notes | ForEach-Object { '- ' + $_ }) -join "`n") + "`n", $utf8)
Write-Output $outputRoot

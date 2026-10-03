param(
    [Parameter(Mandatory = $true)]
    [string]$ExecutablePath,
    [ValidatePattern('^v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$')]
    [string]$Version = '0.1.0',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9 _-]{0,63}$')]
    [string]$Name = 'Input Overlay',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path $PSScriptRoot -Parent
$versionNumber = $Version.TrimStart('v')
$packageName = ($Name.Trim() -replace ' +', '-') + '-windows-x64.zip'
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path 'dist/releases' ('v' + $versionNumber)
}
$executable = (Get-Item -LiteralPath $ExecutablePath -ErrorAction Stop).FullName
$license = Join-Path $projectRoot 'LICENSE'
$readme = Join-Path $projectRoot 'README.md'
$notices = Join-Path $projectRoot 'THIRD_PARTY_NOTICES.txt'
foreach ($required in @($license, $readme, $notices)) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf) -or [string]::IsNullOrWhiteSpace([IO.File]::ReadAllText($required))) {
        throw 'A nonempty LICENSE, README.md and THIRD_PARTY_NOTICES.txt are required before a distributable package can be created.'
    }
}
if ([IO.Path]::GetExtension($executable) -ine '.exe') { throw 'ExecutablePath must identify the built Windows executable.' }
$binary = [IO.File]::ReadAllBytes($executable)
if ($binary.Length -lt 256 -or [BitConverter]::ToUInt16($binary, 0) -ne 0x5a4d) { throw 'The executable is not a valid Windows PE image.' }
$peOffset = [BitConverter]::ToInt32($binary, 0x3c)
if ($peOffset -lt 64 -or $peOffset -gt $binary.Length - 96 -or [BitConverter]::ToUInt32($binary, $peOffset) -ne 0x4550) {
    throw 'The executable has an invalid PE header.'
}
if ([BitConverter]::ToUInt16($binary, $peOffset + 4) -ne 0x8664 -or
    [BitConverter]::ToUInt16($binary, $peOffset + 24) -ne 0x20b -or
    [BitConverter]::ToUInt16($binary, $peOffset + 24 + 68) -ne 2 -or
    ([BitConverter]::ToUInt16($binary, $peOffset + 22) -band 0x2000)) {
    throw 'The package requires an x64 GUI executable.'
}
$members = @(
    [pscustomobject]@{ Source = $executable; Entry = $Name.Trim() + '.exe' },
    [pscustomobject]@{ Source = $license; Entry = 'LICENSE' },
    [pscustomobject]@{ Source = $readme; Entry = 'README.md' },
    [pscustomobject]@{ Source = $notices; Entry = 'THIRD_PARTY_NOTICES.txt' }
)
$outputRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
$archivePath = Join-Path $outputRoot $packageName
$checksumPath = $archivePath + '.sha256'
if ((Test-Path -LiteralPath $archivePath) -or (Test-Path -LiteralPath $checksumPath)) {
    throw 'The package or checksum already exists. Choose a new output directory or remove the previous generated package explicitly.'
}
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$temporaryArchive = $archivePath + '.tmp.' + [Guid]::NewGuid().ToString('N')
$temporaryChecksum = $checksumPath + '.tmp.' + [Guid]::NewGuid().ToString('N')
$timestamp = [DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
try {
    $stream = [IO.File]::Open($temporaryArchive, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        $archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create, $true, [Text.Encoding]::UTF8)
        try {
            foreach ($member in $members) {
                $entry = $archive.CreateEntry($member.Entry, [IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = $timestamp
                $entry.ExternalAttributes = 0
                $sourceStream = [IO.File]::OpenRead($member.Source)
                try {
                    $entryStream = $entry.Open()
                    try { $sourceStream.CopyTo($entryStream) } finally { $entryStream.Dispose() }
                } finally { $sourceStream.Dispose() }
            }
        } finally { $archive.Dispose() }
        $stream.Flush($true)
    } finally { $stream.Dispose() }
    $digest = (Get-FileHash -LiteralPath $temporaryArchive -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText($temporaryChecksum, "$digest  $packageName`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temporaryArchive, $archivePath)
    [IO.File]::Move($temporaryChecksum, $checksumPath)
    [pscustomobject]@{ Package = $archivePath; Checksum = $checksumPath; Files = $members.Entry }
} finally {
    foreach ($temporary in @($temporaryArchive, $temporaryChecksum)) {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) { Remove-Item -LiteralPath $temporary -Force }
    }
}

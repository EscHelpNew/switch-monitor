$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
$monitorRoot = $PSScriptRoot
$outputDir = Join-Path $monitorRoot 'build'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$output = Join-Path $outputDir 'switch-monitor-source.zip'
$temporary = Join-Path $outputDir ('source-' + [guid]::NewGuid().ToString('N') + '.zip')
$files = @('README.md', 'LICENSE', '.gitignore', 'config.example.json', 'build.ps1', 'run.ps1', 'run.cmd', 'package.ps1', 'src/Monitor.cs', 'tests/MonitorTests.cs')
$kitRoot = Join-Path $monitorRoot 'switch-debug-kit'
$kitPrefix = [IO.Path]::GetFullPath($kitRoot) + [IO.Path]::DirectorySeparatorChar
$manifest = Join-Path $kitRoot 'package-files.txt'
foreach ($line in [IO.File]::ReadAllLines($manifest)) {
    $relative = $line.Trim()
    if (-not $relative -or $relative.StartsWith('#')) { continue }
    if ($relative -notmatch '^(?:[A-Za-z0-9_.-]+/)*[A-Za-z0-9_.-]+$' -or
        @($relative.Split('/') | Where-Object { $_ -eq '.' -or $_ -eq '..' }).Count -gt 0 -or
        $relative -match '^(build|data)/' -or $relative -eq 'project.json') {
        throw "Invalid public kit path: $relative"
    }
    $sourcePath = [IO.Path]::GetFullPath((Join-Path $kitRoot $relative))
    if (-not $sourcePath.StartsWith($kitPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        -not [IO.File]::Exists($sourcePath)) { throw "Missing or outside-kit file: $relative" }
    $files += 'switch-debug-kit/' + $relative
}
if (@($files | Select-Object -Unique).Count -ne $files.Count) { throw 'Duplicate public source path' }
$stream = [IO.File]::Create($temporary)
$archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create, $false)
try {
    foreach ($relative in $files) {
        $entry = $archive.CreateEntry('switch-monitor/' + $relative)
        $destination = $entry.Open()
        try {
            $bytes = [IO.File]::ReadAllBytes((Join-Path $monitorRoot $relative))
            $destination.Write($bytes, 0, $bytes.Length)
        } finally { $destination.Dispose() }
    }
} finally { $archive.Dispose(); $stream.Dispose() }
[IO.File]::Copy($temporary, $output, $true)
[IO.File]::Delete($temporary)
Write-Output $output

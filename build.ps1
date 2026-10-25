param([switch]$Test)
$ErrorActionPreference = 'Stop'
$monitorRoot = $PSScriptRoot
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if (-not (Test-Path -LiteralPath $compiler)) {
    $compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework/v4.0.30319/csc.exe'
}
if (-not (Test-Path -LiteralPath $compiler)) { throw 'Install .NET Framework 4.8 (Windows desktop runtime with C# compiler).' }
$outputDir = Join-Path $monitorRoot 'build'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$source = Join-Path $monitorRoot 'src/Monitor.cs'
$references = @('/r:System.Windows.Forms.dll', '/r:System.Drawing.dll', '/r:System.Runtime.Serialization.dll')
if ($Test) {
    $output = Join-Path $outputDir 'MonitorTests.exe'
    & $compiler /nologo /codepage:65001 /target:exe /main:SwitchMonitor.MonitorTests "/out:$output" @references $source (Join-Path $monitorRoot 'tests/MonitorTests.cs')
    if ($LASTEXITCODE -ne 0) { throw "Test compilation failed: $LASTEXITCODE" }
    & $output
    if ($LASTEXITCODE -ne 0) { throw "Tests failed: $LASTEXITCODE" }
} else {
    $output = Join-Path $outputDir 'SwitchMonitor.exe'
    & $compiler /nologo /codepage:65001 /target:winexe /optimize+ /main:SwitchMonitor.Program "/out:$output" @references $source
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
    Write-Output $output
}

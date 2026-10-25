$ErrorActionPreference = 'Stop'
try {
    $monitorRoot = $PSScriptRoot
    $program = Join-Path $monitorRoot 'build/SwitchMonitor.exe'
    $source = Get-ChildItem -LiteralPath (Join-Path $monitorRoot 'src') -Filter '*.cs' | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if (-not (Test-Path -LiteralPath $program) -or $source.LastWriteTimeUtc -gt (Get-Item -LiteralPath $program).LastWriteTimeUtc) {
        & (Join-Path $monitorRoot 'build.ps1')
    }
    Start-Process -FilePath $program -ArgumentList @('--root', ('"' + $monitorRoot + '"')) -WorkingDirectory $monitorRoot -WindowStyle Hidden
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    Read-Host 'Press Enter to close'
    exit 1
}

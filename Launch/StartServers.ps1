$ErrorActionPreference = 'Stop'
$server = Join-Path $PSScriptRoot 'MultiThreadServer.exe'
$configPath = Join-Path $PSScriptRoot 'Server.ini'
$configText = [IO.File]::ReadAllText($configPath)
$match = [regex]::Match($configText, '(?m)^ServerFile=(.+)\r?$')
if (-not $match.Success) { throw 'ServerFile is missing in Server.ini' }
$serverCsv = Join-Path $PSScriptRoot $match.Groups[1].Value.Trim()
$servers = @(Import-Csv -LiteralPath $serverCsv -Encoding UTF8)
# 하나라도 잘못된 설정이면 창을 여러 개 띄우기 전에 중단한다.
& $server --config-check World --config $configPath
if ($LASTEXITCODE -ne 0) { throw 'Server configuration check failed' }
$ids = @('World') + @($servers | ForEach-Object { $_.Index })
foreach ($id in $ids) {
    Start-Process -FilePath $server -WorkingDirectory $PSScriptRoot -ArgumentList @('--server', $id, '--config', ('"' + $configPath + '"')) -WindowStyle Normal
}
Write-Output "Started $($servers.Count) game servers and one World server."

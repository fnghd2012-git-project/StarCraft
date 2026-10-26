param([Parameter(Mandatory = $true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'

# 모든 게임 서버와 World가 같은 입장권 키를 사용한다. 키는 로컬 생성하고 Git에는 포함하지 않는다.
$keyPath = Join-Path $PSScriptRoot 'Cluster.local.ini'
if (-not (Test-Path -LiteralPath $keyPath)) {
    $bytes = New-Object byte[] 32
    $random = [Security.Cryptography.RandomNumberGenerator]::Create()
    try { $random.GetBytes($bytes) } finally { $random.Dispose() }
    $key = [BitConverter]::ToString($bytes).Replace('-', '').ToLowerInvariant()
    # Debug/Release를 동시에 처음 빌드해도 한 쪽이 만든 키를 덮어쓰지 않는다.
    try {
        $file = [IO.File]::Open($keyPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try {
            $text = [Text.Encoding]::UTF8.GetBytes("[Cluster]`r`nKey=$key`r`n")
            $file.Write($text, 0, $text.Length)
        } finally { $file.Dispose() }
    } catch [IO.IOException] {
        if (-not (Test-Path -LiteralPath $keyPath)) { throw }
    }
}
New-Item -ItemType Directory -Path (Join-Path $OutputDirectory 'Data') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Server.ini') -Destination $OutputDirectory -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ThirdPartyNotices.txt') -Destination $OutputDirectory -Force
# 동시 최초 빌드에서 다른 프로세스의 키 기록이 끝나기 전이면 잠깐 기다린다.
for ($attempt = 0; $attempt -lt 20; ++$attempt) {
    try { Copy-Item -LiteralPath $keyPath -Destination $OutputDirectory -Force; break }
    catch { if ($attempt -eq 19) { throw }; Start-Sleep -Milliseconds 50 }
}
foreach ($name in @('GameServer.csv', 'Unit.csv', 'Skill.csv', 'StartPoint.csv', 'Map.ini')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Data\$name") -Destination (Join-Path $OutputDirectory 'Data') -Force
}
foreach ($name in @('StartServers.cmd', 'StartServers.ps1', 'StartDummy.cmd', 'StartReplay.cmd', 'StartMatch10.cmd', 'StartMatch.ps1', 'StartBattle1500.cmd', 'StartBattle.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "Launch\$name") -Destination $OutputDirectory -Force
}
# 리플레이 이미지도 함께 배포한다. 실행 시 인터넷에 접속하지 않는다.
$replayAssets = Join-Path $OutputDirectory 'Assets\Replay'
New-Item -ItemType Directory -Path $replayAssets -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'Assets\Replay') -File | Copy-Item -Destination $replayAssets -Force

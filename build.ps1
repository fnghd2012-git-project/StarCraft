param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$RunChecks,
    [switch]$RunDbCheck,
    [switch]$RunNetworkChecks
)

$ErrorActionPreference = 'Stop'
$projectDirectory = Join-Path $PSScriptRoot 'MultiThreadServer\MultiThreadServer'
$solutionPath = Join-Path $projectDirectory 'MultiThreadServer.sln'
$vswherePath = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath)) {
    throw 'Visual Studio Installer의 vswhere.exe를 찾을 수 없습니다.'
}

$msbuildPaths = @(& $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe')
if ($LASTEXITCODE -ne 0 -or $msbuildPaths.Count -eq 0) {
    throw 'MSBuild를 찾을 수 없습니다. Visual Studio C++ 개발 도구를 확인하세요.'
}

& (Join-Path $PSScriptRoot 'setup-protobuf.ps1') -Configuration $Configuration

& $msbuildPaths[0] $solutionPath /m /nologo /v:minimal "/p:Configuration=$Configuration" /p:Platform=x64
if ($LASTEXITCODE -ne 0) {
    throw "빌드 실패: exit code $LASTEXITCODE"
}

if ($RunChecks -or $RunDbCheck) {
    $binaryPath = Join-Path $projectDirectory "build\x64\$Configuration\bin\MultiThreadServer.exe"
    $checkArgument = if ($RunDbCheck) { '--db-check' } else { '--self-check' }
    & $binaryPath $checkArgument
    if ($LASTEXITCODE -ne 0) {
        throw "점검 실패 ($checkArgument): exit code $LASTEXITCODE"
    }
}

if ($RunNetworkChecks) {
    & (Join-Path $PSScriptRoot 'test-network.ps1') -Configuration $Configuration
}

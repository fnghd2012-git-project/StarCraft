param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
# 프로젝트 안에서만 의존성을 준비한다. 이미 실행 중인 Redis 서비스 설정은 바꾸지 않는다.
$root = Join-Path $PSScriptRoot '.deps'
New-Item -ItemType Directory -Path $root -Force | Out-Null
$archive = Join-Path $root 'hiredis-1.3.0.zip'
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -UseBasicParsing 'https://github.com/redis/hiredis/archive/refs/tags/v1.3.0.zip' -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne '3EABD167EF610D9D6F159E607F16F6E6E7785EC661ED19BABDFD35A7FA9EABE9') { throw 'hiredis checksum mismatch' }
$source = Join-Path $root 'hiredis-1.3.0'
if (-not (Test-Path -LiteralPath $source)) { Expand-Archive -LiteralPath $archive -DestinationPath $root }
$build = Join-Path $root 'hiredis-build'
$props = Join-Path $root "redis-$Configuration.props"
$library = Join-Path $build "$Configuration\$(if ($Configuration -eq 'Debug') {'hiredisd'} else {'hiredis'}).lib"
if (-not (Test-Path -LiteralPath $library)) {
    $vswhere = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    $cmake = @(& $vswhere -version '[16.0,17.0)' -products * -find '**\cmake.exe')[0]
    if (-not $cmake) { throw 'Visual Studio 2019 CMake component is required' }
    & $cmake -S $source -B $build -G 'Visual Studio 16 2019' -A x64 -DBUILD_SHARED_LIBS=OFF -DDISABLE_TESTS=ON -DENABLE_EXAMPLES=OFF
    if ($LASTEXITCODE -ne 0) { throw 'hiredis configure failed' }
    & $cmake --build $build --config $Configuration --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'hiredis build failed' }
}
$include = [Security.SecurityElement]::Escape($source)
$link = [Security.SecurityElement]::Escape($library)
[IO.File]::WriteAllText($props, "<Project xmlns=`"http://schemas.microsoft.com/developer/msbuild/2003`"><ItemDefinitionGroup><ClCompile><AdditionalIncludeDirectories>$include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories></ClCompile><Link><AdditionalDependencies>$link;crypt32.lib;%(AdditionalDependencies)</AdditionalDependencies></Link></ItemDefinitionGroup></Project>")
Write-Output "[OK] hiredis 1.3.0 / $Configuration ready"

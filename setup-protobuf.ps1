param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$dependencyRoot = Join-Path $PSScriptRoot '.deps'
New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null

function Get-SourceArchive {
    param([string]$Name, [string]$Url, [string]$Hash, [string]$Directory)
    $archive = Join-Path $dependencyRoot $Name
    if (-not (Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile ($archive + '.part')
        if ((Get-FileHash -LiteralPath ($archive + '.part') -Algorithm SHA256).Hash -ne $Hash) {
            throw "Archive checksum mismatch: $Name"
        }
        Move-Item -LiteralPath ($archive + '.part') -Destination $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $Hash) {
        throw "Cached archive checksum mismatch: $Name"
    }
    $destination = Join-Path $dependencyRoot $Directory
    if (-not (Test-Path -LiteralPath $destination)) {
        if ($Name -like 'protoc-*') {
            Expand-Archive -LiteralPath $archive -DestinationPath $destination
        }
        else {
            Expand-Archive -LiteralPath $archive -DestinationPath $dependencyRoot
        }
    }
}

# protoc와 C++ 런타임은 같은 릴리스로 고정한다. 시스템 PATH나 전역 설치는 바꾸지 않는다.
Get-SourceArchive 'protobuf-33.6.zip' 'https://github.com/protocolbuffers/protobuf/releases/download/v33.6/protobuf-33.6.zip' 'c2f3a6737008bbd117127b1e6673517249016afd483caf41848f83d99679540a' 'protobuf-33.6'
Get-SourceArchive 'protoc-33.6-win64.zip' 'https://github.com/protocolbuffers/protobuf/releases/download/v33.6/protoc-33.6-win64.zip' '96a83b8ee942f1046f079c43657cc09fd41f37a4e349ddb0e8ba5064c48bb5ff' 'protoc-33.6'
Get-SourceArchive 'abseil-20250512.1.zip' 'https://github.com/abseil/abseil-cpp/archive/refs/tags/20250512.1.zip' '65aef88da5bbdc7c59d3a87a3d320a5b7c5560397bd41ae55e46c0ea04e77ede' 'abseil-cpp-20250512.1'

$vswhere = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$cmakePaths = @(& $vswhere -version '[16.0,17.0)' -products * -find '**\cmake.exe')
if ($cmakePaths.Count -eq 0) { throw 'Visual Studio 2019 CMake component is required.' }
$cmake = $cmakePaths[0]
$source = Join-Path $dependencyRoot 'protobuf-33.6'
$build = Join-Path $dependencyRoot 'protobuf-33.6-build'
$abseil = Join-Path $dependencyRoot 'abseil-cpp-20250512.1'
$settings = Join-Path $dependencyRoot "protobuf-$Configuration.props"
$buildStamp = Join-Path $dependencyRoot "protobuf-$Configuration.ready"
$log = Join-Path $dependencyRoot "protobuf-$Configuration.log"
$buildSignature = 'protobuf=33.6;abseil=20250512.1;toolset=v142;platform=x64;standard=C++20;runtime=MD;utf8=1;exceptions=EHsc;settings=4'

function Invoke-CMake {
    param([string[]]$Arguments)
    # PowerShell 5에서는 네이티브 도구의 stderr도 오류 객체가 된다. 성공 여부는 종료 코드로 판단한다.
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $cmake @Arguments *>> $log
        $exitCode = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $savedPreference }
    if ($exitCode -ne 0) {
        Get-Content -LiteralPath $log -Tail 35
        throw "Protobuf CMake failed: $exitCode"
    }
}

# 서버의 /MDd, /MD와 맞춘 정적 라이브러리를 만든다. 최초 준비 후에는 결과를 재사용한다.
if (-not (Test-Path -LiteralPath $buildStamp) -or -not (Test-Path -LiteralPath $settings) -or
    [IO.File]::ReadAllText($buildStamp) -ne $buildSignature) {
    [IO.File]::WriteAllText($log, '')
    Invoke-CMake @('-S', $source, '-B', $build, '-G', 'Visual Studio 16 2019', '-A', 'x64', '-DCMAKE_CXX_STANDARD=20', '-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /W3 /GR /EHsc /utf-8', '-Dprotobuf_BUILD_TESTS=OFF', '-Dprotobuf_BUILD_SHARED_LIBS=OFF', '-Dprotobuf_MSVC_STATIC_RUNTIME=OFF', '-Dprotobuf_WITH_ZLIB=OFF', '-Dprotobuf_BUILD_PROTOC_BINARIES=OFF', '-Dprotobuf_BUILD_LIBPROTOC=OFF', '-Dprotobuf_BUILD_LIBUPB=OFF', '-Dprotobuf_INSTALL=OFF', '-Dutf8_range_ENABLE_INSTALL=OFF', '-DABSL_ENABLE_INSTALL=OFF', "-DFETCHCONTENT_SOURCE_DIR_ABSL=$abseil")
    Invoke-CMake @('--build', $build, '--config', $Configuration, '--target', 'libprotobuf-lite', '--parallel', '4')

    # lite 런타임과 해당 런타임이 사용하는 Abseil/UTF-8 라이브러리만 링크한다.
    $libraries = @(Get-ChildItem -LiteralPath $build -Recurse -Filter '*.lib' |
        Where-Object { $_.Directory.Name -eq $Configuration -and $_.Name -match '^(libprotobuf-lite|absl_|(lib)?utf8_)' } |
        Sort-Object FullName | ForEach-Object { $_.FullName })
    if ($libraries.Count -eq 0 -or -not ($libraries | Where-Object { $_ -match 'libprotobuf-lite' })) {
        throw 'Protobuf runtime library is missing.'
    }
    $includeDirectories = [Security.SecurityElement]::Escape((Join-Path $source 'src') + ';' + $abseil + ';' + (Join-Path $source 'third_party\utf8_range'))
    $linkLibraries = [Security.SecurityElement]::Escape(($libraries -join ';'))
    $xml = '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemDefinitionGroup><ClCompile><AdditionalIncludeDirectories>' + $includeDirectories + ';%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories></ClCompile><Link><AdditionalDependencies>' + $linkLibraries + ';%(AdditionalDependencies)</AdditionalDependencies></Link></ItemDefinitionGroup></Project>'
    [IO.File]::WriteAllText($settings, $xml)
    [IO.File]::WriteAllText($buildStamp, $buildSignature)
}

$protoc = Join-Path $dependencyRoot 'protoc-33.6\bin\protoc.exe'
$version = & $protoc --version
if ($LASTEXITCODE -ne 0 -or $version -ne 'libprotoc 33.6') { throw "Unexpected protoc version: $version" }
Write-Output "[OK] Protobuf 33.6 / $Configuration ready."

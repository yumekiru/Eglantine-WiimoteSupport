param(
    [string]$BuildDirectory = 'build-wiimote',
    [string]$Destination = 'Play'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$buildRoot = if ([IO.Path]::IsPathRooted($BuildDirectory)) { [IO.Path]::GetFullPath($BuildDirectory) } else { Join-Path $projectRoot $BuildDirectory }
$playRoot = if ([IO.Path]::IsPathRooted($Destination)) { [IO.Path]::GetFullPath($Destination) } else { Join-Path $projectRoot $Destination }
$configureArgs = @('-S', $projectRoot, '-B', $buildRoot, '-G', 'Visual Studio 17 2022',
    '-A', 'x64', '-DENABLE_WERROR=OFF', '-DYUZU_TESTS=OFF',
    '-DENABLE_QT_TRANSLATION=OFF', '-DWIIMOTE_BUILD_DIAGNOSTIC=ON')

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    $installations = & $vswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $runtimeLibraries = foreach ($installation in $installations) {
        Get-ChildItem -LiteralPath (Join-Path $installation 'VC\Tools\MSVC') -Directory |
            ForEach-Object { Join-Path $_.FullName 'lib\x64\msvcprt.lib' } |
            Where-Object { Test-Path -LiteralPath $_ }
    }
    $runtime = $runtimeLibraries | Sort-Object { [version]((Split-Path (Split-Path (Split-Path $_ -Parent) -Parent) -Parent) | Split-Path -Leaf) } -Descending | Select-Object -First 1
    if ($runtime) {
        $configureArgs += ('-DCMAKE_EXE_LINKER_FLAGS="' + $runtime.Replace('\', '/') + '" /NODEFAULTLIB:msvcprt.lib')
    }
}

& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
$previousCompilerOverrides = $env:_CL_
try {
    $env:_CL_ = ($previousCompilerOverrides + ' /wd4505 /wd5245 /wd4189').Trim()
    & cmake --build $buildRoot --config Release --target yuzu wiimote_diagnostic wiimote_protocol_test wiimote_motion_test --parallel 8
    if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
    & ctest --test-dir $buildRoot -C Release -R '^wiimote_(protocol|motion)$' --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Wii Remote decoder tests failed.' }
} finally {
    $env:_CL_ = $previousCompilerOverrides
}

New-Item -ItemType Directory -Path (Join-Path $playRoot 'user\config\input') -Force | Out-Null
foreach ($binary in 'lichii.exe', 'wiimote_diagnostic.exe', 'wiimote_protocol_test.exe') {
    Copy-Item -LiteralPath (Join-Path $buildRoot ('bin\Release\' + $binary)) -Destination $playRoot
}
$configuration = Join-Path $playRoot 'user\config\qt-config.ini'
if (!(Test-Path -LiteralPath $configuration)) {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'dist\wiimote\qt-config.ini') -Destination $configuration
}
foreach ($profile in Get-ChildItem -LiteralPath (Join-Path $projectRoot 'dist\wiimote\profiles') -Filter '*.ini') {
    $profileTarget = Join-Path $playRoot ('user\config\input\' + $profile.Name)
    if (!(Test-Path -LiteralPath $profileTarget)) {
        Copy-Item -LiteralPath $profile.FullName -Destination $profileTarget
    }
}
Write-Host "Portable build ready at $playRoot"

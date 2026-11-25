param(
 [Parameter(Mandatory=$true)][string]$ObsSdkRoot,
 [Parameter(Mandatory=$true)][string]$ObsDependenciesRoot,
 [Parameter(Mandatory=$true)][string]$ObsQtRoot,
 [string]$CMake='cmake',
 [string]$BuildDirectory=(Join-Path $PSScriptRoot '../build/release')
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
& $CMake -S $root -B $BuildDirectory -A x64 -DBUILD_TESTING=OFF -DNEXUS_BUILD_TEST_BENCH=OFF -DNEXUS_BUILD_OBS_PLUGIN=ON -DNEXUS_BUILD_EQ_UI=ON -DNEXUS_BUILD_VST3_CORE=ON -DNEXUS_BUILD_OBS_VST3=ON "-Dlibobs_DIR=$ObsSdkRoot/cmake" "-DCMAKE_PREFIX_PATH=$ObsSdkRoot;$ObsDependenciesRoot;$ObsQtRoot"
if($LASTEXITCODE){throw 'Configuration failed'}
& $CMake --build $BuildDirectory --config Release --target obs-vst3-nexus nexus_vst3_worker
if($LASTEXITCODE){throw 'Build failed'}
& $CMake --install $BuildDirectory --config Release --prefix "$BuildDirectory/staging"
if($LASTEXITCODE){throw 'Staging failed'}

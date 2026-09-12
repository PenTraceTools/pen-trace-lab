param(
    [ValidateSet('x64','ARM64')][string]$Architecture = 'x64',
    [string]$Generator = 'Visual Studio 17 2022'
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskBuild = Join-Path $taskRoot "build-$Architecture"
$taskPackage = Join-Path $taskRoot "out\PenTraceLab-$Architecture"
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'CMake is missing. See docs/BUILDING.md. This script does not install prerequisites.'
}
function Invoke-CheckedCMake {
    param([string[]]$Arguments)
    & cmake @Arguments
    if ($LASTEXITCODE -ne 0) { throw "CMake failed with exit code $LASTEXITCODE" }
}
Invoke-CheckedCMake -Arguments @('-S',$taskRoot,'-B',$taskBuild,'-G',$Generator,'-A',$Architecture,'-DBUILD_TESTING=ON')
Invoke-CheckedCMake -Arguments @('--build',$taskBuild,'--config','Release','--parallel')
# An x64 PC cannot natively execute an ARM64 test binary. Run host core tests too.
$taskHostArch = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
if ($Architecture -eq 'ARM64' -and $taskHostArch -ne 'Arm64') {
    $taskHostBuild = Join-Path $taskRoot 'build-host-tests'
    Invoke-CheckedCMake -Arguments @('-S',$taskRoot,'-B',$taskHostBuild,'-G',$Generator,'-A','x64','-DBUILD_TESTING=ON')
    Invoke-CheckedCMake -Arguments @('--build',$taskHostBuild,'--config','Release','--target','pentrace_tests','--parallel')
    & ctest --test-dir $taskHostBuild -C Release --output-on-failure
} else {
    & ctest --test-dir $taskBuild -C Release --output-on-failure
}
if ($LASTEXITCODE -ne 0) { throw 'Core tests failed. No portable package was produced by this run.' }
Invoke-CheckedCMake -Arguments @('--install',$taskBuild,'--config','Release','--prefix',$taskPackage)
$taskZip = Join-Path $taskRoot "out\PenTraceLab-$Architecture.zip"
Compress-Archive -Path (Join-Path $taskPackage '*') -DestinationPath $taskZip -Force
Write-Host "Portable app: $taskPackage\PenTraceLab.exe"
Write-Host "Transfer package: $taskZip"
Write-Host 'Core tests passed. Physical pen/touch acceptance still must be performed on the target device.'

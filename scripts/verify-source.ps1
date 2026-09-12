# Source-only checks: this script never invokes a compiler, installer or app.
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskFiles = @(
    'CMakeLists.txt','README.md','LICENSE','src/core.hpp','src/core.cpp','src/history.hpp','src/analyze.cpp',
    'src/trace_io.hpp','src/trace_io.cpp','src/compare.hpp','src/compare.cpp','src/win_input.hpp','src/win_input.cpp',
    'src/renderer.hpp','src/renderer.cpp','src/winmain.cpp','tests/core_tests.cpp',
    'resources/app.rc','resources/app.manifest','docs/BUILDING.md','docs/TESTING.md',
    'docs/DESIGN.md','docs/RECORDING_FORMAT.md','docs/INDEPENDENT_TRACKING.md','.github/workflows/ci.yml'
)
foreach ($taskFile in $taskFiles) {
    $taskPath = Join-Path $taskRoot $taskFile
    if (-not (Test-Path -LiteralPath $taskPath -PathType Leaf)) { throw "Missing: $taskFile" }
    if ((Get-Item -LiteralPath $taskPath).Length -eq 0) { throw "Empty file: $taskFile" }
}
[xml]$taskManifest = Get-Content -LiteralPath (Join-Path $taskRoot 'resources/app.manifest') -Raw
if ($taskManifest.assembly.manifestVersion -ne '1.0') { throw 'Invalid application manifest.' }
$taskScripts = Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1'
foreach ($taskScript in $taskScripts) {
    $taskTokens = $null
    $taskErrors = $null
    $null = [System.Management.Automation.Language.Parser]::ParseFile($taskScript.FullName,[ref]$taskTokens,[ref]$taskErrors)
    if ($taskErrors.Count) { throw ($taskErrors | Out-String) }
}
$taskSources = Get-ChildItem -LiteralPath (Join-Path $taskRoot 'src') -File
foreach ($taskSource in $taskSources) {
    $taskText = Get-Content -LiteralPath $taskSource.FullName -Raw
    foreach ($taskMatch in [regex]::Matches($taskText,'#include\s+"([^"]+)"')) {
        $taskInclude = Join-Path $taskSource.DirectoryName $taskMatch.Groups[1].Value
        if (-not (Test-Path -LiteralPath $taskInclude)) { throw "Missing local include: $taskInclude" }
    }
    if ($taskText -match '(?m)[ \t]+$') { throw "Trailing whitespace: $($taskSource.Name)" }
}
Write-Host 'PASS: source inventory, local includes, manifest XML and PowerShell script syntax.'
Write-Host 'These are NOT C++ compilation, unit-test execution, GUI checks or pen-device validation.'

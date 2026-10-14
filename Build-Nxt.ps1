[CmdletBinding()]
param(
    [ValidateRange(1,64)][int]$Jobs = 4,
    [ValidateSet('Release','Debug','RelWithDebInfo')][string]$Configuration = 'Release',
    [switch]$TestsOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
$CmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$Cmake = if ($CmakeCommand) { $CmakeCommand.Source } else { 'C:\Program Files\CMake\bin\cmake.exe' }
if (-not (Test-Path $Cmake)) { throw 'CMake 3.26 or newer is required. Add cmake.exe to PATH.' }
function Invoke-Checked([string[]]$Arguments) {
    & $Cmake @Arguments
    if ($LASTEXITCODE -ne 0) { throw "CMake failed with exit code $LASTEXITCODE. See the first error above." }
}
Push-Location $Root
try {
    if ($TestsOnly) {
        $Build = Join-Path $Root 'build/nxt-tests'
        Invoke-Checked -Arguments @('-S','tests/nxt','-B',$Build,'-G','Visual Studio 17 2022','-T','v143','-A','x64')
        Invoke-Checked -Arguments @('--build',$Build,'--config',$Configuration,'--parallel',"$Jobs")
        & (Join-Path (Split-Path $Cmake) 'ctest.exe') --test-dir $Build -C $Configuration --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'NXT headless tests failed.' }
        return
    }
    foreach ($font in @('PressStart2P-Regular.ttf','Fipps-Regular.otf','Inconsolata-Regular.ttf','Montserrat-Regular.ttf','NotoSansJP-Regular.ttf')) {
        if (-not (Test-Path (Join-Path $Root "soh/assets/custom/fonts/$font"))) {
            throw "An unchanged baseline font asset is missing: $font. Apply the overlay to your original source, or run Restore-NxtBaselineAssets.ps1 -BaselineDirectory 'C:\path\to\original-source'."
        }
    }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'Git is required for the upstream dependency downloads.' }
    foreach ($path in @('libultraship/CMakeLists.txt','torch/CMakeLists.txt')) {
        if (-not (Test-Path (Join-Path $Root $path))) { throw "Missing populated baseline dependency: $path. Use the complete original source, not an empty submodule folder." }
    }
    # Separate build directory: never reuse a cache generated against a different source tree.
    $Build = Join-Path $Root 'build/nxt-win64'
    Invoke-Checked -Arguments @('-S',$Root,'-B',$Build,'-G','Visual Studio 17 2022','-T','v143','-A','x64')
    Invoke-Checked -Arguments @('--build',$Build,'--config',$Configuration,'--target','soh','GenerateSohOtr','--parallel',"$Jobs")
    Write-Host 'NXT compilation completed. Executable locations:'
    Get-ChildItem $Build -Recurse -Filter soh.exe | ForEach-Object { Write-Host $_.FullName }
    Write-Host 'Use your own compatible oot.o2r / oot-mq.o2r assets. Read README_NXT.md before loading saves.'
} finally { Pop-Location }

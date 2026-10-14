[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BaselineDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Names = @('PressStart2P-Regular.ttf','Fipps-Regular.otf','Inconsolata-Regular.ttf','Montserrat-Regular.ttf','NotoSansJP-Regular.ttf')
$Source = Join-Path (Resolve-Path $BaselineDirectory).Path 'soh/assets/custom/fonts'
$Destination = Join-Path $PSScriptRoot 'soh/assets/custom/fonts'
foreach ($Name in $Names) {
    if (-not (Test-Path (Join-Path $Source $Name))) { throw "Baseline asset not found: $Source/$Name. Point to the extracted source root, not the RAR file." }
}
New-Item -ItemType Directory -Force $Destination | Out-Null
foreach ($Name in $Names) {
    $Target = Join-Path $Destination $Name
    if (-not (Test-Path $Target)) { Copy-Item -LiteralPath (Join-Path $Source $Name) -Destination $Target }
}
Write-Host 'Missing unchanged assets were copied from your local baseline. Existing destination assets were left unchanged.'

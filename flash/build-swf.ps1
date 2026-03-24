Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$ffdec = "D:\Dev\Skyrim\Tools\ffdec_25.0.0_nightly3419\ffdec.bat"
$templateSwf = Join-Path $PSScriptRoot "AlchemyRecipeViewVR.base.swf"
$scriptsRoot = $PSScriptRoot
$outputSwf = Join-Path $projectRoot "deploy\Interface\AlchemyRecipeViewVR.swf"
$tmpDir = Join-Path $projectRoot "_tmp\swf_build"
$tmpSwf = Join-Path $tmpDir "AlchemyRecipeViewVR.swf"

if (-not (Test-Path $ffdec)) {
	throw "FFDec not found: $ffdec"
}

if (-not (Test-Path $templateSwf)) {
	throw "Template SWF not found: $templateSwf"
}

if (-not (Test-Path (Join-Path $scriptsRoot "scripts"))) {
	throw "Script source folder not found: $(Join-Path $scriptsRoot 'scripts')"
}

New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null

& $ffdec -importScript $templateSwf $tmpSwf $scriptsRoot
if ($LASTEXITCODE -ne 0) {
	throw "FFDec importScript failed with exit code $LASTEXITCODE"
}

Copy-Item -Force $tmpSwf $outputSwf
Write-Host "Built SWF -> $outputSwf"

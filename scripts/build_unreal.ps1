param(
 [Parameter(Mandatory=$true)][string]$EngineRoot,
 [switch]$Automation,
 [switch]$GenerateAssets,
 [string]$CacheRoot,
 [string]$OutputRoot
)
$ErrorActionPreference='Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskProject = Join-Path $taskRoot 'AegisArena.uproject'
$taskBuild = Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat'
$taskEditor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
if (!(Test-Path -LiteralPath $taskBuild) -or !(Test-Path -LiteralPath $taskEditor)) { throw 'EngineRoot does not contain an installed Unreal C++ build toolchain.' }
$taskVersion = Get-Content -LiteralPath (Join-Path $EngineRoot 'Engine/Build/Build.version') -Raw | ConvertFrom-Json
if ($taskVersion.MajorVersion -ne 5 -or $taskVersion.MinorVersion -ne 8) { throw 'This adapter targets UE 5.8. Review compatibility before compiling another minor version.' }
$taskPreviousTemp = $env:TEMP
$taskPreviousTmp = $env:TMP
$taskPreviousDdc = [Environment]::GetEnvironmentVariable('UE-LocalDataCachePath','Process')
$taskBuildArgs = @('AegisArenaEditor','Win64','Development',$taskProject,'-WaitMutex','-NoHotReloadFromIDE','-MaxParallelActions=2')
try {
 if ($CacheRoot) {
  New-Item -ItemType Directory -Path $CacheRoot -Force | Out-Null
  $taskTemp = Join-Path $CacheRoot 'Temp'
  New-Item -ItemType Directory -Path $taskTemp -Force | Out-Null
  $env:TEMP = $taskTemp
  $env:TMP = $taskTemp
  [Environment]::SetEnvironmentVariable('UE-LocalDataCachePath', (Join-Path $CacheRoot 'DerivedDataCache'), 'Process')
  $taskBuildArgs += "-UBARootDir=$(Join-Path $CacheRoot 'UBA')"
 }
 if (!$OutputRoot) { $OutputRoot = if ($CacheRoot) { Join-Path $CacheRoot 'Reports' } else { Join-Path $taskRoot 'outputs/unreal' } }
 $taskOutput = Join-Path $OutputRoot ((Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N'))
 New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
 & $taskBuild @taskBuildArgs 2>&1 | Tee-Object -FilePath (Join-Path $taskOutput 'build.log')
 if ($LASTEXITCODE -ne 0) { throw "Unreal compilation failed. Inspect $taskOutput/build.log" }
 if ($GenerateAssets) {
  $taskScript = Join-Path $PSScriptRoot 'unreal/create_arena.py'
  & $taskEditor $taskProject -run=pythonscript "-script=$taskScript" -unattended -nop4 -NullRHI -stdout -FullStdOutLogOutput "-abslog=$taskOutput/assets-engine.log" 2>&1 | Tee-Object -FilePath (Join-Path $taskOutput 'assets.log')
  if ($LASTEXITCODE -ne 0) { throw "Unreal asset generation failed. Inspect $taskOutput/assets.log" }
  foreach ($taskAsset in @('Maps/AegisArena.umap','Maps/AegisFunctional.umap','AI/BB_Aegis.uasset','AI/BT_Aegis.uasset','AI/EQS_Cover.uasset','AI/EQS_Attack.uasset','AI/EQS_Retreat.uasset')) {
   if (!(Test-Path -LiteralPath (Join-Path $taskRoot "Content/Aegis/$taskAsset"))) { throw "Asset generation did not produce $taskAsset" }
  }
 }
 if ($Automation) {
  & $taskEditor $taskProject -unattended -nop4 -NullRHI '-ExecCmds=Automation RunTests Aegis.Core' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$taskOutput/automation" -stdout -FullStdOutLogOutput "-abslog=$taskOutput/automation-engine.log" 2>&1 | Tee-Object -FilePath (Join-Path $taskOutput 'automation.log')
  if ($LASTEXITCODE -ne 0) { throw 'Unreal Automation process failed.' }
  $taskReportFile = Join-Path $taskOutput 'automation/index.json'
  if (!(Test-Path -LiteralPath $taskReportFile)) { throw 'No new Automation report; process exit is not test success.' }
  $taskReport = Get-Content -LiteralPath $taskReportFile -Raw | ConvertFrom-Json
  if ($null -eq $taskReport.succeeded -or $null -eq $taskReport.failed) { throw 'Unknown Automation report schema. Inspect manually.' }
  if ($taskReport.succeeded -lt 5 -or $taskReport.failed -ne 0 -or $taskReport.notRun -gt 0 -or $taskReport.inProcess -gt 0) { throw 'Expected at least five completed passing Aegis core tests.' }
 }
 Write-Output "Unreal build completed. New logs and requested test reports: $taskOutput"
} finally {
 $env:TEMP = $taskPreviousTemp
 $env:TMP = $taskPreviousTmp
 [Environment]::SetEnvironmentVariable('UE-LocalDataCachePath', $taskPreviousDdc, 'Process')
}

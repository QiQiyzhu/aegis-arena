param(
 [Parameter(Mandatory=$true)][string]$EngineRoot,
 [Parameter(Mandatory=$true)][string]$OutputRoot,
 [Parameter(Mandatory=$true)][string]$CacheRoot,
 [ValidateSet('Development','Shipping')][string]$Configuration='Development'
)
$ErrorActionPreference='Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskProject = Join-Path $taskRoot 'AegisArena.uproject'
$taskUat = Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat'
if (!(Test-Path -LiteralPath $taskUat)) { throw 'Installed UE RunUAT.bat missing.' }
if (!(Test-Path -LiteralPath (Join-Path $taskRoot 'Content/Aegis/Maps/AegisArena.umap'))) { throw 'Generate and validate the native arena first.' }
if (Test-Path -LiteralPath $OutputRoot) { throw 'Use a new output directory; stale packages cannot satisfy acceptance.' }
New-Item -ItemType Directory -Path $OutputRoot -ErrorAction Stop | Out-Null
$taskOutput = (Resolve-Path -LiteralPath $OutputRoot).Path
$taskPreviousTemp = $env:TEMP
$taskPreviousTmp = $env:TMP
$taskPreviousDdc = [Environment]::GetEnvironmentVariable('UE-LocalDataCachePath','Process')
try {
 New-Item -ItemType Directory -Path (Join-Path $CacheRoot 'Temp') -Force | Out-Null
 $env:TEMP = (Resolve-Path -LiteralPath (Join-Path $CacheRoot 'Temp')).Path
 $env:TMP = $env:TEMP
 [Environment]::SetEnvironmentVariable('UE-LocalDataCachePath', (Join-Path $CacheRoot 'DerivedDataCache'), 'Process')
 $taskArguments = @('BuildCookRun',"-project=$taskProject",'-installed','-nop4','-unattended',
  '-nocompileeditor','-platform=Win64',"-clientconfig=$Configuration",'-target=AegisArena',
  '-build','-cook','-stage','-pak','-archive','-prereqs','-utf8output',
  '-AdditionalCookerOptions=-corelimit=4',
  '-map=/Game/Aegis/Maps/AegisArena',"-archivedirectory=$taskOutput/package",
  "-ubtargs=-MaxParallelActions=2 -UBARootDir=$(Join-Path $CacheRoot 'UBA')")
 & $taskUat @taskArguments 2>&1 | Tee-Object -FilePath (Join-Path $taskOutput 'package.log')
 if ($LASTEXITCODE -ne 0) { throw "Unreal cook/package failed: $taskOutput/package.log" }
 $taskLauncher = Join-Path $taskOutput 'package/Windows/AegisArena.exe'
 if (!(Test-Path -LiteralPath $taskLauncher)) { throw 'UAT returned success but packaged launcher is missing.' }
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'PLAY-Aegis.cmd') -Destination (Join-Path $taskOutput 'package/Windows/PLAY-Aegis.cmd')
 Copy-Item -LiteralPath (Join-Path $taskRoot 'docs/portfolio-v2.5-play.md') -Destination (Join-Path $taskOutput 'package/Windows/PLAY.md')
 if (Test-Path -LiteralPath (Join-Path $taskRoot 'LICENSE')) { Copy-Item -LiteralPath (Join-Path $taskRoot 'LICENSE') -Destination (Join-Path $taskOutput 'package/Windows/LICENSE.txt') }
 Copy-Item -LiteralPath (Join-Path $taskRoot 'ThirdPartyNotices') -Destination (Join-Path $taskOutput 'package/Windows/ThirdPartyNotices') -Recurse
 Write-Output "Unreal $Configuration package built: $taskLauncher"
 Write-Output 'Build/cook/stage success is separate from launch, input and Shipping gate verification.'
} finally {
 $env:TEMP = $taskPreviousTemp
 $env:TMP = $taskPreviousTmp
 [Environment]::SetEnvironmentVariable('UE-LocalDataCachePath', $taskPreviousDdc, 'Process')
}

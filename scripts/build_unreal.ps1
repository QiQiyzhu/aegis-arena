param([Parameter(Mandatory=$true)][string]$EngineRoot, [switch]$Automation)
$ErrorActionPreference='Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskProject = Join-Path $taskRoot 'AegisArena.uproject'
$taskBuild = Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat'
$taskEditor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
if (!(Test-Path -LiteralPath $taskBuild) -or !(Test-Path -LiteralPath $taskEditor)) { throw 'EngineRoot does not contain an installed Unreal C++ build toolchain.' }
$taskVersionFile = Join-Path $EngineRoot 'Engine/Build/Build.version'
$taskVersion = Get-Content -LiteralPath $taskVersionFile -Raw | ConvertFrom-Json
if ($taskVersion.MajorVersion -ne 5 -or $taskVersion.MinorVersion -ne 7) { throw 'This adapter targets UE 5.7. Review and update compatibility before compiling another minor version.' }
$taskOutput = Join-Path $taskRoot 'outputs/unreal'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
& $taskBuild AegisArenaEditor Win64 Development $taskProject -WaitMutex -NoHotReloadFromIDE 2>&1 | Tee-Object -FilePath (Join-Path $taskOutput 'build.log')
if ($LASTEXITCODE -ne 0) { throw 'Unreal compilation failed; inspect outputs/unreal/build.log.' }
if ($Automation) {
 & $taskEditor $taskProject -unattended -nop4 -NullRHI '-ExecCmds=Automation RunTests Aegis.Core' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$taskOutput/automation" '-log' 2>&1 | Tee-Object -FilePath (Join-Path $taskOutput 'automation.log')
 if ($LASTEXITCODE -ne 0) { throw 'Unreal Automation failed.' }
 if (!(Test-Path -LiteralPath (Join-Path $taskOutput 'automation/index.json'))) { throw 'No Automation report; do not treat process exit as test success.' }
 $taskReport = Get-Content -LiteralPath (Join-Path $taskOutput 'automation/index.json') -Raw | ConvertFrom-Json
 if ($null -eq $taskReport.succeeded -or $null -eq $taskReport.failed) { throw 'Unknown Automation report schema; inspect report manually before claiming success.' }
 if ($taskReport.succeeded -lt 5 -or $taskReport.failed -ne 0 -or $taskReport.notRun -gt 0 -or $taskReport.inProcess -gt 0) { throw 'Expected five completed passing Aegis core tests; Automation report did not satisfy that contract.' }
}
Write-Output 'Unreal build finished. Inspect generated Automation results before publishing evidence.'

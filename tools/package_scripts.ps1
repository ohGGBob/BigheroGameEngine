# Build a framework-dependent script package; the target machine needs .NET runtime, not SDK.
param(
    [Parameter(Mandatory=$true)][string]$Project,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$AssemblyName = "MyGame"
)
$ErrorActionPreference = 'Stop'
if ($AssemblyName -notmatch '^[A-Za-z0-9_.-]+$') { throw 'Invalid assembly name' }
$taskProject = (Resolve-Path -LiteralPath $Project).Path
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $taskOutput) { throw 'Choose a new output directory to avoid stale artifacts' }
$taskSourceRoot = Split-Path $PSScriptRoot -Parent
$taskRuntimeProject = Join-Path $taskSourceRoot 'scriptcore/BigHero.Runtime/BigHero.Runtime.csproj'
New-Item -ItemType Directory -Path $taskOutput | Out-Null
(Get-Process -Id $PID).PriorityClass = 'BelowNormal'
& dotnet publish $taskRuntimeProject -c Release -o (Join-Path $taskOutput 'runtime') --no-self-contained -m:1 -p:UseSharedCompilation=false --nologo
if ($LASTEXITCODE -ne 0) { throw 'Runtime publish failed; incomplete output has no package marker' }
& dotnet publish $taskProject -c Release -o $taskOutput --no-self-contained -m:1 -p:UseSharedCompilation=false --nologo
if ($LASTEXITCODE -ne 0) { throw 'User script publish failed; incomplete output has no package marker' }
if (-not (Test-Path -LiteralPath (Join-Path $taskOutput ($AssemblyName + '.dll')))) { throw 'Assembly not found; check AssemblyName' }
# Only a complete output is marked as a package. Keep deps.json and transitive published dependencies.
@{version=1; assembly=($AssemblyName + '.dll')} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskOutput 'script-package.json') -Encoding utf8
Write-Output "Script package ready: $taskOutput"

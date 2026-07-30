[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PluginSource,
    [Parameter(Mandatory = $true)]
    [ValidateSet('5.6', '5.7', '5.8')]
    [string]$EngineVersion,
    [switch]$GenerateProjectFiles,
    [switch]$Build
)

$ErrorActionPreference = 'Stop'
$sampleRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourceRoot = (Resolve-Path $PluginSource).Path
$sourceDescriptor = Join-Path $sourceRoot 'EditorActorTagDisplay.uplugin'
$targetRoot = Join-Path $sampleRoot 'Plugins\EditorActorTagDisplay'
$engineRoot = "C:\Program Files\Epic Games\UE_$EngineVersion"

function Get-RelativePath([string]$BasePath, [string]$ChildPath) {
    $baseUri = [System.Uri](([System.IO.Path]::GetFullPath($BasePath).TrimEnd('\')) + '\')
    $childUri = [System.Uri]([System.IO.Path]::GetFullPath($ChildPath))
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($childUri).ToString()).Replace('/', '\')
}

if (-not (Test-Path -LiteralPath $sourceDescriptor)) {
    throw "Plugin descriptor not found: $sourceDescriptor"
}
if (-not (Test-Path -LiteralPath (Join-Path $engineRoot 'Engine\Build\BatchFiles\Build.bat'))) {
    throw "Unreal Engine $EngineVersion was not found at $engineRoot"
}

$descriptor = Get-Content -LiteralPath $sourceDescriptor -Raw | ConvertFrom-Json
if ($descriptor.FriendlyName -ne 'Actor Metadata Overlay') {
    throw 'The supplied plugin is not Actor Metadata Overlay.'
}

if (Test-Path -LiteralPath $targetRoot) {
    $targetRootResolved = (Resolve-Path $targetRoot).Path
    $allowedTarget = [System.IO.Path]::GetFullPath($targetRootResolved).TrimEnd('\')
    if (-not $allowedTarget.StartsWith([System.IO.Path]::GetFullPath($sampleRoot).TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Resolved plugin target escaped the sample root: $allowedTarget"
    }
}
New-Item -ItemType Directory -Path $targetRoot -Force | Out-Null

$excludedDirectories = @('Binaries', 'Intermediate', '.git', 'Marketing', '.verification')
foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File -Force) {
    $relative = (Get-RelativePath $sourceRoot $file.FullName).Replace('\', '/')
    $parts = $relative.Split('/')
    if ($parts | Where-Object { $excludedDirectories -contains $_ }) {
        continue
    }
    $destination = Join-Path $targetRoot ($relative.Replace('/', '\'))
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
}

$trackedCheck = git -C $sampleRoot status --short --untracked-files=all -- 'Plugins/EditorActorTagDisplay'
if ($trackedCheck) {
    throw 'The paid plugin copy is not ignored by the sample repository.'
}

if ($GenerateProjectFiles) {
    $projectFilesTool = Join-Path $engineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    $projectPath = Join-Path $sampleRoot 'ActorMetadataOverlaySample.uproject'
    $logPath = Join-Path $sampleRoot ".verification\setup-projectfiles-$EngineVersion.log"
    New-Item -ItemType Directory -Path (Split-Path -Parent $logPath) -Force | Out-Null
    $process = Start-Process -FilePath $projectFilesTool -ArgumentList @($projectPath, '-unattended', '-nop4', '-nosplash', '-NoSound', '-projectfiles', '-log') -Wait -PassThru -WindowStyle Hidden -RedirectStandardOutput $logPath -RedirectStandardError $logPath
    if ($process.ExitCode -ne 0) {
        throw "Project file generation failed with exit code $($process.ExitCode). See $logPath"
    }
}

if ($Build) {
    $buildTool = Join-Path $engineRoot 'Engine\Build\BatchFiles\Build.bat'
    $projectPath = Join-Path $sampleRoot 'ActorMetadataOverlaySample.uproject'
    $logPath = Join-Path $sampleRoot ".verification\setup-build-$EngineVersion.log"
    New-Item -ItemType Directory -Path (Split-Path -Parent $logPath) -Force | Out-Null
    $process = Start-Process -FilePath $buildTool -ArgumentList @('ActorMetadataOverlaySampleEditor', 'Win64', 'Development', $projectPath, '-WaitMutex', '-NoHotReloadFromIDE') -Wait -PassThru -WindowStyle Hidden -RedirectStandardOutput $logPath -RedirectStandardError $logPath
    if ($process.ExitCode -ne 0) {
        throw "Sample build failed with exit code $($process.ExitCode). See $logPath"
    }
}

[pscustomobject]@{
    PluginSource = $sourceRoot
    EngineVersion = $EngineVersion
    Target = $targetRoot
    PaidPluginTracked = [bool]$trackedCheck
    BuildRequested = [bool]$Build
} | ConvertTo-Json -Depth 4

[CmdletBinding()]
param(
    [ValidateSet('5.6', '5.7', '5.8')]
    [string]$EngineVersion,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$sampleRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$required = @(
    'ActorMetadataOverlaySample.uproject',
    'Config/DefaultEditor.ini',
    'Config/DefaultEditorPerProjectUserSettings.ini',
    'Config/DefaultEngine.ini',
    'Config/DefaultGame.ini',
    'Config/Tags/ActorMetadataOverlaySampleTags.ini',
    'Demo/demo-spec.json',
    'Scripts/Build-DemoMap.py',
    'Scripts/Setup-Local.ps1',
    'Scripts/Sync-DemoToCaptureHost.ps1',
    'Scripts/Capture/Apply-DemoSpec.py',
    'Plugins/ActorMetadataOverlayDemoFixtures/ActorMetadataOverlayDemoFixtures.uplugin',
    'Plugins/ActorMetadataOverlayDemoFixtures/Source/ActorMetadataOverlayDemoFixtures/Public/ActorMetadataOverlayDemoActor.h',
    'Plugins/ActorMetadataOverlayDemoFixtures/Source/ActorMetadataOverlayDemoFixtures/Public/ActorMetadataOverlayDemoZone.h'
)
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $sampleRoot $_)) })
if ($missing.Count -gt 0) {
    throw "Missing required files: $($missing -join ', ')"
}

$spec = Get-Content -LiteralPath (Join-Path $sampleRoot 'Demo/demo-spec.json') -Raw | ConvertFrom-Json
if ($spec.schemaVersion -ne 1 -or $spec.actors.Count -ne 7 -or $spec.rules.Count -ne 2) {
    throw 'demo-spec.json does not contain the required schema, actor count, or rule count.'
}
if ($spec.map -ne '/Game/ActorMetadataOverlayDemo/Maps/ActorMetadataOverlayOverview') {
    throw 'demo-spec.json map path is incorrect.'
}

$forbidden = @(
    'Content/Python/init_unreal.py',
    'Content/DeepWaterStation',
    'DeepWaterStation',
    'Marketing'
)
$forbiddenPresent = @($forbidden | Where-Object { Test-Path -LiteralPath (Join-Path $sampleRoot $_) })
if ($forbiddenPresent.Count -gt 0) {
    throw "Forbidden sample paths exist: $($forbiddenPresent -join ', ')"
}

$engineConfigPath = Join-Path $sampleRoot 'Config/DefaultEngine.ini'
$engineConfigText = Get-Content -LiteralPath $engineConfigPath -Raw
$forbiddenEngineConfigPatterns = @(
    [pscustomobject]@{
        Name = 'Android File Server settings section'
        Pattern = '(?m)^\s*\[/Script/AndroidFileServerEditor\.AndroidFileServerRuntimeSettings\]\s*$'
    },
    [pscustomobject]@{
        Name = 'Android File Server security token'
        Pattern = '(?m)^\s*SecurityToken\s*='
    },
    [pscustomobject]@{
        Name = 'Android File Server network access'
        Pattern = '(?m)^\s*bAllowNetworkConnection\s*=\s*True\s*$'
    }
)
foreach ($check in $forbiddenEngineConfigPatterns) {
    if ($engineConfigText -match $check.Pattern) {
        throw "Forbidden $($check.Name) remains in Config/DefaultEngine.ini. Remove the Android File Server setting instead of blanking its token."
    }
}

$verifyScriptPath = (Resolve-Path -LiteralPath $PSCommandPath).Path
$textExtensions = @('.ini', '.json', '.md', '.ps1', '.py', '.txt', '.uplugin', '.uproject', '.cs', '.cpp', '.h', '.hpp')
$textFiles = @(Get-ChildItem -LiteralPath $sampleRoot -Recurse -File | Where-Object {
    $_.FullName -notmatch '\\(\.git|Binaries|Intermediate|Saved|DerivedDataCache|\.verification)\\' -and
    $_.FullName -ne $verifyScriptPath -and
    $_.Extension -in $textExtensions
})
$securityTokenMatches = @($textFiles | Select-String -Pattern 'SecurityToken\s*=')
if ($securityTokenMatches.Count -gt 0) {
    $locations = @($securityTokenMatches | ForEach-Object { "$($_.Path):$($_.LineNumber)" }) -join ', '
    throw "Forbidden SecurityToken= configuration remains in Sample files: $locations"
}

$paidPluginTracked = @()
if (Test-Path -LiteralPath (Join-Path $sampleRoot '.git')) {
    $paidPluginTracked = @(git -C $sampleRoot ls-files -- 'Plugins/EditorActorTagDisplay')
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to inspect tracked paid-plugin files.'
    }
}
if ($paidPluginTracked.Count -gt 0) {
    throw 'The paid plugin must remain ignored and untracked: Plugins/EditorActorTagDisplay'
}

$scriptText = Get-Content -LiteralPath (Join-Path $sampleRoot 'Scripts/Build-DemoMap.py') -Raw
if ($scriptText.Contains('EditorLevelLibrary')) {
    throw 'Build-DemoMap.py uses the deprecated EditorLevelLibrary API.'
}
$displayModeAssignments = rg -n --hidden -S 'DisplayMode\s*=' (Join-Path $sampleRoot 'Scripts') 2>$null
if ($LASTEXITCODE -eq 0 -and $displayModeAssignments) {
    throw 'Sample scripts contain a DisplayMode assignment.'
}

$mapPath = Join-Path $sampleRoot 'Content\ActorMetadataOverlayDemo\Maps\ActorMetadataOverlayOverview.umap'
$mapState = [pscustomobject]@{
    Exists = Test-Path -LiteralPath $mapPath
    Size = if (Test-Path -LiteralPath $mapPath) { (Get-Item -LiteralPath $mapPath).Length } else { 0 }
}
if ($mapState.Size -le 0) {
    throw 'The overview map is missing or empty.'
}

$result = [pscustomobject]@{
    EngineVersion = $EngineVersion
    RequiredFiles = $required.Count
    MissingFiles = $missing
    ActorCount = $spec.actors.Count
    RuleCount = $spec.rules.Count
    Map = $mapState
    ForbiddenPaths = $forbiddenPresent
    PaidPluginTracked = $false
    EditorLevelLibrary = $false
    DisplayModeAssignments = @()
}
if (-not $OutputPath) {
    $OutputPath = Join-Path $sampleRoot '.verification\user-review\sample-static-verification.json'
}
New-Item -ItemType Directory -Path (Split-Path -Parent $OutputPath) -Force | Out-Null
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
$result | ConvertTo-Json -Depth 8

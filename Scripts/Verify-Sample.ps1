[CmdletBinding()]
param(
    [ValidateSet('5.6', '5.7', '5.8')]
    [string]$EngineVersion,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$sampleRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$required = @(
    'ActorMetadataSample.uproject',
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
if (-not $spec.editorRegion -or $spec.editorRegion.actorClass -ne 'ALocationVolume' -or $spec.editorRegion.actorName -ne 'AMO_DemoRegion' -or $spec.editorRegion.actorLabel -ne 'Actor Metadata Overlay Demo Region' -or $spec.editorRegion.loadsActorIds.Count -ne 7) {
    throw 'demo-spec.json editorRegion is missing or does not cover all seven fixture actors.'
}

$projectPath = Join-Path $sampleRoot 'ActorMetadataSample.uproject'
$project = Get-Content -LiteralPath $projectPath -Raw | ConvertFrom-Json
$projectModule = @($project.Modules | Where-Object { $_.Name -eq 'ActorMetadataSample' })
if ($projectModule.Count -ne 1) {
    throw 'ActorMetadataSample.uproject does not declare exactly one ActorMetadataSample module.'
}
$androidFileServerPlugin = @($project.Plugins | Where-Object { $_.Name -eq 'AndroidFileServer' })
if ($androidFileServerPlugin.Count -ne 1 -or $androidFileServerPlugin[0].Enabled -ne $false) {
    throw 'ActorMetadataSample.uproject must explicitly disable the engine AndroidFileServer plugin.'
}

function Get-RelativePath([string]$BasePath, [string]$ChildPath) {
    $baseUri = [System.Uri](([System.IO.Path]::GetFullPath($BasePath).TrimEnd('\')) + '\')
    $childUri = [System.Uri]([System.IO.Path]::GetFullPath($ChildPath))
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($childUri).ToString()).Replace('/', '\')
}

$textExtensions = @('.ini', '.json', '.md', '.ps1', '.py', '.txt', '.uplugin', '.uproject', '.cs', '.cpp', '.h', '.hpp')
$verifyScriptPath = (Resolve-Path -LiteralPath $PSCommandPath).Path

$legacyInternalNamePatterns = @(
    'ActorMetadataOverlaySample\.uproject',
    'ActorMetadataOverlaySampleEditor\b',
    'ActorMetadataOverlaySampleTarget\b',
    'ActorMetadataOverlaySampleEditorTarget\b',
    'FActorMetadataOverlaySample\w*',
    'ExtraModuleNames\.Add\(["'']ActorMetadataOverlaySample["'']\)',
    'IMPLEMENT_PRIMARY_GAME_MODULE\([^\r\n]*ActorMetadataOverlaySample'
)
$legacyInternalNameAllowlist = @('Scripts/Setup-Local.ps1')
$legacyMatches = @()
foreach ($pattern in $legacyInternalNamePatterns) {
    $matches = @(Get-ChildItem -LiteralPath $sampleRoot -Recurse -File | Where-Object {
        $_.FullName -notmatch '\\(\.git|Binaries|Intermediate|Saved|DerivedDataCache|\.verification)\\' -and
        $_.FullName -ne $verifyScriptPath -and
        $_.Extension -in $textExtensions -and
        ((Get-RelativePath $sampleRoot $_.FullName) -replace '\\', '/') -notin $legacyInternalNameAllowlist
    } | Select-String -Pattern $pattern)
    $legacyMatches += $matches
}
if ($legacyMatches.Count -gt 0) {
    $locations = @($legacyMatches | ForEach-Object { "$($_.Path):$($_.LineNumber):$($_.Line.Trim())" }) -join "`n"
    throw "Old internal project name remains outside its explicit generated-metadata allowlist:`n$locations"
}
$oldNameScanPath = Join-Path $sampleRoot '.verification\user-review\old-name-scan.json'
$oldNameScan = [ordered]@{
    status = 'PASS'
    internalProjectName = 'ActorMetadataSample'
    internalProjectNameLength = 'ActorMetadataSample'.Length
    forbiddenPatterns = $legacyInternalNamePatterns
    allowlist = $legacyInternalNameAllowlist
    matches = @()
    publicRepositoryNameAllowed = $true
}
New-Item -ItemType Directory -Path (Split-Path -Parent $oldNameScanPath) -Force | Out-Null
$oldNameScan | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $oldNameScanPath -Encoding UTF8

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

$localPluginRoot = Join-Path $sampleRoot 'Plugins/EditorActorTagDisplay'
$localPluginPresent = Test-Path -LiteralPath $localPluginRoot
$expectedPluginEngineVersion = if ($EngineVersion) { "$EngineVersion.0" } else { $null }
$actualPluginEngineVersion = $null
$localCopyMarkerValid = $false
$localPluginTracked = ($paidPluginTracked.Count -gt 0)
if ($EngineVersion -and $localPluginPresent) {
    $localDescriptorPath = Join-Path $localPluginRoot 'EditorActorTagDisplay.uplugin'
    if (-not (Test-Path -LiteralPath $localDescriptorPath)) {
        throw 'The local Actor Metadata Overlay copy is missing EditorActorTagDisplay.uplugin. Run Setup-Local.ps1 again with -EngineVersion 5.6 -Build.'
    }
    $localDescriptor = Get-Content -LiteralPath $localDescriptorPath -Raw | ConvertFrom-Json
    $actualPluginEngineVersion = $localDescriptor.EngineVersion
    if ($localDescriptor.FriendlyName -ne 'Actor Metadata Overlay') {
        throw 'The local Actor Metadata Overlay copy has an unexpected FriendlyName. Run Setup-Local.ps1 again with -EngineVersion 5.6 -Build.'
    }
    if ($actualPluginEngineVersion -ne $expectedPluginEngineVersion) {
        throw "The local Actor Metadata Overlay copy targets Unreal Engine $actualPluginEngineVersion, but this verification run targets Unreal Engine $EngineVersion. Run Setup-Local.ps1 again with -EngineVersion $EngineVersion -Build."
    }

    $localMarkerPath = Join-Path $localPluginRoot '.amo-sample-local-copy.json'
    if (-not (Test-Path -LiteralPath $localMarkerPath)) {
        throw 'The local Actor Metadata Overlay copy is missing .amo-sample-local-copy.json. Run Setup-Local.ps1 again with -EngineVersion 5.6 -Build.'
    }
    $localMarker = Get-Content -LiteralPath $localMarkerPath -Raw | ConvertFrom-Json
    if ($localMarker.engineVersion -ne $EngineVersion) {
        throw "The local copy marker targets Unreal Engine $($localMarker.engineVersion), but this verification run targets Unreal Engine $EngineVersion. Run Setup-Local.ps1 again with -EngineVersion $EngineVersion -Build."
    }
    if ($localMarker.descriptorEngineVersion -ne $actualPluginEngineVersion) {
        throw 'The local copy marker does not match the copied descriptor EngineVersion. Run Setup-Local.ps1 again with -EngineVersion 5.6 -Build.'
    }
    if ([string]::IsNullOrWhiteSpace([string]$localMarker.sourceDescriptorSha256)) {
        throw 'The local copy marker has an empty sourceDescriptorSha256. Run Setup-Local.ps1 again with -EngineVersion 5.6 -Build.'
    }

    $forbiddenLocalDirectories = @()
    if (Test-Path -LiteralPath (Join-Path $localPluginRoot '.git')) {
        $forbiddenLocalDirectories += '.git'
    }
    $localDirectories = @(Get-ChildItem -LiteralPath $localPluginRoot -Recurse -Directory -Force)
    foreach ($localDirectory in $localDirectories) {
        if ($localDirectory.Name -ieq 'Marketing' -or $localDirectory.Name -ieq '.verification' -or $localDirectory.Name -ieq '.git') {
            $forbiddenLocalDirectories += $localDirectory.Name
        }
    }
    if ($forbiddenLocalDirectories.Count -gt 0) {
        throw "The local paid plugin copy contains forbidden directories: $($forbiddenLocalDirectories -join ', ')"
    }
    $localCopyMarkerValid = $true
}

$scriptText = Get-Content -LiteralPath (Join-Path $sampleRoot 'Scripts/Build-DemoMap.py') -Raw
if ($scriptText.Contains('EditorLevelLibrary')) {
    throw 'Build-DemoMap.py uses the deprecated EditorLevelLibrary API.'
}
$displayModeAssignments = rg -n --hidden -S 'DisplayMode\s*=' (Join-Path $sampleRoot 'Scripts') 2>$null
if ($LASTEXITCODE -eq 0 -and $displayModeAssignments) {
    throw 'Sample scripts contain a DisplayMode assignment.'
}
$regionModulePath = Join-Path $sampleRoot 'Plugins/ActorMetadataOverlayDemoFixtures/Source/ActorMetadataOverlayDemoFixturesEditor/Private/ActorMetadataOverlayDemoFixturesEditor.cpp'
$regionModuleText = Get-Content -LiteralPath $regionModulePath -Raw
if (-not $regionModuleText.Contains('FEditorDelegates::OnMapOpened') -or -not $regionModuleText.Contains('ALocationVolume') -or -not $regionModuleText.Contains('AMO_DemoRegion')) {
    throw 'The Fixture Editor module does not contain the exact one-shot LocationVolume map-open loader.'
}
if ($regionModuleText.Contains('FTSTicker') -or $regionModuleText.Contains('DisplayMode') -or $regionModuleText.Contains('Python')) {
    throw 'The Fixture Editor module contains a forbidden tick, display-mode, or Python startup path.'
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
    LocalPluginPresent = $localPluginPresent
    ExpectedPluginEngineVersion = $expectedPluginEngineVersion
    ActualPluginEngineVersion = $actualPluginEngineVersion
    LocalCopyMarkerValid = $localCopyMarkerValid
    LocalPluginTracked = $localPluginTracked
    EditorLevelLibrary = $false
    DisplayModeAssignments = @()
    LegacyInternalNameScan = $oldNameScan
    EditorRegionLoader = [pscustomobject]@{
        MapOpenedDelegate = $true
        LocationVolume = $true
        Tick = $false
        PythonStartup = $false
        DisplayModeMutation = $false
    }
}
if (-not $OutputPath) {
    $OutputPath = Join-Path $sampleRoot '.verification\user-review\sample-static-verification.json'
}
New-Item -ItemType Directory -Path (Split-Path -Parent $OutputPath) -Force | Out-Null
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
$result | ConvertTo-Json -Depth 8

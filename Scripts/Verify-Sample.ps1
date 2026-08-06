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

$visualEnvironment = $spec.visualEnvironment
if (-not $visualEnvironment -or $visualEnvironment.style -ne 'polished-technical-showcase' -or $visualEnvironment.folder -ne 'ActorMetadataOverlayDemo/Environment') {
    throw 'demo-spec.json visualEnvironment is missing or has an unexpected style/folder.'
}
$environmentEntries = @($visualEnvironment.floor, $visualEnvironment.directionalLight, $visualEnvironment.skyLight, $visualEnvironment.skyAtmosphere)
if ($environmentEntries.Count -ne 4) {
    throw 'demo-spec.json visualEnvironment must contain exactly four environment actors.'
}
$environmentNames = @($environmentEntries | ForEach-Object { $_.actorName })
if (($environmentNames | Select-Object -Unique).Count -ne 4 -or $environmentNames -contains $null -or $environmentNames -contains '') {
    throw 'visualEnvironment actor names must be present and unique.'
}
$fixtureNames = @($spec.actors | ForEach-Object { $_.actorName })
$regionNames = @($spec.editorRegion.actorName)
$presentationNames = @()
$presentation = $spec.presentation
if (-not $presentation -or $presentation.style -ne 'polished-technical-showcase' -or -not $presentation.plaza) {
    throw 'demo-spec.json presentation is missing or has an unexpected style/plaza definition.'
}
$presentationEntries = @(
    $presentation.plaza.floor
    $presentation.plaza.borders
    $presentation.plaza.accentLines
    $presentation.plaza.backdrop
    $spec.stations
    $spec.distanceLane.floor
    $spec.distanceLane.borders
    $spec.distanceLane.boundary
    $spec.distanceLane.markers
    $spec.distanceLane.labels
    $spec.signage.texts
    $spec.overviewCamera
)
foreach ($entry in $presentationEntries) {
    if (-not $entry -or [string]::IsNullOrWhiteSpace([string]$entry.actorName) -or $entry.actorName -notlike 'AMO_Environment_*') {
        throw 'Every presentation actor must define a unique AMO_Environment_ actorName.'
    }
    $presentationNames += $entry.actorName
}
$generatedActorWaitSet = @($fixtureNames) + @($regionNames) + @($environmentNames) + @($presentationNames)
$generatedActorWaitUnique = @($generatedActorWaitSet | Select-Object -Unique)
if ($fixtureNames.Count -ne 7 -or $regionNames.Count -ne 1 -or $environmentNames.Count -ne 4) {
    throw "Expected generated actor groups of 7 fixtures, 1 region, and 4 environment actors; found $($fixtureNames.Count), $($regionNames.Count), and $($environmentNames.Count)."
}
if ($presentationNames.Count -ne 30 -or $generatedActorWaitSet.Count -ne 42 -or $generatedActorWaitUnique.Count -ne 42) {
    throw "Expected 30 presentation actors and a 42-name generated actor wait set; found $($presentationNames.Count), $($generatedActorWaitSet.Count), and $($generatedActorWaitUnique.Count)."
}
if (@($generatedActorWaitSet | Where-Object { [string]::IsNullOrWhiteSpace([string]$_) }).Count -gt 0) {
    throw 'Generated actor wait set contains an empty actor name.'
}
if (@($environmentNames | Where-Object { $_ -notlike 'AMO_Environment_*' }).Count -gt 0) {
    throw 'Every visual environment actor name must use the AMO_Environment_ prefix.'
}
if ($regionNames[0] -ne 'AMO_DemoRegion') {
    throw 'The generated actor wait set must use AMO_DemoRegion as its region name.'
}
if ($visualEnvironment.floor.actorClass -ne 'AStaticMeshActor' -or $visualEnvironment.floor.mesh -ne '/Engine/BasicShapes/Cube.Cube') {
    throw 'visualEnvironment floor must use AStaticMeshActor and Engine Cube.'
}
if ($visualEnvironment.directionalLight.actorClass -ne 'ADirectionalLight' -or $visualEnvironment.skyLight.actorClass -ne 'ASkyLight' -or $visualEnvironment.skyAtmosphere.actorClass -ne 'ASkyAtmosphere') {
    throw 'visualEnvironment must use the standard directional light, sky light, and sky atmosphere actors.'
}
$pointActors = @($spec.actors | Where-Object { $_.actorClass -eq 'AActorMetadataOverlayDemoActor' })
if ($pointActors.Count -ne 6) {
    throw 'demo-spec.json must contain six point fixture actors.'
}
$invalidVisualMeshes = @($pointActors | Where-Object { [string]::IsNullOrWhiteSpace([string]$_.visualMesh) -or $_.visualMesh -notlike '/Engine/BasicShapes/*' })
if ($invalidVisualMeshes.Count -gt 0) {
    throw 'Every point fixture must have a visualMesh under /Engine/BasicShapes/.'
}
$visualMeshPaths = @($pointActors | ForEach-Object { $_.visualMesh })
if (($visualMeshPaths | Where-Object { $_ -notlike '/Engine/BasicShapes/*' }).Count -gt 0) {
    throw 'A visualMesh outside Engine Basic Shapes was found.'
}

$expectedSignage = [ordered]@{
    AMO_Environment_Text_Title = [pscustomobject]@{ Text = 'ACTOR METADATA OVERLAY'; MinimumSize = 180.0 }
    AMO_Environment_Text_ShowMode = [pscustomobject]@{ Text = 'Viewport Show  /  Actor Metadata Overlay'; MinimumSize = 80.0 }
    AMO_Environment_Text_Modes = [pscustomobject]@{ Text = 'Selected Actors   /   All Matching Actors   /   Off'; MinimumSize = 68.0 }
    AMO_Environment_Text_Instructions = [pscustomobject]@{ Text = "Select a fixture to inspect metadata.`nThe gray station is excluded with OverlayHidden.`nThe 200 m actor demonstrates draw-distance filtering."; MinimumSize = 60.0 }
}
$signageEntries = @($spec.signage.texts)
if ($signageEntries.Count -ne $expectedSignage.Count) {
    throw "Expected exactly $($expectedSignage.Count) signage text actors; found $($signageEntries.Count)."
}
foreach ($expectedSignageEntry in $expectedSignage.GetEnumerator()) {
    $matchingSignage = @($signageEntries | Where-Object { $_.actorName -eq $expectedSignageEntry.Key })
    if ($matchingSignage.Count -ne 1) {
        throw "Expected exactly one signage actor named $($expectedSignageEntry.Key)."
    }
    $signageEntry = $matchingSignage[0]
    if ($signageEntry.actorClass -ne 'ATextRenderActor' -or $signageEntry.text -ne $expectedSignageEntry.Value.Text) {
        throw "Signage actor $($expectedSignageEntry.Key) has the wrong class or text."
    }
    if ([double]$signageEntry.textSize -lt $expectedSignageEntry.Value.MinimumSize) {
        throw "Signage actor $($expectedSignageEntry.Key) is smaller than its public-presentation minimum size."
    }
    if (@($signageEntry.location).Count -ne 3 -or @($signageEntry.rotation).Count -ne 3 -or @($signageEntry.scale).Count -ne 3) {
        throw "Signage actor $($expectedSignageEntry.Key) must define three-value location, rotation, and scale arrays."
    }
    if ([double]$signageEntry.rotation[0] -ne 0.0 -or [double]$signageEntry.rotation[1] -ne -90.0 -or [double]$signageEntry.rotation[2] -ne 0.0) {
        throw "Signage actor $($expectedSignageEntry.Key) must face the Overview Camera with the verified negative-Y rotation."
    }
    if (@($signageEntry.textColor).Count -ne 4 -or [int]$signageEntry.textColor[3] -ne 255) {
        throw "Signage actor $($expectedSignageEntry.Key) must define an opaque RGBA textColor."
    }
    $boardFrontY = [double]$spec.presentation.plaza.backdrop.location[1] - 25.0
    if ([double]$signageEntry.location[1] -ge $boardFrontY -or [double]$signageEntry.location[1] -lt ($boardFrontY - 160.0)) {
        throw "Signage actor $($expectedSignageEntry.Key) must remain visibly in front of the Title Board without excessive floating."
    }
}

$captureViewNames = @('overviewSelected', 'overviewClean', 'distanceLane')
if (-not $spec.captureViews) {
    throw 'demo-spec.json must define machine-readable captureViews for selected, clean, and distance evidence.'
}
foreach ($captureViewName in $captureViewNames) {
    $captureView = $spec.captureViews.$captureViewName
    if (-not $captureView -or @($captureView.location).Count -ne 3 -or @($captureView.rotation).Count -ne 3 -or
        [double]$captureView.fov -lt 35.0 -or [double]$captureView.fov -gt 80.0) {
        throw "captureViews.$captureViewName must define a three-value location, three-value rotation, and sane FOV."
    }
}

$expectedDistanceLabels = [ordered]@{
    AMO_Environment_Text_50m = [pscustomobject]@{ Text = '50 m  REFERENCE'; Yaw = -122.0 }
    AMO_Environment_Text_100m = [pscustomobject]@{ Text = '100 m  DEFAULT DRAW DISTANCE'; Yaw = -145.0 }
    AMO_Environment_Text_200m = [pscustomobject]@{ Text = '200 m  FAR ACTOR'; Yaw = -161.0 }
}
$distanceLabels = @($spec.distanceLane.labels)
if ($distanceLabels.Count -ne $expectedDistanceLabels.Count) {
    throw "Expected exactly $($expectedDistanceLabels.Count) distance labels; found $($distanceLabels.Count)."
}
foreach ($expectedDistanceLabel in $expectedDistanceLabels.GetEnumerator()) {
    $matchingLabels = @($distanceLabels | Where-Object { $_.actorName -eq $expectedDistanceLabel.Key })
    if ($matchingLabels.Count -ne 1 -or $matchingLabels[0].text -ne $expectedDistanceLabel.Value.Text) {
        throw "Distance label $($expectedDistanceLabel.Key) is missing or has the wrong text."
    }
    $distanceLabel = $matchingLabels[0]
    if ([double]$distanceLabel.textSize -lt 120.0 -or [double]$distanceLabel.rotation[0] -ne 0.0 -or
        [double]$distanceLabel.rotation[1] -ne $expectedDistanceLabel.Value.Yaw -or [double]$distanceLabel.rotation[2] -ne 0.0) {
        throw "Distance label $($expectedDistanceLabel.Key) must be large and camera-facing for the public distance view."
    }
}
$defaultBoundary = $spec.distanceLane.boundary
if ([double]$defaultBoundary.location[2] -lt 250.0 -or [double]$defaultBoundary.scale[1] -lt 7.5 -or [double]$defaultBoundary.scale[2] -lt 0.2) {
    throw 'The 100 m default boundary must form a raised crossbar that is distinguishable without color.'
}

$materials = $spec.materials
$materialRoot = '/Game/ActorMetadataOverlayDemo/Visuals/Materials'
if (-not $materials -or $materials.root -ne $materialRoot -or -not $materials.master -or $materials.master.path -ne "$materialRoot/M_AMO_Surface") {
    throw 'demo-spec.json materials must define the Sample-owned M_AMO_Surface root.'
}
if (@($materials.master.parameters.PSObject.Properties).Count -ne 5 -or
    'BaseColor' -notin @($materials.master.parameters.PSObject.Properties.Name) -or
    'Roughness' -notin @($materials.master.parameters.PSObject.Properties.Name) -or
    'Metallic' -notin @($materials.master.parameters.PSObject.Properties.Name) -or
    'EmissiveColor' -notin @($materials.master.parameters.PSObject.Properties.Name) -or
    'EmissiveStrength' -notin @($materials.master.parameters.PSObject.Properties.Name)) {
    throw 'M_AMO_Surface must define BaseColor, Roughness, Metallic, EmissiveColor, and EmissiveStrength parameters.'
}
$materialInstances = @($materials.instances)
if ($materialInstances.Count -ne 13) {
    throw "Expected exactly 13 Sample Material Instances; found $($materialInstances.Count)."
}
$materialInstanceNames = @($materialInstances | ForEach-Object { $_.name })
if (($materialInstanceNames | Select-Object -Unique).Count -ne 13 -or @($materialInstances | Where-Object { $_.path -notlike "$materialRoot/*" }).Count -gt 0) {
    throw 'Material Instance names or paths are not unique and Sample-owned.'
}
foreach ($fixture in $spec.actors) {
    if ([string]::IsNullOrWhiteSpace([string]$fixture.visualMaterial) -or $fixture.visualMaterial -notin $materialInstanceNames) {
        throw "Fixture $($fixture.actorName) has no valid Sample visualMaterial assignment."
    }
}
if (-not $materials.fixtureAssignments -or @($materials.fixtureAssignments.PSObject.Properties).Count -ne 7) {
    throw 'materials.fixtureAssignments must cover all seven fixture actors.'
}
$materialAssetRoot = Join-Path $sampleRoot 'Content\ActorMetadataOverlayDemo\Visuals\Materials'
$expectedMaterialFiles = @('M_AMO_Surface.uasset') + @($materialInstanceNames | ForEach-Object { "${_}.uasset" })
$actualMaterialFiles = @()
if (Test-Path -LiteralPath $materialAssetRoot) {
    $actualMaterialFiles = @(Get-ChildItem -LiteralPath $materialAssetRoot -File -Force | ForEach-Object { $_.Name })
}
$missingMaterialFiles = @($expectedMaterialFiles | Where-Object { $_ -notin $actualMaterialFiles })
$unexpectedMaterialFiles = @($actualMaterialFiles | Where-Object { $_ -notin $expectedMaterialFiles })
if ($missingMaterialFiles.Count -gt 0 -or $unexpectedMaterialFiles.Count -gt 0) {
    throw "Sample material asset set mismatch. Missing: $($missingMaterialFiles -join ', '); unexpected: $($unexpectedMaterialFiles -join ', ')"
}
$textureAssets = @(Get-ChildItem -LiteralPath (Join-Path $sampleRoot 'Content') -Recurse -File -Force -ErrorAction SilentlyContinue | Where-Object { $_.Extension -in @('.utexture', '.png', '.jpg', '.jpeg', '.exr', '.hdr') })
if ($textureAssets.Count -gt 0) {
    throw "Sample must not add texture assets for the showcase: $($textureAssets -join ', ')"
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
$captureScriptText = Get-Content -LiteralPath (Join-Path $sampleRoot 'Scripts/Capture/Apply-DemoSpec.py') -Raw
foreach ($ignoredCaptureKey in @('visualEnvironment', 'visualMesh', 'visualMaterial', 'materials', 'presentation', 'stations', 'distanceLane', 'signage', 'overviewCamera', 'captureViews')) {
    if ($captureScriptText.Contains($ignoredCaptureKey)) {
        throw "Capture Apply script must ignore the public presentation key '$ignoredCaptureKey'."
    }
}
if (-not $scriptText.Contains('ensure_visual_environment') -or -not $scriptText.Contains('AMO_Environment_') -or -not $scriptText.Contains('assign_basic_shape_mesh') -or -not $scriptText.Contains('ensure_material_system') -or -not $scriptText.Contains('presentation_actor_entries')) {
    throw 'Build-DemoMap.py does not contain the required environment regeneration and Basic Shape assignment paths.'
}
if (-not $scriptText.Contains('def get_required_generated_actor_names') -or
    -not $scriptText.Contains('actor_entries = spec.get("actors")') -or
    -not $scriptText.Contains('region_spec.get("actorName")') -or
    -not $scriptText.Contains('visual_environment.get(key)') -or
    -not $scriptText.Contains('VISUAL_ENVIRONMENT_KEYS') -or
    -not $scriptText.Contains('presentation_actor_entries(spec)')) {
    throw 'Build-DemoMap.py does not build the required generated actor wait set from demo-spec.json.'
}
if (-not $scriptText.Contains('required_names = get_required_generated_actor_names(spec)') -or
    $scriptText.Contains('required_names = {entry["actorName"] for entry in spec["actors"]}')) {
    throw 'wait_for_existing_generated_actors must use the shared generated actor wait-set helper without excluding visual environment actors.'
}
$waitFunctionMatch = [regex]::Match($scriptText, '(?ms)^def wait_for_existing_generated_actors\(.*?(?=^def\s|\z)')
if (-not $waitFunctionMatch.Success) {
    throw 'Build-DemoMap.py does not contain a complete wait_for_existing_generated_actors function.'
}
$waitFunctionText = $waitFunctionMatch.Value
if (-not $waitFunctionText.Contains('required_names = get_required_generated_actor_names(spec)') -or
    -not $waitFunctionText.Contains('required_name_set = set(required_names)') -or
    -not $waitFunctionText.Contains('required_name_set.issubset(current_names)')) {
    throw 'wait_for_existing_generated_actors must use the shared complete generated actor set.'
}
if (-not $waitFunctionText.Contains('missing = sorted(required_name_set - current_names)') -or
    -not $waitFunctionText.Contains('Timed out waiting for the complete generated actor set') -or
    -not $waitFunctionText.Contains('unregister_wait_callback(state)') -or
    -not $waitFunctionText.Contains('state["completed"]')) {
    throw 'Build-DemoMap.py must report sorted missing names, always unregister its callback, and guard against duplicate regeneration.'
}
if ($waitFunctionText.Contains('state["attempts"] >= 30') -or
    $waitFunctionText.Contains('actor set differs from demo-spec.json') -or
    $waitFunctionText.Contains('any(name.startswith("AMO_")') -or
    $waitFunctionText.Contains('fallback')) {
    throw 'wait_for_existing_generated_actors contains a partial actor-set or tick-count fallback.'
}
if (-not [regex]::IsMatch($waitFunctionText, '(?s)if\s+required_name_set\.issubset\(current_names\):.*?regenerate_map\(spec, level_subsystem, actor_subsystem\)')) {
    throw 'regenerate_map must be reachable only from the complete generated actor-set branch.'
}
$incompleteBranchMatch = [regex]::Match($waitFunctionText, '(?ms)^[ \t]+if not required_name_set\.issubset\(current_names\):.*?(?=^[ \t]+if required_name_set\.issubset\(current_names\):|^[ \t]+except Exception:)')
if ($incompleteBranchMatch.Success -and $incompleteBranchMatch.Value.Contains('regenerate_map(spec, level_subsystem, actor_subsystem)')) {
    throw 'regenerate_map must not be reachable from the incomplete generated actor-set branch.'
}
if ([regex]::IsMatch($waitFunctionText, '(?s)if\s+state\["attempts"\].*?regenerate_map\(') -or
    [regex]::IsMatch($waitFunctionText, '(?s)if\s+.*?startswith\("AMO_"\).*?regenerate_map\(')) {
    throw 'regenerate_map must not be gated by a tick-count or AMO_ prefix condition.'
}
if ($scriptText.Contains('fallback') -and $scriptText.Contains('visualMesh')) {
    throw 'Build-DemoMap.py must stop on an invalid visual mesh instead of silently falling back.'
}
$regionModulePath = Join-Path $sampleRoot 'Plugins/ActorMetadataOverlayDemoFixtures/Source/ActorMetadataOverlayDemoFixturesEditor/Private/ActorMetadataOverlayDemoFixturesEditor.cpp'
$regionModuleText = Get-Content -LiteralPath $regionModulePath -Raw
if (-not $regionModuleText.Contains('FEditorDelegates::OnMapOpened') -or -not $regionModuleText.Contains('ALocationVolume') -or -not $regionModuleText.Contains('AMO_DemoRegion')) {
    throw 'The Fixture Editor module does not contain the exact LocationVolume map-open loader.'
}
if ($regionModuleText.Contains('FTSTicker') -or $regionModuleText.Contains('DisplayMode') -or $regionModuleText.Contains('Python')) {
    throw 'The Fixture Editor module contains a forbidden tick, display-mode, or Python startup path.'
}
$onMapOpenedAddCount = ([regex]::Matches($regionModuleText, 'FEditorDelegates::OnMapOpened\.AddRaw')).Count
$onMapOpenedRemoveCount = ([regex]::Matches($regionModuleText, 'FEditorDelegates::OnMapOpened\.Remove')).Count
if ($onMapOpenedAddCount -ne 1 -or $onMapOpenedRemoveCount -ne 1) {
    throw 'The Fixture Editor module must register OnMapOpened once and remove it only through its shutdown cleanup path.'
}
if (-not [regex]::IsMatch($regionModuleText, '(?s)ShutdownModule\(\).*?RemoveMapOpenedDelegate\(\)')) {
    throw 'The Fixture Editor module does not remove the OnMapOpened delegate from ShutdownModule.'
}
if ([regex]::IsMatch($regionModuleText, '(?s)TryLoadDemoRegion\(\)\s*\)\s*\{\s*RemoveMapOpenedDelegate\(\)')) {
    throw 'The Fixture Editor module still removes the delegate after a successful startup load.'
}
if ([regex]::IsMatch($regionModuleText, '(?s)void\s+HandleMapOpened\s*\([^)]*\)\s*\{\s*if\s*\(\s*TryLoadDemoRegion\(\)\s*&&.*?RemoveMapOpenedDelegate')) {
    throw 'The Fixture Editor module still removes the delegate from HandleMapOpened.'
}
if (-not $regionModuleText.Contains('World->WorldType != EWorldType::Editor') -or -not $regionModuleText.Contains('World->GetOutermost()->GetName() != DemoMapPackage')) {
    throw 'The Fixture Editor module does not enforce the exact Editor world and Overview Map package checks.'
}
if (-not [regex]::IsMatch($regionModuleText, '(?s)if\s*\(!DemoRegion->IsLoaded\(\)\).*?DemoRegion->Load\(\).*?if\s*\(!DemoRegion->IsLoaded\(\)\)')) {
    throw 'The Fixture Editor module does not guard duplicate loads and confirm IsLoaded after Load.'
}
if ($regionModuleText.Contains('LogTemp') -or -not $regionModuleText.Contains('DEFINE_LOG_CATEGORY_STATIC(LogActorMetadataOverlayDemoFixturesEditor')) {
    throw 'The Fixture Editor module must use its own log category and must not use LogTemp.'
}
if (-not $regionModuleText.Contains('bRegionWarningIssued') -or -not [regex]::IsMatch($regionModuleText, '(?s)UE_LOG\(LogActorMetadataOverlayDemoFixturesEditor,\s*Warning')) {
    throw 'The Fixture Editor module must issue a bounded module-specific warning for missing or duplicate regions.'
}
if (-not $regionModuleText.Contains('SetIsTemporarilyHiddenInEditor(true)') -or -not $regionModuleText.Contains('IsTemporarilyHiddenInEditor()')) {
    throw 'The Fixture Editor module must temporarily hide the loaded demo region using the editor-only API.'
}

$testSourcePath = Join-Path $sampleRoot 'Plugins/ActorMetadataOverlayDemoFixtures/Source/ActorMetadataOverlayDemoFixturesEditor/Private/ActorMetadataOverlayDemoTests.cpp'
$testSourceText = Get-Content -LiteralPath $testSourcePath -Raw
$automationTestMatches = [regex]::Matches($testSourceText, '(?s)IMPLEMENT_(?:SIMPLE|CUSTOM_COMPLEX|COMPLEX)_AUTOMATION_TEST\s*\([^,]+,\s*"([^"]+)"')
$automationTestPaths = @($automationTestMatches | ForEach-Object { $_.Groups[1].Value })
if ($automationTestPaths.Count -ne 10 -or 'ActorMetadataOverlay.Sample.RegionReopen' -notin $automationTestPaths -or 'ActorMetadataOverlay.Sample.VisualEnvironment' -notin $automationTestPaths -or 'ActorMetadataOverlay.Sample.Presentation' -notin $automationTestPaths) {
    throw "Expected exactly ten Sample automation tests including VisualEnvironment, Presentation, and RegionReopen; found $($automationTestPaths.Count): $($automationTestPaths -join ', ')."
}
$regionReopenSectionMatch = [regex]::Match($testSourceText, '(?s)IMPLEMENT_SIMPLE_AUTOMATION_TEST\(FActorMetadataSampleRegionReopenTest.*\z')
if (-not $regionReopenSectionMatch.Success) {
    throw 'The RegionReopen automation test source is missing.'
}
$regionReopenSection = $regionReopenSectionMatch.Value
if ($regionReopenSection.Contains('ForEachActorWithLoading') -or $regionReopenSection.Contains('DemoRegion->Load') -or $regionReopenSection.Contains('ALocationVolume::Load')) {
    throw 'RegionReopen must not explicitly load the region or fixture actors from the test.'
}
$visualEnvironmentSectionMatch = [regex]::Match($testSourceText, '(?s)IMPLEMENT_SIMPLE_AUTOMATION_TEST\(FActorMetadataSampleVisualEnvironmentTest.*?IMPLEMENT_SIMPLE_AUTOMATION_TEST\(FActorMetadataSampleRegionReopenTest')
if (-not $visualEnvironmentSectionMatch.Success) {
    throw 'The VisualEnvironment automation test source is missing.'
}
if ($visualEnvironmentSectionMatch.Value.Contains('ForEachActorWithLoading') -or $visualEnvironmentSectionMatch.Value.Contains('DemoRegion->Load') -or $visualEnvironmentSectionMatch.Value.Contains('ALocationVolume::Load')) {
    throw 'VisualEnvironment must use normal actor iteration and must not explicitly load the region or fixture actors.'
}
$mapPath = Join-Path $sampleRoot 'Content\ActorMetadataOverlayDemo\Maps\ActorMetadataOverlayOverview.umap'
$mapState = [pscustomobject]@{
    Exists = Test-Path -LiteralPath $mapPath
    Size = if (Test-Path -LiteralPath $mapPath) { (Get-Item -LiteralPath $mapPath).Length } else { 0 }
}
if ($mapState.Size -le 0) {
    throw 'The overview map is missing or empty.'
}
$showcasePath = Join-Path $sampleRoot 'Documentation\Images\actor-metadata-overlay-showcase.png'
Add-Type -AssemblyName System.Drawing
$showcaseImage = [System.Drawing.Image]::FromFile($showcasePath)
try {
    if ($showcaseImage.Width -ne 1600 -or $showcaseImage.Height -ne 900) {
        throw "README showcase dimensions must be 1600x900; found $($showcaseImage.Width)x$($showcaseImage.Height)."
    }
} finally {
    $showcaseImage.Dispose()
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
        ExactEditorWorldAndMap = $true
        ShutdownOnlyRemoval = $true
        DuplicateLoadGuard = $true
        WarningBoundedPerSession = $true
        Tick = $false
        PythonStartup = $false
        DisplayModeMutation = $false
    }
    AutomationTests = [pscustomobject]@{
        Count = $automationTestPaths.Count
        Paths = $automationTestPaths
        VisualEnvironment = $true
        RegionReopen = $true
    }
    VisualEnvironment = [pscustomobject]@{
        Style = $visualEnvironment.style
        Folder = $visualEnvironment.folder
        ActorNames = $environmentNames
        FloorMesh = $visualEnvironment.floor.mesh
        PointVisualMeshes = $visualMeshPaths
        CaptureApplyExcludesVisuals = $true
        NewMaterialTextureAssets = 0
    }
    PresentationStyle = $presentation.style
    MaterialAssets = $actualMaterialFiles
    MaterialInstanceCount = $materialInstances.Count
    DefaultMaterialFixtureCount = 0
    PresentationActorNames = $presentationNames
    PresentationActorCount = $presentationNames.Count
    OverviewCamera = [pscustomobject]@{
        ActorName = $spec.overviewCamera.actorName
        Location = $spec.overviewCamera.location
        Rotation = $spec.overviewCamera.rotation
        Fov = $spec.overviewCamera.fov
        SpatiallyLoaded = $false
    }
    DistanceMarkers = @($spec.distanceLane.markers | ForEach-Object { [pscustomobject]@{ ActorName = $_.actorName; DistanceMeters = $_.distanceMeters } })
    CaptureViews = [pscustomobject]@{
        OverviewSelected = $spec.captureViews.overviewSelected
        OverviewClean = $spec.captureViews.overviewClean
        DistanceLane = $spec.captureViews.distanceLane
    }
    Signage = @($signageEntries | ForEach-Object { [pscustomobject]@{ ActorName = $_.actorName; Text = $_.text; TextSize = $_.textSize; Rotation = $_.rotation; TextColor = $_.textColor } })
    DistanceLabels = @($distanceLabels | ForEach-Object { [pscustomobject]@{ ActorName = $_.actorName; Text = $_.text; TextSize = $_.textSize; Rotation = $_.rotation } })
    DefaultBoundary = [pscustomobject]@{ Location = $defaultBoundary.location; Scale = $defaultBoundary.scale; NonColorShapeDifference = $true }
    ReadmeShowcase = [pscustomobject]@{ Width = 1600; Height = 900 }
    CaptureApplyExcludesPresentation = $true
    GeneratedActorWaitSet = $generatedActorWaitSet
    GeneratedActorWaitCount = $generatedActorWaitSet.Count
    GeneratedActorWaitUnique = ($generatedActorWaitUnique.Count -eq $generatedActorWaitSet.Count)
    EnvironmentActorsIncludedInWait = (@($environmentNames | Where-Object { $_ -in $generatedActorWaitSet }).Count -eq 4)
    StrictGeneratedActorWait = $true
    PartialActorSetFallback = $false
    RegenerateOnlyAfterCompleteSet = $true
    TimeoutIncludesSortedMissingNames = $true
}
if (-not $OutputPath) {
    $OutputPath = Join-Path $sampleRoot '.verification\user-review\sample-static-verification.json'
}
New-Item -ItemType Directory -Path (Split-Path -Parent $OutputPath) -Force | Out-Null
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
$result | ConvertTo-Json -Depth 8

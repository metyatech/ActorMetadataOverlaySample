[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$CaptureProjectRoot,
    [switch]$DryRun,
    [switch]$Apply,
    [switch]$AllowDelete,
    [string]$VerificationRoot
)

$ErrorActionPreference = 'Stop'
$sampleRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$captureRoot = [System.IO.Path]::GetFullPath((Resolve-Path $CaptureProjectRoot).Path)
if ($DryRun -and $Apply) { throw 'Choose either -DryRun or -Apply, not both.' }
if (-not $DryRun -and -not $Apply) { $DryRun = $true }
if (-not (Test-Path -LiteralPath (Join-Path $sampleRoot '.git'))) { throw 'Sample repository must be initialized before syncing.' }

$gitStatus = @(git -C $sampleRoot status --porcelain --untracked-files=all)
if ($gitStatus.Count -gt 0) { throw 'Sample working tree must be clean before syncing.' }
$sampleHead = (git -C $sampleRoot rev-parse HEAD).Trim()
if (-not $sampleHead) { throw 'Could not read Sample HEAD.' }
if (-not (Test-Path -LiteralPath $captureRoot -PathType Container)) { throw "Capture target does not exist: $captureRoot" }

if (-not $VerificationRoot) { $VerificationRoot = Join-Path $sampleRoot '.verification\sample-sync' }
if (-not [System.IO.Path]::IsPathRooted($VerificationRoot)) { $VerificationRoot = Join-Path $sampleRoot $VerificationRoot }
$verificationRoot = [System.IO.Path]::GetFullPath($VerificationRoot)
New-Item -ItemType Directory -Path $verificationRoot -Force | Out-Null
$backupRoot = Join-Path $verificationRoot 'backup'
New-Item -ItemType Directory -Path $backupRoot -Force | Out-Null

$allowedFiles = @(
    'Demo/demo-spec.json',
    'Config/Tags/ActorMetadataOverlaySampleTags.ini',
    'Scripts/Capture/Apply-DemoSpec.py'
)
$allowedRoots = @('Plugins/ActorMetadataOverlayDemoFixtures/')
$prohibitedFragments = @(
    'Plugins/EditorActorTagDisplay/',
    'DeepWaterStation/',
    'Content/DeepWaterStation/',
    'Maps/DemoMapScalabilityCinematic.umap',
    'Binaries/',
    'Intermediate/',
    'Saved/',
    'DerivedDataCache/',
    '.git/',
    'Marketing/',
    '.verification/'
)
$generatedDirectories = @('Binaries', 'Intermediate', 'Saved', 'DerivedDataCache', '.git', '.verification')

function Normalize-Relative([string]$Path) {
    return $Path.Replace('\', '/').TrimStart('/')
}
function Get-RelativePath([string]$BasePath, [string]$ChildPath) {
    $baseUri = [System.Uri](([System.IO.Path]::GetFullPath($BasePath).TrimEnd('\')) + '\')
    $childUri = [System.Uri]([System.IO.Path]::GetFullPath($ChildPath))
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($childUri).ToString()).Replace('/', '\')
}
function Assert-SafeRelative([string]$RelativePath) {
    $normalized = Normalize-Relative $RelativePath
    if ($normalized.Contains('..') -or [System.IO.Path]::IsPathRooted($RelativePath)) { throw "Unsafe relative path: $RelativePath" }
    foreach ($fragment in $prohibitedFragments) {
        if ($normalized.StartsWith($fragment, [System.StringComparison]::OrdinalIgnoreCase) -or $normalized -eq $fragment.TrimEnd('/')) { throw "Prohibited sync path: $normalized" }
    }
    return $normalized
}
function Resolve-Child([string]$Root, [string]$RelativePath) {
    $safe = Assert-SafeRelative $RelativePath
    $resolved = [System.IO.Path]::GetFullPath((Join-Path $Root $safe.Replace('/', '\')))
    $rootWithSlash = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($rootWithSlash, [System.StringComparison]::OrdinalIgnoreCase)) { throw "Resolved path escaped root: $RelativePath" }
    return $resolved
}
function Get-Hash([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $stream = [System.IO.File]::OpenRead($Path)
        try {
            return ([System.BitConverter]::ToString($sha256.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
        } finally {
            $stream.Dispose()
        }
    } finally {
        $sha256.Dispose()
    }
}
function Is-Allowed([string]$RelativePath) {
    $normalized = Normalize-Relative $RelativePath
    if ($allowedFiles -contains $normalized) { return $true }
    foreach ($root in $allowedRoots) { if ($normalized.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) { return $true } }
    return $false
}

$sourceRelative = New-Object System.Collections.Generic.List[string]
foreach ($relative in $allowedFiles) { $sourceRelative.Add((Assert-SafeRelative $relative)) }
$fixtureRoot = Join-Path $sampleRoot 'Plugins\ActorMetadataOverlayDemoFixtures'
foreach ($file in Get-ChildItem -LiteralPath $fixtureRoot -Recurse -File -Force) {
    $relative = Normalize-Relative (Get-RelativePath $sampleRoot $file.FullName)
    if ($relative.Split('/') | Where-Object { $generatedDirectories -contains $_ }) { continue }
    if (-not (Is-Allowed $relative)) { throw "Generated allowlist escaped: $relative" }
    $sourceRelative.Add($relative)
}
$sourceRelative = @($sourceRelative | Sort-Object -Unique)

$plan = New-Object System.Collections.Generic.List[object]
foreach ($relative in $sourceRelative) {
    $sourcePath = Resolve-Child $sampleRoot $relative
    $targetPath = Resolve-Child $captureRoot $relative
    $before = Get-Hash $targetPath
    $after = Get-Hash $sourcePath
    $action = if ($null -eq $before) { 'Add' } elseif ($before -eq $after) { 'Skip' } else { 'Update' }
    $plan.Add([pscustomobject]@{ RelativePath = $relative; Action = $action; BeforeSha256 = $before; AfterSha256 = $after })
}

$targetAllowlisted = @()
foreach ($root in $allowedRoots) {
    $targetRoot = Resolve-Child $captureRoot $root
    if (Test-Path -LiteralPath $targetRoot -PathType Container) {
        foreach ($file in Get-ChildItem -LiteralPath $targetRoot -Recurse -File -Force) {
            $relative = Normalize-Relative (Get-RelativePath $captureRoot $file.FullName)
            if ($relative.Split('/') | Where-Object { $generatedDirectories -contains $_ }) { continue }
            $targetAllowlisted += $relative
        }
    }
}
foreach ($relative in $allowedFiles) {
    $candidate = Resolve-Child $captureRoot $relative
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $targetAllowlisted += (Normalize-Relative $relative) }
}
$stale = @($targetAllowlisted | Sort-Object -Unique | Where-Object { $_ -notin $sourceRelative })
foreach ($relative in $stale) {
    $plan.Add([pscustomobject]@{ RelativePath = $relative; Action = 'Delete'; BeforeSha256 = Get-Hash (Resolve-Child $captureRoot $relative); AfterSha256 = $null })
}
$planArray = $plan.ToArray()

$protectedRootsPresent = @($prohibitedFragments | Where-Object { Test-Path -LiteralPath (Join-Path $captureRoot $_.TrimEnd('/').Replace('/', '\')) })
$planObject = [pscustomobject]@{
    schemaVersion = 1
    mode = if ($Apply) { 'Apply' } else { 'DryRun' }
    sampleRoot = $sampleRoot
    sampleHead = $sampleHead
    captureRoot = $captureRoot
    allowlist = @($allowedFiles + $allowedRoots)
    prohibited = $prohibitedFragments
    protectedPathsPresent = $protectedRootsPresent
    operations = $planArray
}
$planPath = Join-Path $verificationRoot 'sync-plan.json'
$planObject | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $planPath -Encoding UTF8

$deletes = @($planArray | Where-Object Action -eq 'Delete')
if ($deletes.Count -gt 0 -and -not $AllowDelete) {
    if ($Apply) { throw "Stale allowlisted files require explicit -AllowDelete: $($deletes.RelativePath -join ', ')" }
}

$applied = $false
if ($Apply) {
    foreach ($operation in @($planArray | Where-Object { $_.Action -in @('Add', 'Update', 'Delete') })) {
        $targetPath = Resolve-Child $captureRoot $operation.RelativePath
        if (Test-Path -LiteralPath $targetPath -PathType Leaf) {
            $backupPath = Join-Path $backupRoot ($operation.RelativePath.Replace('/', '\'))
            New-Item -ItemType Directory -Path (Split-Path -Parent $backupPath) -Force | Out-Null
            Copy-Item -LiteralPath $targetPath -Destination $backupPath -Force
        }
        if ($operation.Action -eq 'Delete') {
            [System.IO.File]::Delete($targetPath)
        } else {
            $sourcePath = Resolve-Child $sampleRoot $operation.RelativePath
            New-Item -ItemType Directory -Path (Split-Path -Parent $targetPath) -Force | Out-Null
            Copy-Item -LiteralPath $sourcePath -Destination $targetPath -Force
        }
    }
    $applied = $true
}

$resultObject = [pscustomobject]@{
    schemaVersion = 1
    success = $true
    applied = $applied
    sampleHead = $sampleHead
    captureRoot = $captureRoot
    verificationRoot = $verificationRoot
    operationCount = $planArray.Count
    changedCount = @($planArray | Where-Object { $_.Action -in @('Add', 'Update', 'Delete') }).Count
    deletes = $deletes.Count
    allowDelete = [bool]$AllowDelete
    backupRoot = $backupRoot
}
$resultPath = Join-Path $verificationRoot 'sync-result.json'
$resultObject | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $resultPath -Encoding UTF8
$resultObject | ConvertTo-Json -Depth 12

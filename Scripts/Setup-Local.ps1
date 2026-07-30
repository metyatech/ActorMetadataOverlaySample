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
$sampleRootFull = [System.IO.Path]::GetFullPath($sampleRoot).TrimEnd('\')
$sourceRoot = (Resolve-Path -LiteralPath $PluginSource).Path
$sourceRootFull = [System.IO.Path]::GetFullPath($sourceRoot).TrimEnd('\')
$sourceDescriptor = Join-Path $sourceRootFull 'EditorActorTagDisplay.uplugin'
$targetRoot = Join-Path $sampleRootFull 'Plugins\EditorActorTagDisplay'
$targetRootFull = [System.IO.Path]::GetFullPath($targetRoot).TrimEnd('\')
$expectedTargetRoot = [System.IO.Path]::GetFullPath((Join-Path $sampleRootFull 'Plugins\EditorActorTagDisplay')).TrimEnd('\')
$engineRoot = "C:\Program Files\Epic Games\UE_$EngineVersion"
$expectedDescriptorEngineVersion = "$EngineVersion.0"
$reviewRoot = Join-Path $sampleRootFull '.verification\user-review'
$setupRoot = Join-Path $sampleRootFull '.verification\setup-local'
$logRoot = Join-Path $setupRoot 'logs'
$resultPath = Join-Path $reviewRoot "setup-local-result-$EngineVersion.json"

$excludedSourceDirectories = @(
    '.git',
    '.vs',
    'Binaries',
    'Intermediate',
    'Saved',
    'DerivedDataCache',
    'Marketing',
    '.verification',
    'artifacts'
)
$excludedBackupDirectories = @('Binaries', 'Intermediate', 'Saved', 'DerivedDataCache', 'artifacts')
$allowedGeneratedPaths = @(
    (Join-Path $sampleRootFull 'Binaries'),
    (Join-Path $sampleRootFull 'Intermediate'),
    (Join-Path $sampleRootFull 'Plugins\ActorMetadataOverlayDemoFixtures\Binaries'),
    (Join-Path $sampleRootFull 'Plugins\ActorMetadataOverlayDemoFixtures\Intermediate'),
    (Join-Path $sampleRootFull 'Plugins\EditorActorTagDisplay\Binaries'),
    (Join-Path $sampleRootFull 'Plugins\EditorActorTagDisplay\Intermediate')
) | ForEach-Object { [System.IO.Path]::GetFullPath($_).TrimEnd('\') }

$sourceSnapshotBefore = $null
$sourceSnapshotAfter = $null
$backupInfo = $null
$stagingPath = $null
$markerPath = Join-Path $targetRootFull '.amo-sample-local-copy.json'
$backupRoot = $null
$backupManifestPath = $null
$replacedTarget = $false
$targetRemoved = $false
$rollbackOccurred = $false
$rollbackSucceeded = $false
$buildExitCode = $null
$projectFilesExitCode = $null
$excludedPathChecks = @()
$buildCleanupChecks = @()
$paidPluginTracked = $false
$resultStatus = 'Failed'
$resultError = $null

function Get-RelativePath([string]$BasePath, [string]$ChildPath) {
    $baseUri = [System.Uri](([System.IO.Path]::GetFullPath($BasePath).TrimEnd('\')) + '\')
    $childUri = [System.Uri]([System.IO.Path]::GetFullPath($ChildPath))
    return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($childUri).ToString()).Replace('/', '\')
}

function Get-Sha256([string]$FilePath) {
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    $stream = $null
    try {
        $stream = [System.IO.File]::OpenRead($FilePath)
        return ([System.BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '')
    }
    finally {
        if ($null -ne $stream) {
            $stream.Dispose()
        }
        $algorithm.Dispose()
    }
}

function Test-ExcludedDirectoryName([string]$Name, [string[]]$Names) {
    foreach ($candidate in $Names) {
        if ($Name.Equals($candidate, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

function Test-ReparsePoint([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        return $false
    }
    $item = Get-Item -LiteralPath $Path -Force
    return (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)
}

function Assert-SafeTargetPath {
    if ([System.String]::Compare($targetRootFull, $expectedTargetRoot, $true, [System.Globalization.CultureInfo]::InvariantCulture) -ne 0) {
        throw "Resolved plugin target is not the exact allowed path: $targetRootFull"
    }
    $samplePrefix = $sampleRootFull + '\'
    if (-not $targetRootFull.StartsWith($samplePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Resolved plugin target escaped the sample root: $targetRootFull"
    }
    $pluginsRoot = Join-Path $sampleRootFull 'Plugins'
    if (Test-ReparsePoint $pluginsRoot) {
        throw "Plugin parent is a reparse point and cannot be modified: $pluginsRoot"
    }
    if (Test-ReparsePoint $targetRootFull) {
        throw "Plugin target is a reparse point and cannot be modified: $targetRootFull"
    }
}

function Get-SourceSnapshot([string]$Root, [string]$DescriptorPath) {
    $hasGit = Test-Path -LiteralPath (Join-Path $Root '.git')
    $head = $null
    $status = $null
    $clean = $null
    if ($hasGit) {
        $head = (git -C $Root rev-parse HEAD).Trim()
        if ($LASTEXITCODE -ne 0) {
            throw "Unable to read the plugin source HEAD: $Root"
        }
        $status = @(git -C $Root status --short --untracked-files=all)
        if ($LASTEXITCODE -ne 0) {
            throw "Unable to read the plugin source status: $Root"
        }
        $clean = ($status.Count -eq 0)
    }
    return [pscustomobject]@{
        hasGit = $hasGit
        head = $head
        status = $status
        clean = $clean
        descriptorSha256 = Get-Sha256 $DescriptorPath
    }
}

function Assert-SourceUnchanged($Before, $After) {
    if ($Before.descriptorSha256 -ne $After.descriptorSha256) {
        throw 'The plugin source descriptor changed during local setup.'
    }
    if ($Before.hasGit -ne $After.hasGit -or $Before.head -ne $After.head) {
        throw 'The plugin source repository HEAD changed during local setup.'
    }
    $beforeStatusText = @($Before.status) -join "`n"
    $afterStatusText = @($After.status) -join "`n"
    if ($beforeStatusText -cne $afterStatusText) {
        throw 'The plugin source repository working tree changed during local setup.'
    }
}

function Copy-SourceTree([string]$Source, [string]$Destination) {
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    $sourceFiles = @(Get-ChildItem -LiteralPath $Source -Recurse -File -Force)
    foreach ($file in $sourceFiles) {
        $relative = Get-RelativePath $Source $file.FullName
        $parts = $relative -split '[\\/]'
        $skip = $false
        foreach ($part in $parts) {
            if (Test-ExcludedDirectoryName $part $excludedSourceDirectories) {
                $skip = $true
                break
            }
        }
        if ($skip) {
            continue
        }
        $destinationFile = Join-Path $Destination $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destinationFile) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destinationFile -Force
    }
}

function Assert-NoExcludedDirectories([string]$Root, [string]$Label) {
    $directories = @(Get-ChildItem -LiteralPath $Root -Recurse -Directory -Force)
    foreach ($directory in $directories) {
        if (Test-ExcludedDirectoryName $directory.Name $excludedSourceDirectories) {
            throw "$Label contains an excluded directory: $($directory.FullName)"
        }
    }
    return [pscustomobject]@{
        label = $Label
        checked = $true
        excludedDirectoriesFound = @()
    }
}

function Get-ContentManifest([string]$Root, [string[]]$ExcludedNames) {
    $entries = @()
    if (-not (Test-Path -LiteralPath $Root)) {
        return $entries
    }
    $files = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Force)
    foreach ($file in $files) {
        $relative = Get-RelativePath $Root $file.FullName
        $parts = $relative -split '[\\/]'
        $skip = $false
        foreach ($part in $parts) {
            if (Test-ExcludedDirectoryName $part $ExcludedNames) {
                $skip = $true
                break
            }
        }
        if (-not $skip) {
            $entries += [pscustomobject]@{
                relativePath = $relative.Replace('\', '/')
                sha256 = Get-Sha256 $file.FullName
                length = $file.Length
            }
        }
    }
    return $entries
}

function New-TargetBackup([string]$Target, [string]$Backup) {
    New-Item -ItemType Directory -Path $Backup -Force | Out-Null
    $entries = Get-ContentManifest $Target $excludedBackupDirectories
    foreach ($entry in $entries) {
        $sourceFile = Join-Path $Target ($entry.relativePath.Replace('/', '\'))
        $backupFile = Join-Path $Backup ($entry.relativePath.Replace('/', '\'))
        New-Item -ItemType Directory -Path (Split-Path -Parent $backupFile) -Force | Out-Null
        Copy-Item -LiteralPath $sourceFile -Destination $backupFile -Force
    }
    $manifestPath = Join-Path (Split-Path -Parent $Backup) 'manifest.json'
    $manifest = [ordered]@{
        createdUtc = (Get-Date).ToUniversalTime().ToString('o')
        sourceTarget = $Target
        backupTarget = $Backup
        excludedDirectories = $excludedBackupDirectories
        files = $entries
    }
    ConvertTo-Json -InputObject $manifest -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    return [pscustomobject]@{
        path = $Backup
        manifestPath = $manifestPath
        fileCount = $entries.Count
        files = $entries
    }
}

function Restore-TargetBackup([string]$Backup, [string]$Target) {
    if (-not (Test-Path -LiteralPath $Backup)) {
        throw "Backup target does not exist: $Backup"
    }
    New-Item -ItemType Directory -Path $Target -Force | Out-Null
    $backupFiles = @(Get-ChildItem -LiteralPath $Backup -Recurse -File -Force)
    foreach ($file in $backupFiles) {
        $relative = Get-RelativePath $Backup $file.FullName
        $targetFile = Join-Path $Target $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $targetFile) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $targetFile -Force
    }
}

function Assert-PaidPluginBoundary {
    $gitRoot = Join-Path $sampleRootFull '.git'
    if (Test-Path -LiteralPath $gitRoot) {
        $null = git -C $sampleRootFull check-ignore --no-index --quiet -- 'Plugins/EditorActorTagDisplay/EditorActorTagDisplay.uplugin'
        if ($LASTEXITCODE -ne 0) {
            throw 'The paid plugin copy is not ignored by the sample repository.'
        }
        $tracked = @(git -C $sampleRootFull ls-files -- 'Plugins/EditorActorTagDisplay')
        if ($LASTEXITCODE -ne 0) {
            throw 'Unable to inspect tracked paid-plugin files.'
        }
        if ($tracked.Count -gt 0) {
            throw 'The paid plugin copy must remain untracked: Plugins/EditorActorTagDisplay'
        }
        return $false
    }

    $gitIgnorePath = Join-Path $sampleRootFull '.gitignore'
    if (-not (Test-Path -LiteralPath $gitIgnorePath)) {
        throw 'Sample is not a Git checkout and has no .gitignore boundary file.'
    }
    $gitIgnoreLines = @(Get-Content -LiteralPath $gitIgnorePath)
    if (-not ($gitIgnoreLines -contains '/Plugins/EditorActorTagDisplay/')) {
        throw 'Sample .gitignore is missing the exact paid-plugin rule: /Plugins/EditorActorTagDisplay/'
    }
    return $false
}

function Remove-AllowedGeneratedDirectory([string]$Path) {
    $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
    $allowed = $false
    foreach ($candidate in $allowedGeneratedPaths) {
        if ([System.String]::Compare($fullPath, $candidate, $true, [System.Globalization.CultureInfo]::InvariantCulture) -eq 0) {
            $allowed = $true
            break
        }
    }
    if (-not $allowed) {
        throw "Refusing to remove a non-allowlisted generated path: $fullPath"
    }
    if (-not $fullPath.StartsWith($sampleRootFull + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Generated path escaped the sample root: $fullPath"
    }
    $existedBefore = Test-Path -LiteralPath $fullPath
    if ($existedBefore) {
        if (Test-ReparsePoint $fullPath) {
            throw "Generated path is a reparse point and cannot be removed: $fullPath"
        }
        [System.IO.Directory]::Delete($fullPath, $true)
    }
    $existsAfter = Test-Path -LiteralPath $fullPath
    if ($existsAfter) {
        throw "Generated path still exists after removal: $fullPath"
    }
    return [pscustomobject]@{
        path = $fullPath
        existedBefore = $existedBefore
        deleted = $existedBefore
        existsAfter = $existsAfter
    }
}

function Invoke-HiddenProcess([string]$FilePath, [string[]]$Arguments, [string]$WorkingDirectory, [string]$StdoutPath, [string]$StderrPath) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $StdoutPath) -Force | Out-Null
    $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -WorkingDirectory $WorkingDirectory -Wait -PassThru -WindowStyle Hidden -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath
    return $process
}

function Get-Descriptor([string]$Path) {
    return (Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json)
}

function Assert-Descriptor([string]$Path, [string]$ExpectedVersion, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Label descriptor not found: $Path"
    }
    $descriptorObject = Get-Descriptor $Path
    if ($descriptorObject.FriendlyName -ne 'Actor Metadata Overlay') {
        throw "$Label descriptor FriendlyName is not Actor Metadata Overlay."
    }
    if ($descriptorObject.EngineVersion -ne $ExpectedVersion) {
        throw "$Label descriptor EngineVersion is '$($descriptorObject.EngineVersion)', expected '$ExpectedVersion'."
    }
    return $descriptorObject
}

function Assert-StagedDescriptorMatchesSource([string]$SourceDescriptorPath, [string]$StagedDescriptorPath) {
    $sourceObject = Get-Descriptor $SourceDescriptorPath
    $stagedObject = Get-Descriptor $StagedDescriptorPath
    $sourceEngineVersion = $sourceObject.EngineVersion
    $stagedEngineVersion = $stagedObject.EngineVersion
    if ($sourceObject.PSObject.Properties['EngineVersion']) {
        $sourceObject.PSObject.Properties.Remove('EngineVersion')
    }
    if ($stagedObject.PSObject.Properties['EngineVersion']) {
        $stagedObject.PSObject.Properties.Remove('EngineVersion')
    }
    $sourceComparable = ConvertTo-Json -InputObject $sourceObject -Depth 100 -Compress
    $stagedComparable = ConvertTo-Json -InputObject $stagedObject -Depth 100 -Compress
    if ($sourceComparable -cne $stagedComparable) {
        throw 'The staged descriptor changed fields other than EngineVersion.'
    }
    return [pscustomobject]@{
        sourceEngineVersion = $sourceEngineVersion
        stagedEngineVersion = $stagedEngineVersion
        otherFieldsMatch = $true
    }
}

function Write-ResultManifest([string]$Status, [string]$ErrorMessage) {
    New-Item -ItemType Directory -Path $reviewRoot -Force | Out-Null
    $actualDescriptorEngineVersion = $null
    if (Test-Path -LiteralPath (Join-Path $targetRootFull 'EditorActorTagDisplay.uplugin')) {
        try {
            $actualDescriptorEngineVersion = (Get-Descriptor (Join-Path $targetRootFull 'EditorActorTagDisplay.uplugin')).EngineVersion
        }
        catch {
            $actualDescriptorEngineVersion = $null
        }
    }
    $manifest = [ordered]@{
        status = $Status
        selectedEngine = $EngineVersion
        expectedDescriptorEngineVersion = $expectedDescriptorEngineVersion
        actualDescriptorEngineVersion = $actualDescriptorEngineVersion
        sourceDescriptorSha256Before = if ($null -ne $sourceSnapshotBefore) { $sourceSnapshotBefore.descriptorSha256 } else { $null }
        sourceDescriptorSha256After = if ($null -ne $sourceSnapshotAfter) { $sourceSnapshotAfter.descriptorSha256 } else { $null }
        sourceHeadBefore = if ($null -ne $sourceSnapshotBefore) { $sourceSnapshotBefore.head } else { $null }
        sourceHeadAfter = if ($null -ne $sourceSnapshotAfter) { $sourceSnapshotAfter.head } else { $null }
        sourceCleanBefore = if ($null -ne $sourceSnapshotBefore) { $sourceSnapshotBefore.clean } else { $null }
        sourceCleanAfter = if ($null -ne $sourceSnapshotAfter) { $sourceSnapshotAfter.clean } else { $null }
        sourceStatusBefore = if ($null -ne $sourceSnapshotBefore) { $sourceSnapshotBefore.status } else { $null }
        sourceStatusAfter = if ($null -ne $sourceSnapshotAfter) { $sourceSnapshotAfter.status } else { $null }
        targetPath = $targetRootFull
        targetDescriptorPath = Join-Path $targetRootFull 'EditorActorTagDisplay.uplugin'
        markerPath = $markerPath
        markerValid = (Test-Path -LiteralPath $markerPath)
        stagingPath = $stagingPath
        backupPath = if ($null -ne $backupInfo) { $backupInfo.path } else { $backupRoot }
        backupManifestPath = if ($null -ne $backupInfo) { $backupInfo.manifestPath } else { $backupManifestPath }
        buildRequested = [bool]$Build
        buildExitCode = $buildExitCode
        projectFilesRequested = [bool]$GenerateProjectFiles
        projectFilesExitCode = $projectFilesExitCode
        replacedTarget = $replacedTarget
        rollbackOccurred = $rollbackOccurred
        rollbackSucceeded = $rollbackSucceeded
        excludedPathChecks = $excludedPathChecks
        buildCleanupChecks = $buildCleanupChecks
        paidPluginTracked = $paidPluginTracked
        error = $ErrorMessage
        generatedUtc = (Get-Date).ToUniversalTime().ToString('o')
    }
    ConvertTo-Json -InputObject $manifest -Depth 20 | Set-Content -LiteralPath $resultPath -Encoding UTF8
    return $manifest
}

try {
    Assert-SafeTargetPath

    if (-not (Test-Path -LiteralPath $sourceDescriptor)) {
        throw "Plugin descriptor not found: $sourceDescriptor"
    }
    $engineBuildTool = Join-Path $engineRoot 'Engine\Build\BatchFiles\Build.bat'
    $unrealBuildToolDll = Join-Path $engineRoot 'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll'
    $dotnetRoot = Join-Path $engineRoot 'Engine\Binaries\ThirdParty\DotNet'
    $projectFilesDotnet = $null
    if (-not (Test-Path -LiteralPath $engineBuildTool)) {
        throw "Unreal Engine $EngineVersion was not found at $engineRoot"
    }
    if ($GenerateProjectFiles) {
        if (-not (Test-Path -LiteralPath $unrealBuildToolDll)) {
            throw "UnrealBuildTool.dll for Unreal Engine $EngineVersion was not found at $unrealBuildToolDll"
        }
        $dotnetCandidates = @(Get-ChildItem -LiteralPath $dotnetRoot -Recurse -File -Filter 'dotnet.exe' -ErrorAction SilentlyContinue)
        if ($dotnetCandidates.Count -eq 0) {
            throw "Bundled .NET runtime for Unreal Engine $EngineVersion was not found under $dotnetRoot"
        }
        $projectFilesDotnet = $dotnetCandidates[0].FullName
    }

    $sourceDescriptorObject = Get-Descriptor $sourceDescriptor
    if ($sourceDescriptorObject.FriendlyName -ne 'Actor Metadata Overlay') {
        throw 'The supplied plugin is not Actor Metadata Overlay.'
    }
    $sourceSnapshotBefore = Get-SourceSnapshot $sourceRootFull $sourceDescriptor
    $paidPluginTracked = Assert-PaidPluginBoundary

    $stagingId = [System.Guid]::NewGuid().ToString('N')
    $stagingPath = Join-Path (Join-Path $setupRoot 'staging') $stagingId
    $stagingTarget = Join-Path $stagingPath 'EditorActorTagDisplay'
    New-Item -ItemType Directory -Path $stagingPath -Force | Out-Null
    Copy-SourceTree $sourceRootFull $stagingTarget

    $stagedDescriptorPath = Join-Path $stagingTarget 'EditorActorTagDisplay.uplugin'
    $stagedDescriptorObject = Get-Descriptor $stagedDescriptorPath
    if ($stagedDescriptorObject.PSObject.Properties['EngineVersion']) {
        $stagedDescriptorObject.EngineVersion = $expectedDescriptorEngineVersion
    }
    else {
        $stagedDescriptorObject | Add-Member -NotePropertyName EngineVersion -NotePropertyValue $expectedDescriptorEngineVersion
    }
    ConvertTo-Json -InputObject $stagedDescriptorObject -Depth 100 | Set-Content -LiteralPath $stagedDescriptorPath -Encoding UTF8
    Assert-Descriptor $stagedDescriptorPath $expectedDescriptorEngineVersion 'Staged' | Out-Null
    Assert-StagedDescriptorMatchesSource $sourceDescriptor $stagedDescriptorPath | Out-Null
    $excludedPathChecks += Assert-NoExcludedDirectories $stagingTarget 'staging'
    $sourceSnapshotAfter = Get-SourceSnapshot $sourceRootFull $sourceDescriptor
    Assert-SourceUnchanged $sourceSnapshotBefore $sourceSnapshotAfter

    if (Test-Path -LiteralPath $targetRootFull) {
        $existingGitDirectories = @(Get-ChildItem -LiteralPath $targetRootFull -Recurse -Directory -Force | Where-Object { $_.Name.Equals('.git', [System.StringComparison]::OrdinalIgnoreCase) })
        if ($existingGitDirectories.Count -gt 0) {
            throw 'The existing paid plugin target contains a .git directory and cannot be replaced.'
        }
        $backupTimestamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfffZ')
        $backupRoot = Join-Path (Join-Path $setupRoot 'backups') (Join-Path $backupTimestamp 'EditorActorTagDisplay')
        $backupInfo = New-TargetBackup $targetRootFull $backupRoot
        $backupManifestPath = $backupInfo.manifestPath
        if (-not (Test-Path -LiteralPath $backupManifestPath)) {
            throw 'Target backup manifest was not created.'
        }
    }

    if (Test-Path -LiteralPath $targetRootFull) {
        $targetRemoved = $true
        [System.IO.Directory]::Delete($targetRootFull, $true)
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $targetRootFull) -Force | Out-Null
    Move-Item -LiteralPath $stagingTarget -Destination $targetRootFull
    $replacedTarget = $true
    Assert-SafeTargetPath
    Assert-Descriptor (Join-Path $targetRootFull 'EditorActorTagDisplay.uplugin') $expectedDescriptorEngineVersion 'Target' | Out-Null
    $excludedPathChecks += Assert-NoExcludedDirectories $targetRootFull 'target before build'

    $marker = [ordered]@{
        schemaVersion = 1
        generatedBy = 'Scripts/Setup-Local.ps1'
        engineVersion = $EngineVersion
        descriptorEngineVersion = $expectedDescriptorEngineVersion
        sourceDescriptorSha256 = $sourceSnapshotBefore.descriptorSha256
        sourceHead = $sourceSnapshotBefore.head
        setupUtc = (Get-Date).ToUniversalTime().ToString('o')
    }
    ConvertTo-Json -InputObject $marker -Depth 5 | Set-Content -LiteralPath $markerPath -Encoding UTF8

    if ($Build -and ($targetRemoved -or $replacedTarget)) {
        foreach ($generatedPath in $allowedGeneratedPaths) {
            $buildCleanupChecks += Remove-AllowedGeneratedDirectory $generatedPath
        }
    }

    if ($GenerateProjectFiles) {
        $projectPath = Join-Path $sampleRootFull 'ActorMetadataOverlaySample.uproject'
        $projectStdout = Join-Path $logRoot "setup-$EngineVersion-projectfiles.stdout.log"
        $projectStderr = Join-Path $logRoot "setup-$EngineVersion-projectfiles.stderr.log"
        $projectUbtLog = Join-Path $logRoot "setup-$EngineVersion-projectfiles.ubt.log"
        $projectArguments = @(
            ('"{0}"' -f $unrealBuildToolDll),
            '-projectfiles',
            ('-project="{0}"' -f $projectPath),
            '-game',
            '-engine',
            '-progress',
            ('-log="{0}"' -f $projectUbtLog)
        )
        $projectWorkingDirectory = Join-Path $engineRoot 'Engine\Source'
        $projectProcess = Invoke-HiddenProcess $projectFilesDotnet $projectArguments $projectWorkingDirectory $projectStdout $projectStderr
        $projectFilesExitCode = $projectProcess.ExitCode
        if ($projectFilesExitCode -ne 0) {
            throw "Project file generation failed with exit code $projectFilesExitCode. See $projectStdout and $projectStderr"
        }
    }

    if ($Build) {
        $projectPath = Join-Path $sampleRootFull 'ActorMetadataOverlaySample.uproject'
        $buildStdout = Join-Path $logRoot "setup-$EngineVersion-build.stdout.log"
        $buildStderr = Join-Path $logRoot "setup-$EngineVersion-build.stderr.log"
        $buildArguments = @(
            'ActorMetadataOverlaySampleEditor',
            'Win64',
            'Development',
            ('"{0}"' -f $projectPath),
            '-WaitMutex',
            '-NoHotReloadFromIDE'
        )
        $buildProcess = Invoke-HiddenProcess $engineBuildTool $buildArguments $sampleRootFull $buildStdout $buildStderr
        $buildExitCode = $buildProcess.ExitCode
        if ($buildExitCode -ne 0) {
            throw "Sample build failed with exit code $buildExitCode. See $buildStdout and $buildStderr"
        }
    }

    $sourceSnapshotAfter = Get-SourceSnapshot $sourceRootFull $sourceDescriptor
    Assert-SourceUnchanged $sourceSnapshotBefore $sourceSnapshotAfter
    $resultStatus = 'Succeeded'
    $resultManifest = Write-ResultManifest $resultStatus $null
    ConvertTo-Json -InputObject $resultManifest -Depth 20
}
catch {
    $resultError = $_.Exception.Message
    if ($Build -and ($targetRemoved -or $replacedTarget)) {
        try {
            foreach ($generatedPath in $allowedGeneratedPaths) {
                $buildCleanupChecks += Remove-AllowedGeneratedDirectory $generatedPath
            }
        }
        catch {
            $resultError = "$resultError Cleanup failed: $($_.Exception.Message)"
        }
    }
    if ($targetRemoved -or $replacedTarget) {
        $rollbackOccurred = $true
        try {
            if (Test-Path -LiteralPath $targetRootFull) {
                Assert-SafeTargetPath
                [System.IO.Directory]::Delete($targetRootFull, $true)
            }
            if ($null -ne $backupInfo) {
                Restore-TargetBackup $backupInfo.path $targetRootFull
            }
            $rollbackSucceeded = $true
        }
        catch {
            $resultError = "$resultError Rollback failed: $($_.Exception.Message)"
        }
    }
    try {
        $sourceSnapshotAfter = Get-SourceSnapshot $sourceRootFull $sourceDescriptor
    }
    catch {
        $sourceSnapshotAfter = $null
    }
    $resultManifest = Write-ResultManifest 'Failed' $resultError
    ConvertTo-Json -InputObject $resultManifest -Depth 20
    throw $resultError
}
finally {
    if ($null -ne $stagingPath -and (Test-Path -LiteralPath $stagingPath)) {
        [System.IO.Directory]::Delete($stagingPath, $true)
    }
}

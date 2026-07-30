[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$VerificationRoot,
    [string]$CaptureProjectRoot,
    [string]$ProtectedSentinelPath,
    [string]$ProtectedSentinelSha256
)

$ErrorActionPreference = 'Stop'
$sha256 = [System.Security.Cryptography.SHA256]::Create()
function Get-Hash([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        return ([System.BitConverter]::ToString($sha256.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
    } finally {
        $stream.Dispose()
    }
}
$root = (Resolve-Path $VerificationRoot).Path
$planPath = Join-Path $root 'sync-plan.json'
$resultPath = Join-Path $root 'sync-result.json'
if (-not (Test-Path -LiteralPath $planPath) -or -not (Test-Path -LiteralPath $resultPath)) { throw 'Sync plan/result is missing.' }
$plan = Get-Content -LiteralPath $planPath -Raw | ConvertFrom-Json
$result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
if (-not $result.success) { throw 'Sync result reports failure.' }
$allowlist = @('Plugins/ActorMetadataOverlayDemoFixtures/','Demo/demo-spec.json','Config/Tags/ActorMetadataOverlaySampleTags.ini','Scripts/Capture/Apply-DemoSpec.py')
foreach ($operation in @($plan.operations)) {
    $path = $operation.RelativePath.Replace('\', '/')
    if (-not ($allowlist | Where-Object { $path -eq $_ -or $path.StartsWith($_) })) { throw "Plan contains an out-of-allowlist path: $path" }
}
if ($result.applied -and $result.changedCount -gt 0 -and -not (Test-Path -LiteralPath (Join-Path $root 'backup'))) { throw 'Applied sync has no backup directory.' }
if ($ProtectedSentinelPath) {
    if (-not (Test-Path -LiteralPath $ProtectedSentinelPath -PathType Leaf)) { throw "Protected sentinel disappeared: $ProtectedSentinelPath" }
    $actual = Get-Hash $ProtectedSentinelPath
    if ($actual -ne $ProtectedSentinelSha256.ToLowerInvariant()) { throw 'Protected sentinel hash changed.' }
}
$sha256.Dispose()
[pscustomobject]@{Pass=$true; Plan=$planPath; Result=$resultPath; Applied=$result.applied; ChangedCount=$result.changedCount} | ConvertTo-Json -Depth 8

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$PackageDir = $PSScriptRoot
$SourceDir = Join-Path $PackageDir "gh-pr-review"

if (-not (Test-Path (Join-Path $SourceDir "SKILL.md") -PathType Leaf)) {
    Write-Error "Cannot find skill source at $SourceDir"
    exit 1
}

if ($env:CODEX_HOME) {
    $CodexHome = $env:CODEX_HOME
} else {
    $CodexHome = Join-Path $HOME ".codex"
}

$SkillsDir = Join-Path $CodexHome "skills"
$TargetDir = Join-Path $SkillsDir "gh-pr-review"

New-Item -ItemType Directory -Force -Path $SkillsDir | Out-Null

if (Test-Path $TargetDir) {
    $Timestamp = Get-Date -Format "yyyyMMddHHmmss"
    $BackupDir = "$TargetDir.backup.$Timestamp"
    Move-Item -Path $TargetDir -Destination $BackupDir
    Write-Host "Backed up existing skill to: $BackupDir"
}

Copy-Item -Path $SourceDir -Destination $TargetDir -Recurse

Write-Host "Installed gh-pr-review skill to: $TargetDir"
Write-Host 'Open a new Codex thread, then invoke it with: Use $gh-pr-review to review the current PR.'

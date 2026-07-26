<#
.SYNOPSIS
    Point this clone's git hooks at the in-repo .githooks directory.

.DESCRIPTION
    Git does not version .git/hooks, so a hook committed to the repo does nothing until
    core.hooksPath is set. This script sets it. Run once per clone (and once per worktree
    is NOT needed - core.hooksPath is per-repository and worktrees share it).

    Installed hooks:
      pre-push  - blocks a push that would land stale generated artifacts
                  (WebSpa.h, bridge codec, web JSON contract, design tokens).

.EXAMPLE
    tools\install-hooks.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (git rev-parse --show-toplevel 2>$null)
if (-not $repoRoot) { throw "Not inside a git repository." }
$repoRoot = $repoRoot -replace '/', '\'

git config core.hooksPath .githooks
Write-Host "core.hooksPath -> .githooks" -ForegroundColor Green

$hook = Join-Path $repoRoot '.githooks\pre-push'
if (Test-Path $hook) {
    Write-Host "installed: pre-push (generated-artifact gate)" -ForegroundColor Green
} else {
    Write-Warning "expected hook not found: $hook"
}

Write-Host ""
Write-Host "Verify with: python code\scripts\check_generated.py" -ForegroundColor Cyan
Write-Host "Bypass once: git push --no-verify" -ForegroundColor Cyan

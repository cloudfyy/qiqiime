# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script = Join-Path $repoRoot 'scripts\release_notes.ps1'
$directory = Join-Path ([IO.Path]::GetTempPath()) ('QiQiReleaseNotes-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($directory)
$notesPath = Join-Path $directory 'notes.md'
$outputPath = Join-Path $directory 'body.md'
try {
    [IO.File]::WriteAllText($notesPath, "# QiQiIME 2.0.0 Patch`n`nCurrent changes.`n`n---`n`n# QiQiIME 1.0.9 Patch`n`nOld changes.`n")
    $parameters = @{
        NotesPath = $notesPath
        Version = '2.0.0'
        Tag = 'v2.0.0-rc.1'
        Repository = 'cloudfyy/qiqiime'
        AssetNames = @('QiQiIME-v2.0.0-win-x64-zh-CN.msi', 'QiQiIME-v2.0.0-win-x64-x86.zip')
        OutputPath = $outputPath
    }
    & $script @parameters
    $body = [IO.File]::ReadAllText($outputPath)
    if (-not $body.StartsWith('## Downloads') -or -not $body.Contains('Current changes.') -or
        $body.Contains('Old changes.') -or $body.Contains('# QiQiIME 1.0.9') -or
        $body -match '(?m)^---\s*$') {
        throw 'Release body must put downloads first and include only the selected version.'
    }
    foreach ($asset in $parameters.AssetNames) {
        if (-not $body.Contains("https://github.com/cloudfyy/qiqiime/releases/download/v2.0.0-rc.1/$asset")) {
            throw "Missing exact tagged asset URL: $asset"
        }
    }
    if (-not $body.Contains('/blob/v2.0.0-rc.1/docs/release-notes.md')) {
        throw 'Missing tag-specific full history link.'
    }
    $parameters.Version = '3.0.0'
    $rejected = $false
    try { & $script @parameters } catch {
        if ($_.Exception.Message -notlike 'Expected exactly one release-notes section*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'A missing version must fail instead of publishing unrelated notes.' }
    $parameters.Version = '2.0.0'
    [IO.File]::AppendAllText($notesPath, "`n# QiQiIME 2.0.0 Duplicate`n")
    $rejected = $false
    try { & $script @parameters } catch {
        if ($_.Exception.Message -notlike 'Expected exactly one release-notes section*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'Duplicate version sections must be rejected.' }
    Write-Host '[PASS] Current-version release notes, download links, and missing/duplicate section checks.'
} finally {
    Remove-Item -LiteralPath $notesPath -Force -ErrorAction Stop
    if (Test-Path -LiteralPath $outputPath) { Remove-Item -LiteralPath $outputPath -Force -ErrorAction Stop }
    [IO.Directory]::Delete($directory)
}

# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,

    [Parameter(Mandatory = $true)]
    [string]$Configuration
)

$ErrorActionPreference = 'Stop'

$buildArguments = @(
    '--build', $BuildDirectory,
    '--config', $Configuration,
    '--target', 'ALL_BUILD',
    '--',
    '/t:Rebuild',
    '/p:RunCodeAnalysis=true',
    '/p:CodeAnalysisRuleSet=CppCoreCheckRules.ruleset'
)

Write-Host 'Running MSVC CppCoreCheck (C++ Core Guidelines and GSL rules)...'
$buildOutput = @(& cmake @buildArguments 2>&1)
$buildExitCode = $LASTEXITCODE
if ($buildExitCode -ne 0) {
    $buildOutput | Select-Object -Last 80 | ForEach-Object { Write-Output $_ }
    throw "CppCoreCheck build failed with exit code $buildExitCode."
}

$cppWarnings = @(
    $buildOutput |
        Where-Object {
            "$_" -match '(?i)[\\/](?:src|tests)[\\/].*warning C\d+:' -and
            "$_" -notmatch 'C26490'
        } |
        Sort-Object -Unique
)
$baselineWarningCount = 0
if ($cppWarnings.Count -gt $baselineWarningCount) {
    Write-Host 'CppCoreCheck findings exceed the reviewed baseline:' -ForegroundColor Red
    $cppWarnings | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }
    throw "CppCoreCheck reported $($cppWarnings.Count) unique warnings; baseline is $baselineWarningCount."
}
Write-Host "CppCoreCheck reported $($cppWarnings.Count) unique warning(s); baseline is $baselineWarningCount."
if ($cppWarnings.Count -gt 0) {
    $cppWarnings |
        ForEach-Object { if ("$_" -match 'warning (C\d+):') { $Matches[1] } } |
        Group-Object |
        Sort-Object Count -Descending |
        ForEach-Object { Write-Host ("  {0}: {1}" -f $_.Name, $_.Count) }
}
Write-Host '[PASS] CppCoreCheck findings are within the reviewed baseline.' -ForegroundColor Green

$scriptPaths = @($PSScriptRoot, (Join-Path (Split-Path -Parent $PSScriptRoot) 'tests'))
if (-not (Get-Command Invoke-ScriptAnalyzer -ErrorAction SilentlyContinue)) {
    Write-Host '[WARN] PSScriptAnalyzer is not installed; verifying PowerShell AST syntax instead.' -ForegroundColor Yellow
    foreach ($psFile in Get-ChildItem -Path $scriptPaths -Filter '*.ps1' -File -Recurse) {
        $tokens = $null
        $errors = $null
        [void][System.Management.Automation.Language.Parser]::ParseFile($psFile.FullName, [ref]$tokens, [ref]$errors)
        if ($errors -and $errors.Count -gt 0) {
            $errors | Format-Table -AutoSize | Out-String | Write-Host
            throw "PowerShell syntax error in $($psFile.FullName)."
        }
    }
    Write-Host '[PASS] Zero PowerShell syntax errors in scripts/ and tests/.' -ForegroundColor Green
    exit 0
}

Write-Host 'Running PSScriptAnalyzer on scripts/ and tests/...'
$scriptIssues = @(
    foreach ($scriptPath in $scriptPaths) {
        Invoke-ScriptAnalyzer -Path $scriptPath -Recurse -Severity Error
    }
)
if ($scriptIssues.Count -gt 0) {
    $scriptIssues | Format-Table -AutoSize | Out-String | Write-Host
    throw "PSScriptAnalyzer reported $($scriptIssues.Count) error(s)."
}
Write-Host '[PASS] Zero PSScriptAnalyzer errors in scripts/ and tests/.' -ForegroundColor Green

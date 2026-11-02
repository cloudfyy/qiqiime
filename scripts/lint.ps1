[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,

    [Parameter(Mandatory = $true)]
    [string]$Configuration
)

$ErrorActionPreference = 'Stop'

$targets = @(
    'qiqiime'
    'QiQiIMESettings'
    'QiQiIMELogMonitor'
    'qiqi_common_test'
    'qiqi_engine_test'
    'qiqi_tsf_ui_test'
    'qiqi_settings_test'
    'qiqi_log_monitor_test'
)
$buildArguments = @(
    '--build', $BuildDirectory,
    '--config', $Configuration,
    '--target'
) + $targets + @(
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
$baselineWarningCount = 435
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

if (-not (Get-Command Invoke-ScriptAnalyzer -ErrorAction SilentlyContinue)) {
    throw 'PSScriptAnalyzer is required. Install it with: Install-Module PSScriptAnalyzer -Scope CurrentUser'
}

Write-Host 'Running PSScriptAnalyzer on scripts/...'
$scriptIssues = @(Invoke-ScriptAnalyzer -Path (Join-Path $PSScriptRoot '.') -Severity Error)
if ($scriptIssues.Count -gt 0) {
    $scriptIssues | Format-Table -AutoSize | Out-String | Write-Host
    throw "PSScriptAnalyzer reported $($scriptIssues.Count) error(s)."
}
Write-Host '[PASS] Zero PSScriptAnalyzer errors in scripts/.' -ForegroundColor Green

# Looks for what a tool has not got yet. The build list, the test list and the GNU comparison are made from
# the files, so what can be forgotten is the text: a row in the command tables of README.md and README.en.md,
# a line of the directory tree, a test that runs the tool.
#
# Exit status 1 when something is missing (a hint is printed), 0 otherwise. Missing GNU comparisons are
# only mentioned: not every tool has a GNU counterpart that can be compared.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$tools = @(Get-ChildItem -Directory | Where-Object { (Test-Path (Join-Path $_.FullName 'Makefile')) -and ($_.Name -ne 'test') } |
           ForEach-Object { $_.Name } | Sort-Object)
$readmes = @{}
foreach ($r in 'README.md', 'README.en.md') {
    $readmes[$r] = [System.IO.File]::ReadAllText((Join-Path $root $r), [System.Text.Encoding]::UTF8)
}
$testSources = (Get-ChildItem test -Filter *.c | ForEach-Object { [System.IO.File]::ReadAllText($_.FullName) }) -join "`n"

$missing = 0
$notes = 0
foreach ($tool in $tools) {
    foreach ($r in $readmes.Keys) {
        if ($readmes[$r] -notmatch ('(?m)^\| `' + $tool + '` \|')) {
            Write-Host "MISSING  $tool : no row in the command table of $r"
            $missing++
        }
        if ($readmes[$r] -notmatch ('(?m)^├─ ' + $tool + '/ ')) {
            Write-Host "MISSING  $tool : no line in the directory tree of $r (scripts\sync-readme.ps1 adds it)"
            $missing++
        }
    }
    if ($tool -ne 'shell' -and $testSources -notmatch ('"' + [regex]::Escape($tool) + '"')) {
        Write-Host "MISSING  $tool : no test in test\*.c runs it"
        $missing++
    }
    if (-not (Test-Path "test\gnu\$tool.sh")) {
        Write-Host "note     $tool : no GNU comparison (test\gnu\$tool.sh)"
        $notes++
    }
}
Write-Host "$($tools.Count) tools, $missing missing, $notes without a GNU comparison"
exit ([int]($missing -gt 0))

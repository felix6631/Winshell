# Brings the parts of the READMEs that only list the tools in line with the directories:
#   - the sentence of the build structure that names the sub-projects
#   - the directory tree (a missing tool gets a line "<name>/  <name> command" in front of common/)
# What describes a tool (the command table) is written by hand; nmake check-registry looks for gaps.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$tools = @(Get-ChildItem -Directory | Where-Object { (Test-Path (Join-Path $_.FullName 'Makefile')) -and ($_.Name -ne 'test') } |
           ForEach-Object { $_.Name } | Sort-Object)
$ordered = @('shell') + @($tools | Where-Object { $_ -ne 'shell' })
$names = ($ordered | ForEach-Object { '`' + $_ + '`' }) -join ', '

$utf8 = New-Object System.Text.UTF8Encoding($false)
foreach ($readme in 'README.md', 'README.en.md') {
    $path = Join-Path $root $readme
    $text = [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8) -replace "`r`n", "`n"
    $text = [regex]::Replace($text, '\(`shell`(, `[a-z0-9_]+`)+\)', "($names)", 1)

    $lines = [System.Collections.Generic.List[string]]($text -split "`n")
    $commonAt = -1
    $treeLines = @{}
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^├─ common/') { $commonAt = $i }
        if ($lines[$i] -match '^├─ ([a-z0-9_]+)/ ') { $treeLines[$Matches[1]] = $true }
    }
    if ($commonAt -ge 0) {
        $word = if ($readme -eq 'README.md') { '명령' } else { 'command' }
        foreach ($tool in $ordered) {
            if ($tool -eq 'shell' -or $treeLines.ContainsKey($tool)) { continue }
            $entry = ('├─ ' + $tool + '/').PadRight(17) + $tool + ' ' + $word
            $lines.Insert($commonAt, $entry)
            $commonAt++
        }
    }
    [System.IO.File]::WriteAllText($path, ($lines -join "`n"), $utf8)
}

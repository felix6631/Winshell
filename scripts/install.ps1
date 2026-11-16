# Adds the bin directory of Winshell to the PATH of the user (or takes it out with -Remove) and prints the path of
# shell.exe. Nothing is changed outside the PATH of the user; open a new terminal for it to count.
#
#   powershell -File scripts\install.ps1
#   powershell -File scripts\install.ps1 -Remove
param([switch]$Remove)

$bin = Join-Path (Split-Path -Parent $PSScriptRoot) 'bin'
if (-not (Test-Path (Join-Path $bin 'shell.exe'))) {
    Write-Error "no shell.exe in $bin (build with nmake first, or unpack the release zip into bin)"
    exit 1
}

$current = [Environment]::GetEnvironmentVariable('Path', 'User')
if ($null -eq $current) { $current = '' }
$parts = @($current -split ';' | Where-Object { $_ -ne '' })
$same = { param($a, $b) [string]::Equals($a.TrimEnd('\'), $b.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase) }

if ($Remove) {
    $kept = @($parts | Where-Object { -not (& $same $_ $bin) })
    [Environment]::SetEnvironmentVariable('Path', ($kept -join ';'), 'User')
    Write-Host "removed $bin from the PATH of the user"
    exit 0
}

if (-not ($parts | Where-Object { & $same $_ $bin })) {
    [Environment]::SetEnvironmentVariable('Path', (($parts + $bin) -join ';'), 'User')
    Write-Host "added $bin to the PATH of the user (open a new terminal)"
} else {
    Write-Host "$bin is already in the PATH of the user"
}
Write-Host "shell: $(Join-Path $bin 'shell.exe')"

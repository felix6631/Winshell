# Creates the skeleton of a new command:
#
#   <name>\main.c, <name>\Makefile       a runnable placeholder (copies its files), built by nmake at once
#   test\<name>_test.c                   the test file (found by test\Makefile without an entry)
#   test\gnu\<name>.sh                   the comparison with GNU (found by gnu_compare.sh without an entry)
#   README.md, README.en.md              the rows of the command table, test table and directory tree
#
# Nothing else has to be edited: the build list and the test list are made from the directories.
# Usage:  nmake new NAME=<name>   (or: powershell -File scripts\new-command.ps1 <name>)

param([Parameter(Mandatory = $true)][string]$Name)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if ($Name -notmatch '^[a-z][a-z0-9_]*$') { throw "the name has to be lower case letters, digits and _: '$Name'" }
if (Test-Path $Name) { throw "$Name already exists" }

function Write-Lf([string]$Path, [string]$Text) {
    # UTF-8 without a BOM and LF line ends, as the repository has them
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText((Join-Path $root $Path), ($Text -replace "`r`n", "`n"), $utf8)
}

$main = @'
/*
 * @NAME@: TODO what it does.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "textutil.h"

#define VERSION "0.1"
#define PROGRAM "@NAME@"

static void print_usage(void) {
    puts("Usage: @NAME@ [OPTION]... [FILE]...\n"
         "TODO describe what it does.\n"
         "\n"
         "  -x, --example   TODO an option\n"
         "      --help      display this help and exit\n"
         "      --version   output version information and exit\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.");
}

enum { OPT_EXAMPLE = 1 };

static const ArgsOption options[] = {
    { OPT_EXAMPLE, 'x', "example", ARGS_NONE },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    int example;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    (void)value;
    if (id == OPT_EXAMPLE)
        o->example = 1;
    return 0;
}

int main(int argc, char* argv[]) {
    Options options_given;
    const char** operands;
    int count, result, i, status = 0;
    char buffer[65536];

    memset(&options_given, 0, sizeof options_given);
    result = args_parse(&spec, argc, argv, on_option, &options_given, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    count = (int)text_default_operands(operands, (size_t)count);

    text_binary_streams();
    for (i = 0; i < count; i++) {
        FILE* in;
        long got;

        if (text_open(PROGRAM, operands[i], 1, &in) != 0) {
            status = 1;
            continue;
        }
        while ((got = text_read_some(in, buffer, sizeof buffer)) > 0)
            fwrite(buffer, 1, (size_t)got, stdout);
        if (in != stdin)
            fclose(in);
    }
    return fflush(stdout) != 0 ? 1 : status;
}
'@

$makefile = @'
## @NAME@ 빌드 규칙 (NMAKE / MSVC). 보통은 최상위 Makefile을 통해 호출됩니다.

NAME     = @NAME@
SRCS     = main.c ..\common\args.c ..\common\vpath.c ..\common\fsutil.c ..\common\textutil.c
HDRS     = ..\common\args.h ..\common\vpath.h ..\common\fsutil.h ..\common\textutil.h

!INCLUDE ..\common.mk
'@

$test = @'
/*
 * Tests of @NAME@, run as the program it is.
 * Needs the programs in <repository>\bin (build with nmake first).
 */

#include "testtools.h"

static void expect(const char* arguments, const char* input, const char* expected, int status) {
    expect_run("@NAME@", arguments, input, expected, status);
}

int main(void) {
    tools_init("@NAME@_test");

    printf("@NAME@\n");
    expect("", "a\nb\n", "a\nb\n", 0);                  /* TODO: the real behaviour */

    return test_summary("@NAME@_test");
}
'@

$gnu = @'
# GNU comparison of @NAME@: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
for f in $TEXTS; do
    compare_stdin "$f" @NAME@
done
compare @NAME@ --bogus
'@

New-Item -ItemType Directory -Path $Name | Out-Null
Write-Lf "$Name\main.c"        $main.Replace('@NAME@', $Name)
Write-Lf "$Name\Makefile"      $makefile.Replace('@NAME@', $Name)
Write-Lf "test\${Name}_test.c" $test.Replace('@NAME@', $Name)
Write-Lf "test\gnu\$Name.sh"   $gnu.Replace('@NAME@', $Name)

# the README rows: the command table and the test table (after their last rows), the directory tree
function Add-After([string]$Path, [string]$LinePattern, [string]$NewLine) {
    $lines = [System.IO.File]::ReadAllLines((Join-Path $root $Path), [System.Text.Encoding]::UTF8)
    $last = -1
    for ($i = 0; $i -lt $lines.Length; $i++) { if ($lines[$i] -match $LinePattern) { $last = $i } }
    if ($last -lt 0) { Write-Warning "$Path has no line like $LinePattern"; return }
    $out = @($lines[0..$last]) + $NewLine + @($lines[($last + 1)..($lines.Length - 1)])
    Write-Lf $Path (($out -join "`n") + "`n")
}
foreach ($readme in 'README.md', 'README.en.md') {
    $state = if ($readme -eq 'README.md') { '구현 중 (뼈대)' } else { 'In progress (skeleton)' }
    $testText = if ($readme -eq 'README.md') { 'TODO' } else { 'TODO' }
    Add-After $readme '^\| `[a-z0-9_]+` \| (완료|Done|입력 루프|Input loop)' "| ``$Name`` | $state |"
    Add-After $readme '^\| `[a-z0-9_]+_test` \|' "| ``${Name}_test`` | ``$Name``: $testText |"
}
& "$PSScriptRoot\sync-readme.ps1"

Write-Host "created $Name\ and test\${Name}_test.c, test\gnu\$Name.sh; nmake builds and tests them as they are."

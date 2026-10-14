# Winshell

[English](README.en.md) | **한국어**

Winshell은 리눅스 스타일의 쉘과 coreutils를 Windows에서 사용하기 위한 프로젝트이자 툴체인입니다.
순수 C와 Win32 API만으로 작성하며, 최종 목표는 Windows XP까지 동작하는 것입니다.

> **상태: 개발 초기.** 쉘은 한 줄을 읽어 `bin\<명령>.exe`를 실행하는 수준이며,
> 백그라운드 실행(`&`), 제어문, 쉘 함수 등은 아직 구현되지 않았습니다.

## 구현 현황

| 명령 | 상태 |
| --- | --- |
| `shell` | 입력 루프, 명령 실행(종료 대기), 파이프/리다이렉션, `; && \|\|`, 변수 확장(`$? $0 $1..$9 $# $@ $*`), 명령 치환(`$(...)`, 백틱), 주석(`#`), 글롭, 스크립트 실행(`shell FILE [ARG...]`, `shell -c`, `source`/`.`), `cd` `export` `unset` `exit` `source` |
| `arch` | 완료 |
| `basename` | 완료 (`-a -s -z`; 구분자는 `/` 와 `\`, 접미사는 이름 전체가 아닐 때만 제거) |
| `date` | 완료 (`-d -u -r -I -R --rfc-3339`, `+FORMAT` 의 `%a %A %b %B %c %C %d %D %e %F %g %G %H %I %j %k %l %m %M %N %p %P %q %r %R %s %S %T %u %U %V %w %W %x %X %y %Y %z %:z %Z` 와 플래그 `- _ 0 ^ #` 및 너비; `-d` 는 `@초`, ISO 날짜와 시각, 월 이름, `now yesterday tomorrow`, `+2 days`, `3 weeks ago`, `next month`, 요일을 읽음; 1970 이전과 3000년 이후도 계산; 시계 설정 `-s` 는 미지원) |
| `diff` | 완료 (일반 / `-u -U N` / `-c -C N` / `-NUM`, `-q -s -i -w -b -B -a -r -N -x --label --strip-trailing-cr`; GNU diff 와 같은 알고리즘(Myers, 중간 스네이크; 끝의 공통 줄, 이동, 큰 입력의 휴리스틱)이라 같은 결과를 고름; 디렉터리 비교; 종료 상태 0/1/2; 빠진 것: `-y -e -p --color` 등) |
| `dirname` | 완료 (`-z`; 여러 이름) |
| `cat` | 완료 (`-A -b -e -E -n -s -t -T -u -v`, `--help`, `--version`) |
| `comm` | 완료 (`-1 -2 -3`, `--check-order`, `--nocheck-order`, `--output-delimiter`, `--total`, `-z`; 바이트 비교(C 로캘), CR LF 는 줄 끝으로 보고 읽은 그대로 씀; 정렬 순서 위반은 GNU 처럼 한 번 알리고 상태 1) |
| `cp` | 완료 (`-a -f -i -n -p -r -R -t -v`) |
| `cut` | 완료 (`-b -c -f -d -s --complement --output-delimiter`) |
| `echo` | 완료 (`-n`, `-e`, `-E`, `--help`, `--version`) |
| `expr` | 완료 (`| & < <= = == != >= > + - * / % :`, `match substr index length +`, `( )`; 정수는 크기 제한 없음, 비교는 둘 다 정수면 수치 비교, UTF-8 문자 단위, 종료 상태 0/1/2/3) |
| `realpath` | 완료 (`-e -m -s -q -z --relative-to --relative-base`; 마지막 구성요소만 없어도 되는 기본 규칙, 정션과 심볼릭 링크를 따라감, 가상 경로 `/c/...` 로 출력) |
| `sed` | 완료 (`-n -e -f -E -r -i[SUFFIX] -s -z -l -u --sandbox`; 명령 `s y p P d D n N g G h H x l = a i c r R w W q Q b t T z F : { }`, 주소 `N $ /re/ first~step addr,+N addr,~N 0,/re/ !`, `s` 플래그 `g p N I w`, `\1`..`\9 & \U \L \u \l \E`; 줄 끝 CRLF 유지, 끝 줄바꿈 없는 마지막 줄 유지, UTF-8 문자 단위; `e` 명령과 `M` 플래그는 미지원) |
| `find` | 완료 (`-H -L -P`, 연산자 `( ) ! -a -o ,`, 옵션 `-maxdepth -mindepth -depth -follow`, 검사 `-name -iname -path -ipath -regex -iregex -regextype -type -xtype -size -empty -newer -mtime -atime -ctime -mmin -amin -cmin -readable -writable -executable`, 동작 `-print -print0 -printf -delete -prune -quit -exec -execdir -ok`(`;` 와 `+`); `-perm -user -group -links -inum -ls -fprint` 는 미지원) |
| `which` | 완료 (`-a -s`; 프로젝트의 `bin` 을 먼저, 그다음 `PATH` 의 디렉터리를 `PATHEXT` 확장자와 함께 찾음; 경로가 있는 이름은 그대로 확인; 가상 경로로 출력; 종료 상태 0/1/2, 이름 없이 호출하면 255) |
| `xargs` | 완료 (`-0 -a -d -E -e -I -i -L -l -n -P -p -r -s -t -x --show-limits`; 따옴표와 역슬래시 해석, 줄 끝 공백으로 이어지는 줄, 명령줄 길이 제한 30000자, `-P` 병렬 실행; 종료 상태 0/123/124/126/127/1) |
| `grep` | 완료 (`-E -F -G -e -f -i -v -w -x -c -l -L -m -n -H -h -o -q -s -b -r -A -B -C -NUM -a -I -z -Z --include --exclude --exclude-dir`; 기본/확장 정규식(역참조 포함), 가장 왼쪽의 가장 긴 일치, UTF-8 문자 단위; `-P` 와 색은 미지원) |
| `head` | 완료 (`-n -c -q -v -NUM`, `-n -K` 는 끝의 K줄만 빼고) |
| `ls` | 완료 (`-a -A -C -d -F -h -l -p -r -R -S -t -U -1`, `/` 는 드라이브 목록) |
| `kill` | 완료 (`-s -n -SIGNAL -l -L`; Windows 에는 시그널이 없어 0 은 확인만, STOP/TSTP/CONT 는 중지/재개, KILL 은 즉시 종료(상태 137), CHLD/URG/WINCH 는 무시, 나머지는 창에 WM_CLOSE 를 보내고 창이 없으면 종료(상태 128+번호); 프로세스 그룹은 지원하지 않음) |
| `mkdir` | 완료 (`-p -v -m`; `-m` 은 받기만 함) |
| `mv` | 완료 (`-f -i -n -t -v`, 다른 드라이브로는 복사 후 삭제) |
| `env` | 완료 (`-i`, `-u`, `-0`, `-C`, `NAME=VALUE`, 명령 실행; 명령은 `env.exe` 의 디렉터리나 가상 경로에서 찾음) |
| `printenv` | 완료 (`-0`, 이름 지정, 종료 상태 1) |
| `ps` | 완료 (선택 `-e -A -a -x -p --ppid -u -C -N`, BSD 형식 `aux`; 형식 `-f u -o NAME=HEADING`, `--sort`, `--no-headers`; 기본은 같은 콘솔의 프로세스, 열은 pid ppid user comm args time etime start tty nlwp rss vsz pcpu pmem c ni; 알 수 없는 값은 `-`/`?`; 선택된 것이 없으면 상태 1) |
| `pwd` | 완료 (`-L`, `-P`, 가상 경로로 출력) |
| `rm` | 완료 (`-f -i -r -R -d -v`, `/`, 드라이브, 공유의 루트는 `--no-preserve-root` 로도 삭제 불가) |
| `rmdir` | 완료 (`-p -v --ignore-fail-on-non-empty`) |
| `sleep` | 완료 (`NUMBER[s|m|h|d]...` 합산, 소수 허용) |
| `sort` | 완료 (`-b -d -f -g -i -n -r -s -u -c -C -k -t -o`, 바이트 순서(C 로캘), `-h -M -V -z` 는 미지원) |
| `tac` | 완료 (`-b -s`, CRLF 유지; `-r` 은 미지원) |
| `tail` | 완료 (`-n -c -f -s -q -v -NUM`, `-n +K`) |
| `tee` | 완료 (`-a`, 파일마다 오류를 보고하고 계속, 입력이 오는 대로 전달) |
| `nl` | 완료 (`-b -h -f`의 `a t n`, `-n -w -s -v -i -l -p -d`; `p` 정규식 스타일은 미지원) |
| `paste` | 완료 (`-d -s -z`, `- -` 로 표준 입력을 번갈아 읽음) |
| `touch` | 완료 (`-a -c -d -m -r -t`; `-d` 는 `date -d` 와 같은 날짜 해석기(`common/datetime.c`)를 씀: `@초`, ISO 날짜와 시각, 월 이름, `yesterday`, `3 days ago`, 시간대) |
| `test_cmd` | 완료 (`test` 와 `[` 로 설치됨: 문자열 `= == != < >`, 정수 `-eq -ne -lt -le -gt -ge`, 파일 `-e -f -d -s -r -w -x -L -h -c -nt -ot -ef -O -G -N -t`, `! ( ) -a -o`; GNU 처럼 인자 1~4개는 개수로 해석; `[` 는 `]` 를 요구; 종료 상태 0/1/2; `-r -w -x` 는 Windows 에서 열 수 있음/읽기 전용 아님/디렉터리 또는 PATHEXT 확장자) |
| `true` | 완료 (인자를 무시하고 0) |
| `false` | 완료 (인자를 무시하고 1) |
| `tr` | 완료 (`-c -C -d -s -t`, `[:alpha:]` 등의 문자 클래스, 범위, 이스케이프) |
| `uniq` | 완료 (`-c -d -D -u -i -f -s -w`) |
| `wc` | 완료 (`-l -w -m -c -L`) |

구현 예정 목록은 [아래](#구현-예정)를 참고하세요.

## 쉘 사용법

- 프로그램은 `shell.exe`가 있는 디렉터리(`bin\`)에서만 찾으며, 확장자 `.exe`는 생략할 수 있습니다.
- 이 프로젝트는 리눅스 명령과 같은 역할을 하는 것을 목표로 하며, Windows에서 그대로 동작하지 않는 부분만 Windows에 맞게 조정합니다.
  (예: `cat` 은 바이트를 변환하지 않고, CRLF 한 쌍을 하나의 줄 끝으로 취급합니다.)
- `exit [N]` 또는 `quit` 으로 종료합니다. 상태 N을 생략하면 마지막 명령의 상태입니다. 입력 끝(Ctrl+Z, Enter)에서도 종료합니다.
- **내장 명령:** `cd [DIR]`, `export NAME[=VALUE]...`(인자 없이 쓰면 목록), `unset NAME...`, `exit [N]`, `quit`.
- **명령 목록**
  - `a ; b` : a가 끝난 뒤 항상 b를 실행합니다.
  - `a && b` : a가 성공(상태 0)했을 때만 b를 실행합니다.
  - `a || b` : a가 실패했을 때만 b를 실행합니다.
  - 왼쪽에서 오른쪽으로 평가하며(`a && b || c` 는 `(a && b) || c`), 건너뛴 명령은 상태를 바꾸지 않습니다.
  - 각 명령은 차례가 되었을 때 해석하므로 `cd dir && ls *` 처럼 앞 명령이 바꾼 디렉터리와 변수를 봅니다.
  - 파이프 안의 명령은 마지막 명령의 상태가 파이프라인의 상태입니다. 찾지 못한 프로그램은 127, 문법 오류는 2, Ctrl+C로 중단된 프로그램은 130입니다.
- **변수 대입**
  - `NAME=값` 만 쓰면 쉘 변수를 정합니다. 쉘만 알고 프로그램에는 전달되지 않으며, `export NAME` 으로 환경에 내보내면 이후 실행하는 프로그램도 갖습니다. 이미 내보낸 변수에 대입하면 계속 내보낸 상태입니다.
  - `NAME=값 명령` 은 그 명령의 환경에만 변수를 둡니다. 쉘의 변수는 바뀌지 않습니다. 명령 위치에서만 대입이며(`echo A=1` 의 `A=1` 은 인자), 이름이나 `=` 가 따옴표 안에 있으면 대입이 아닙니다.
  - 값은 한 단어로 남고 와일드카드는 동작하지 않습니다. 값 맨 앞의 `~` 는 `$HOME` 입니다. `A=$B` 에서 `$B` 가 공백을 가져도 나뉘지 않습니다.
  - 확장은 대입보다 먼저 일어나므로 `A=1 echo $A` 는 이전의 `$A` 를 봅니다. 파이프 안의 `A=1 | cat` 은 쉘의 변수를 바꾸지 않습니다.
- **변수 확장**
  - `$NAME`, `${NAME}` 은 쉘 변수 또는 환경 변수의 값으로, `$?` 는 마지막 명령의 종료 상태로 바뀝니다. 이름은 대소문자를 구분하지 않습니다(Windows와 같음).
  - 큰따옴표 안에서는 한 단어로 남고, 따옴표 밖에서는 공백으로 단어가 나뉘며 값의 와일드카드도 동작합니다. 작은따옴표 안에서는 확장하지 않습니다. 정의되지 않은 변수는 따옴표 밖에서는 단어를 만들지 않습니다.
  - `$` 뒤에 이름이 오지 않으면 그냥 `$` 입니다. 리터럴 `$NAME` 은 작은따옴표로 쓰세요(`\` 는 경로 구분자라 이스케이프로 쓰지 않습니다).
  - 값에 `|`, `>`, `;` 등이 있어도 연산자가 되지 않습니다.
  - `PWD`, `OLDPWD` 는 `shell/src/cwd.c` 가 가상 경로로 유지하고(`cd` 만 바꿉니다) 내보내기(export)된 변수라 자식 프로세스가 받습니다. `HOME`, `USER` 는 정의되어 있지 않으면 Windows 프로필(`USERPROFILE`, `USERNAME`)에서 만듭니다(`HOME` 은 가상 경로).
  - 단어 맨 앞의 `~` 는 `$HOME` 입니다(`~`, `~/docs`).
- **따옴표**
  - `"a b"` : 공백을 유지합니다. 안에서 `\"` 는 따옴표 문자입니다.
  - `'a b'` : 안의 모든 문자를 그대로 사용합니다.
  - `a"b c"d` 처럼 섞어 쓸 수 있으며, `""` 는 빈 인자가 됩니다.
  - 백슬래시는 큰따옴표 앞에서만 특별하게 취급하므로 `C:\bin\echo` 같은 경로는 그대로 입력하면 됩니다.
  - 따옴표를 닫지 않으면 오류를 출력하고 명령을 실행하지 않습니다.
- **와일드카드(글롭)**
  - `*` 는 임의의 문자열(빈 문자열 포함), `?` 는 한 글자, `[abc]`, `[a-z]`, `[!abc]` 는 문자 집합입니다.
  - 프로그램이 시작되기 전에 쉘이 일치하는 경로로 확장하므로, 프로그램은 확장된 이름을 받습니다(`ls *.txt`, `cat sub/*.c`).
  - 가상 경로, Windows 경로, 하위 디렉터리를 모두 쓸 수 있습니다(`/c/Users/*`, `*/*.c`, `sub\*.dat`). 결과는 입력한 경로 형식 그대로입니다.
  - 일치하는 것이 없으면 입력한 단어가 그대로 전달됩니다. 따옴표 안의 와일드카드(`"*.txt"`)는 확장하지 않습니다.
  - Windows에 맞춘 점: 이름의 대소문자를 구분하지 않으며, `.`으로 시작하는 이름은 `.`으로 시작하는 패턴(`.*`)에만 일치하고, 숨김/시스템 속성이 있는 파일은 일치하지 않습니다(`rm *` 이 시스템 파일을 건드리지 않도록).
  - 리다이렉션의 파일 이름은 확장하지 않습니다.
- **파이프와 리다이렉션**
  - `a | b` : a의 표준 출력을 b의 표준 입력으로 연결합니다. 모든 명령이 동시에 실행됩니다.
  - `< 파일` : 표준 입력을 파일에서 읽습니다.
  - `> 파일`, `>> 파일` : 표준 출력을 파일에 쓰거나(덮어쓰기), 이어 씁니다.
  - `2> 파일`, `2>> 파일` : 표준 오류를 파일에 쓰거나, 이어 씁니다.
  - 연산자 주변의 공백은 생략할 수 있습니다(`echo hi>out.txt`). 따옴표 안에서는 연산자로 해석하지 않습니다.
  - `2>&1` : 표준 오류를 표준 출력이 가는 곳으로 보냅니다. `1>&2`(`>&2`)는 반대입니다.
  - `&> 파일`, `&>> 파일` : 표준 출력과 표준 오류를 같은 파일에 씁니다.
  - 표준 출력/오류의 리다이렉션은 적은 순서대로 적용됩니다. `a > f 2>&1` 은 둘 다 f로, `a 2>&1 > f` 는 오류는 원래 출력으로, 출력만 f로 갑니다.
  - 리다이렉션은 파이프보다 우선합니다. 마지막 명령의 종료 코드가 결과가 됩니다.
  - `&`(백그라운드)는 아직 지원하지 않으며, 혼자 있는 `&` 는 문법 오류입니다.
- **스크립트와 명령 치환**
  - `shell FILE [인자...]` 는 파일의 줄을 차례로 실행합니다(`$0` 은 FILE, `$1`..`$9`, `$#`, `$@`, `$*` 은 인자). `shell -c '명령' [이름 [인자...]]`
    도 같습니다. `source FILE [인자...]` 와 `. FILE` 은 같은 쉘에서 실행하며, 인자가 없으면 호출한 쪽의 `$1`.. 을 그대로 봅니다.
    파일에서 `exit [N]` 은 쉘을 끝내고, 스크립트의 종료 상태는 마지막 명령(또는 `exit`)의 상태입니다. 한 줄이 문법 오류이면
    알리고 다음 줄을 실행합니다. 줄 끝은 LF 와 CR LF, 앞의 UTF-8 BOM 은 무시합니다.
  - `$(명령)` 과 `` `명령` `` 은 명령의 표준 출력으로 바뀌며 끝의 줄바꿈은 지워집니다. 따옴표 밖에서는 공백으로 단어가 나뉘고 따옴표 안에서는
    한 단어입니다. 중첩할 수 있고, 안에 `;`, `&&`, 따옴표가 있어도 됩니다. 작은따옴표 안에서는 글자 그대로입니다.
    서브셸처럼 현재 디렉터리와 변수(환경 포함)는 치환이 끝나면 원래대로 돌아오고, 안의 `exit` 는 치환만 끝냅니다.
    `x=$(명령)` 만 있는 줄의 `$?` 는 명령의 상태이고, 그 밖에는 치환 앞뒤의 `$?` 가 바뀌지 않습니다.
  - `#` 는 단어의 시작에서(따옴표 밖) 그 줄의 나머지를 주석으로 만듭니다.
- **Ctrl+C / Ctrl+Break**
  - 프로그램 실행 중에는 그 프로그램만 중단되고 쉘은 계속 실행됩니다.
  - 프롬프트에서는 쉘이 종료되지 않고 입력 중이던 줄을 버립니다.

## 빌드

### 요구사항

- **MSVC 컴파일러(`cl.exe`)와 `nmake.exe`**
  - Visual Studio 또는 Build Tools for Visual Studio (C++ 데스크톱 개발 워크로드)
- **`cl.exe`의 경로가 환경 변수(`PATH`)에 등록되어 있어야 합니다.**
  - 가장 간단한 방법은 *x64 Native Tools Command Prompt for VS* 또는 *Developer Command Prompt* 에서 빌드하는 것입니다.
    이 터미널은 `PATH` 뿐 아니라 `INCLUDE`, `LIB` 도 함께 설정합니다.
  - 일반 터미널을 쓴다면 먼저 `vcvarsall.bat`(또는 `vcvars64.bat`)을 실행하세요.
  - 확인: `where cl` 이 경로를 출력해야 합니다.

### 빌드와 정리

```bat
nmake          :: 모든 서브 프로젝트를 빌드합니다
nmake clean    :: 빌드 결과물(bin\, obj\)을 삭제합니다
```

### 빌드 구조

- 최상위 `Makefile`은 `bin\` 디렉터리를 만든 뒤, 각 서브 프로젝트(`shell`, `arch`, `basename`, `cat`, `comm`, `cp`, `cut`, `date`, `diff`, `dirname`, `echo`, `env`, `expr`, `false`, `find`, `grep`, `head`, `kill`, `ls`, `mkdir`, `mv`, `nl`, `paste`, `printenv`, `ps`, `pwd`, `realpath`, `rm`, `rmdir`, `sed`, `sleep`, `sort`, `tac`, `tail`, `tee`, `test_cmd`, `touch`, `tr`, `true`, `uniq`, `wc`, `which`, `xargs`)의 `Makefile`을 호출합니다.
- 각 서브 프로젝트의 `Makefile`은 자기 디렉터리의 소스를 빌드하여 `bin\<이름>.exe`를 만듭니다.
- `shell.exe`는 기본적으로 `bin\` 에 있는 실행 파일만을 사용합니다.

## 테스트

```bat
nmake test      :: 모든 프로그램을 빌드한 뒤 테스트를 빌드하고 실행합니다
nmake asan      :: AddressSanitizer 로 빌드한 프로그램과 테스트로 같은 스위트를 실행합니다 (bin-asan\)
nmake gnu       :: GNU 도구와 출력 바이트를 대조합니다 (bash 와 GNU coreutils 필요: MSYS2, Git for Windows)
```

테스트 프로그램은 기능별로 나뉘어 있습니다(`test\`).

| 테스트 | 대상 |
| --- | --- |
| `vpath_test` | `common/vpath.c` 단위 테스트: 가상 경로 변환, `..` 처리, 루트, 버퍼 경계 |
| `parser_test` | 쉘 파서 단위 테스트: 단어, 따옴표, 변수, 명령 목록, 파이프, 리다이렉션, 구문 오류 |
| `args_test` | `common/args.c` 단위 테스트: 도구의 옵션 파서(묶음, 값, 줄임말, 숫자 옵션, 오류) |
| `basic_test` | `echo`, `arch`, `pwd` |
| `cat_test` | `cat`: 바이트 보존, 옵션, CRLF |
| `ls_test` | `ls`: 정렬, 숨김, 긴 형식, 가상 루트 |
| `fileops_test` | `rm`, `cp`, `mv`: 파일과 트리, 읽기 전용, 루트 보호 |
| `dirs_test` | `mkdir`, `rmdir`, `touch` |
| `text_test` | `head`, `tail`, `wc` |
| `filter_test` | `sort`, `uniq`, `cut`, `tr` |
| `lines_test` | `tee`, `tac`, `nl`, `paste`: 줄 끝, 구분자, 섹션, 표준 입력 번갈아 읽기 |
| `env_test` | `env`, `printenv`: 환경 변경, `-i`/`-u`, 명령 실행과 종료 상태, `-C` |
| `regex_test` | `common/regex.c` 단위 테스트: 기본/확장 문법, 가장 긴 일치, 집합과 클래스, 단어 경계, 역참조, UTF-8, 오류 메시지 |
| `clock_test` | `sleep`, `date`: 대기 시간, 형식, `-d` 해석, ISO 주, 종료 상태 |
| `which_test` | `which`: 프로젝트 `bin`, `PATH`, `PATHEXT`, 디렉터리, 경로가 있는 이름, `-a`, 종료 상태 |
| `proc_test` | `kill`, `ps`: 실제 프로세스(sleep)에 대한 선택, 형식, 정렬, 시그널(종료 상태 128+번호, 중지/재개), 오류 |
| `diff_test` | `diff`: 세 형식, 무시 옵션, 줄 끝, 마지막 줄바꿈 없음, 표준 입력, 디렉터리, 큰 입력, 종료 상태 |
| `comm_test` | `comm`: 열, 열 숨기기, CR LF, 정렬 검사, `--total`, 구분자, 표준 입력, 종료 상태 |
| `realpath_test` | `realpath`: 가상 경로, 존재 규칙, 상대 경로, 정션 |
| `names_test` | `basename`, `dirname`: 구분자(`\` 포함), 접미사, NUL 끝, 루트 |
| `xargs_test` | `xargs`: 묶음, 항목 해석, 파일, 종료 상태, 병렬 실행 |
| `find_test` | `find`: 걷기 순서, 깊이, `-prune`, `-exec`/`-execdir`/`-ok`, `-delete`, 가상 경로, 한글 이름, 종료 상태 |
| `expr_test` | `expr`: 산술, 비교, 논리, 문자열 함수, 큰 정수, 종료 상태 |
| `sed_test` | `sed`: 치환, 주소, 명령, `-i`와 백업, `w`/`r` 파일, CRLF와 마지막 줄 끝, 종료 상태, UTF-8 |
| `grep_test` | `grep`: CRLF, UTF-8, 바이너리 파일, `-r`, 문맥, 종료 상태, 흐르는 입력 |
| `shell_utf8_test` | 쉘: 한글 이름과 디렉터리, 글자 단위 와일드카드, 변수, 리다이렉션 (Windows 10 1903 이상) |
| `shell_run_test` | 쉘: 명령 실행, 따옴표, 파이프, 리다이렉션 |
| `shell_script_test` | 쉘 스크립트: 파일/`-c`/`source`, `$0 $1.. $# $@`, 명령 치환(`$(...)`, 백틱), 주석, 종료 상태 |
| `test_cmd_test` | `test`, `[`, `true`, `false`: 문자열, 정수, 파일, 식, 인자 개수 규칙, 오류 |
| `shell_cd_test` | 쉘: `cd`, 가상 파일 시스템, 와일드카드 |
| `shell_lists_test` | 쉘: `; && \|\|`, 종료 상태, 변수, `exit` |
| `shell_pipeline_test` | 쉘: 명령들을 파이프, 리다이렉션, 와일드카드와 함께 |
| `equiv_test` | **속성 테스트**: 무작위 입력에서 LF, CRLF, 혼합 줄 끝이 같은 결과를 내는지 (`WS_ITER` 기본 8회, 오래 돌리려면 `WS_ITER=100`; `WS_SEED`) |
| `limits_test` | **한계**: 50만 줄 정렬, 20MB 한 줄, 버퍼 경계(65536±1), 200만 빈 줄, 깊은 경로, 많은 파일 (`WS_STRESS=1` 이면 10배) |
| `sharing_test` | **공유 위반**: 다른 프로세스가 연 파일/디렉터리에 대한 `rm`/`mv`/`cp`, 다른 볼륨으로의 `mv` |
| `async_test` | **비동기**: 조각으로 들어오는 입력, 입력이 닫히길 기다리는 프로그램, `tail -f` |

- 모든 테스트가 통과하면 `nmake test` 가 0으로 끝나고, 실패하면 어느 프로그램인지와 실패한 검사의 위치, 기대값/실제값을 출력합니다.
- 테스트 프로그램과 작업 파일은 `test\out\` 에 만들어집니다(git 추적 제외). 개별 테스트는 `test\out\<이름>.exe` 로 직접 실행할 수 있습니다.
- 테스트 프로그램은 `test\out\runall.exe` 가 여러 개씩 동시에 실행합니다(각자 작업 디렉터리가 달라 겹치지 않습니다). 통과한 프로그램은 요약 한 줄만, 실패한 프로그램은 전체 출력을 보여 줍니다. `WS_JOBS=N` 동시 실행 수(기본: 프로세서의 절반), `WS_VERBOSE=1` 모든 출력.
- **AddressSanitizer:** `nmake asan` 은 모든 도구와 테스트를 `/fsanitize=address` 로 다시 빌드해서(`bin-asan\`, `test\out-asan\`) 실행합니다. 메모리 오류는 위치와 함께 보고되고 테스트가 실패합니다. 환경 변수 `WINSHELL_BIN` 으로 테스트가 쓰는 도구 디렉터리를 바꿀 수 있습니다.
- **GNU 대조:** `test\gnu_compare.sh` 는 `head`, `tail`, `wc`, `sort`, `uniq`, `cut`, `tr`, `cat`, `tac`, `nl`, `paste`, `tee`, `grep`, `sed`, `expr`, `find`, `xargs`, `basename`, `dirname`, `date`, `realpath`, `diff`, `comm`, `test`, `true`, `false` 를 3천여 가지 옵션/입력 조합으로 GNU 도구와 비교합니다(`LC_ALL=C`, grep 은 `C.UTF-8`). 조합은 스크립트가 모으고 `test\out\gnu_batch.exe` 가 여러 개씩 동시에 실행합니다(MSYS bash 에서 프로그램을 띄우는 것보다 훨씬 빠르며 약 30초). `GNU_ONLY=grep` 으로 한 섹션만, `GNU_IMMEDIATE=1` 이면 스크립트가 직접 하나씩 실행합니다. 의도적으로 다른 부분(CRLF 줄 끝은 줄 내용이 아님, `sort -z/-h/-M/-V` 거부, UTF-8 문자 수)은 비교에서 빠져 있습니다.
- **다른 드라이브로의 `mv`:** 환경 변수 `WINSHELL_TEST_CROSS_DEVICE` 가 설정되면 `mv` 는 이름 변경을 시도하지 않고 복사 후 삭제 경로를 탑니다. 테스트는 이것으로 한 볼륨에서도 같은 코드를 실행하고, 쓸 수 있는 고정 드라이브가 하나 더 있으면 실제로도 시험합니다.
- 새 명령을 추가하면 해당 기능의 `*_test.c` 에 항목을 추가하세요. 공통 도구는 `testlib.h`(검사, 프로세스 실행), `testtools.h`(작업 디렉터리, 도구 실행, 픽스처) 에 있습니다. 줄 끝은 LF 와 CRLF 를 구별하지 않고 비교합니다(`lf_only`).
- Ctrl+C 처리는 실제 콘솔이 필요해서 자동 테스트에 포함하지 않았습니다.

## 디렉터리 구조

```
Winshell/
├─ Makefile        최상위 빌드 규칙
├─ common.mk       모든 도구가 포함하는 빌드 규칙
├─ shell/          쉘 본체 (include/, src/)
├─ arch/           arch 명령
├─ basename/       basename 명령
├─ cat/            cat 명령
├─ comm/           comm 명령
├─ cp/             cp 명령
├─ cut/            cut 명령
├─ echo/           echo 명령
├─ date/           date 명령
├─ diff/           diff 명령
├─ dirname/        dirname 명령
├─ env/            env 명령
├─ expr/           expr 명령
├─ find/           find 명령 (parse.c 식 해석, eval.c 걷기와 평가)
├─ grep/           grep 명령
├─ realpath/       realpath 명령
├─ sed/            sed 명령 (script.c 스크립트 해석, exec.c 실행)
├─ sleep/          sleep 명령
├─ head/           head 명령
├─ kill/           kill 명령
├─ ls/             ls 명령
├─ mkdir/          mkdir 명령
├─ mv/             mv 명령
├─ printenv/       printenv 명령
├─ nl/             nl 명령
├─ paste/          paste 명령
├─ ps/             ps 명령
├─ pwd/            pwd 명령
├─ rm/             rm 명령
├─ rmdir/          rmdir 명령
├─ sort/           sort 명령
├─ tac/            tac 명령
├─ tail/           tail 명령
├─ tee/            tee 명령
├─ touch/          touch 명령
├─ tr/             tr 명령
├─ uniq/           uniq 명령
├─ wc/             wc 명령
├─ which/          which 명령
├─ xargs/          xargs 명령
├─ false/        false 명령
├─ test_cmd/     test_cmd 명령
├─ true/         true 명령
├─ common/         여러 명령이 함께 쓰는 코드 (vpath: 가상 경로 변환, fsutil: 복사/삭제, textutil: 텍스트 입력과 줄 나누기, args: 옵션 파서, regex: 정규식, vec: 자라는 배열)
├─ test/           테스트 (nmake test)
└─ bin/            빌드 결과물 (git 추적 제외)
```

## 새 명령어 추가하기

1. `nmake new NAME=<이름>` 을 실행합니다. 동작하는 뼈대(`<이름>\main.c`, `Makefile`), `test\<이름>_test.c`, `test\gnu\<이름>.sh`
   를 만들고 두 README 의 명령 표, 테스트 표, 디렉터리 구조에 항목을 넣습니다.
2. `main.c` 를 작성합니다. 옵션은 `common/args.h` 의 `args_parse` 로 처리합니다(`tee/main.c` 가 가장 작은 예입니다).
   도구의 `Makefile` 은 `NAME`, `SRCS`, `HDRS`(필요하면 `INCLUDES`, `LIBS`)만 정하고 규칙은 `common.mk` 를 포함해서 얻습니다.
3. README 의 명령 표 행을 채우고 테스트와 GNU 대조(`test\gnu\<이름>.sh`)를 작성합니다.
   `nmake check-registry` 가 README 나 테스트에 빠진 도구를 알려 줍니다.

빌드 목록, 테스트 목록, GNU 대조 목록은 파일에서 만들어지므로 따로 적지 않습니다: `Makefile` 이 있는 디렉터리는 모두 도구이고
(`scripts\list-tools.bat`), `test\*_test.c` 는 모두 테스트 프로그램이며(`scripts\list-tests.bat`; 다른 소스를 링크하는 단위
테스트만 `test\Makefile` 에 규칙이 있습니다), `test\gnu\*.sh` 는 모두 GNU 대조 구간입니다. 디렉터리 구조와 빌드 구조 문장은
`nmake sync-readme` 가 디렉터리에 맞춥니다.

`common.mk` 는 `all`, `clean` 규칙을 제공하고 `BINDIR` 매크로로 출력 위치를, `EXTRA_CFLAGS` 로 추가 컴파일 옵션을 받습니다.

## 설계 방향

- **가상 루트 디렉터리:** UNIX 파일 시스템처럼 논리적인 루트 디렉터리 `/` 가 있고, 각 드라이브는 그 아래에 소문자 한 글자로 배치됩니다.
  - `C:\Users\me` 는 `/c/Users/me` 입니다. `/` 아래에는 드라이브만 있으며 `cd /`, `pwd`, `cat /` 모두 이를 따릅니다.
  - `C:\Users\me` 같은 Windows 경로도 어디서나 그대로 사용할 수 있습니다. `\\server\share` 같은 UNC 경로는 가상 경로 밖이므로 변환 없이 Windows에 전달됩니다.
  - 쉘의 `cd` 는 내장 명령이며, 현재 디렉터리를 환경 변수 `PWD` 로 자식 프로세스에 알립니다. 루트 `/` 에 있을 때는 Windows 쪽 디렉터리가 시스템 드라이브의 루트이고, 상대 경로는 어느 파일도 가리키지 않습니다.
  - 각 명령은 받은 경로를 직접 변환합니다. 새 명령에서 파일 경로를 열 때는 `common/vpath.h` 의 `vpath_to_native` 를 사용하세요.
- **UTF-8:** 모든 프로그램은 `common/utf8.manifest` 를 실행 파일에 넣어 UTF-8 코드 페이지로 동작합니다. 그래서 인자, 파일 이름, 환경 변수, 입력이 모두 UTF-8이고 한글 이름의 파일을 그대로 다룹니다.
  - 쉘은 콘솔 입력을 `ReadConsoleW` 로 읽고, 실행하는 동안 콘솔 코드 페이지를 UTF-8로 바꿨다가 끝나면 되돌립니다. 와일드카드 `?` 와 `[가-힣]` 은 바이트가 아니라 글자 하나에 일치합니다.
  - 이 매니페스트는 Windows 10 버전 1903부터 적용됩니다. 그보다 오래된 Windows 에서는 시스템의 ANSI 코드 페이지로 동작하며, ASCII 이름은 어디서나 같습니다. 쉘이 아닌 `cmd` 에서 도구를 직접 실행하면 출력이 UTF-8이므로 `chcp 65001` 이 필요합니다.
- **호환성 (목표):** 기초적인 시스템 콜을 이용해 Windows XP까지의 호환성을 확보하는 것이 목표이며, 아직 검증되지 않았습니다.

## 구현 예정

**쉘 기능**: 백그라운드 실행(`&`), 중괄호 확장, `if`/`for`/`while`

**coreutils**

<details>
<summary>목록 펼치기</summary>

`[`, b2sum, base32, base64, basename, chgrp, chmod, chown, cksum, comm, csplit, date, dd, df, dir,
dircolors, dirname, du, expand, expr, factor, false, fmt, fold, groups, hostid, hostname, id, install,
join, link, ln, logname, md5sum, mkfifo, mknod, mktemp, nice, nohup, nproc, numfmt, od,
pathchk, pinky, pr, printf, ptx, readlink, realpath, seq, sha1sum, sha224sum, sha256sum,
sha384sum, sha512sum, shred, shuf, sleep, split, stat, stdbuf, sum, sync, test, true,
truncate, tsort, tty, uname, unexpand, unlink, uptime, users, vdir, who, whoami, yes

</details>

**대상 제외 (Windows에서 의미가 없거나 대응 개념이 없음)**: chcon, chroot, runcon, stty, coreutils(멀티콜 바이너리)

## 라이선스

[Apache License 2.0](LICENSE)

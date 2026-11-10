# Winshell 레퍼런스

[English](REFERENCE.en.md) | **한국어** · [← README](../README.md)

명령별 지원 옵션, 쉘 문법, 빌드와 테스트, 내부 구조를 정리한 문서입니다.

## 구현 현황

| 명령 | 상태 |
| --- | --- |
| `shell` | 입력 루프, 명령 실행(종료 대기), 파이프/리다이렉션, `; && \|\|`, 변수 확장(`$? $0 $1..$9 $# $@ $*`), 명령 치환(`$(...)`, 백틱), 주석(`#`), 제어문(`if`/`elif`/`else`, `while`, `until`, `for`, `case`, `break`, `continue`, `:`), 함수(`name() { ...; }`, `return`, `local`), 묶음(`{ ...; }`, `( ... )`), `trap`(`EXIT` `INT` `ERR`), 산술 확장(`$((...))`), 매개변수 확장(`${NAME:-값}` 등), `read`, 반복문의 리다이렉션과 파이프, 글롭, 스크립트 실행(`shell FILE [ARG...]`, `shell -c`, `source`/`.`), `cd` `export` `unset` `exit` `source` |
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
| `ls` | 완료 (`-a -A -b -C -d -F -h -l -p -r -R -S -t -U -1`, `/` 는 드라이브 목록; `-b`는 공백·역슬래시·제어 문자를 이스케이프로 씀) |
| `dir` | 완료 (`ls -C -b`와 같음: 콘솔이 아니어도 열로 나열하고 이름의 공백과 역슬래시를 이스케이프; `ls`와 같은 소스) |
| `vdir` | 완료 (`ls -l -b`와 같음: 긴 형식; `ls`와 같은 소스) |
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
| `printf` | 완료 (GNU printf: `%d %i %o %u %x %X %e %f %g %a %c %s %b %%`, 플래그/너비/정밀도/`*`, 이스케이프 `\n \xHH \NNN \uHHHH \c`, 형식 반복, `'a` 문자 상수; 값은 `long double` 이 아닌 `double` 로 계산) |
| `tty` | 완료 (`-s`; 콘솔이면 `/dev/console`, 아니면 `not a tty` 와 상태 1) |
| `nproc` | 완료 (`--all`, `--ignore=N`, 프로세스 선호도와 `OMP_NUM_THREADS`/`OMP_THREAD_LIMIT` 반영) |
| `hostname` | 완료 (`-s -f -d`; 이름을 바꾸는 것은 지원하지 않음) |
| `uname` | 완료 (`-a -s -n -r -v -m -p -i -o`; 시스템 이름은 `Windows_NT`, 릴리스는 `10.0.26200` 같은 버전) |
| `whoami` | 완료 |
| `yes` | 완료 (인자를 공백으로 이어 한 줄로 끝없이 출력, 읽는 쪽이 닫으면 종료) |
| `seq` | 완료 (`-f -s -w`, 정수는 정확히, 소수는 FIRST/INCREMENT 의 자릿수대로, `-3` 같은 음수 인자) |
| `base32` | 완료 (`-d -i -w`; RFC 4648) |
| `base64` | 완료 (`-d -i -w`; 76 칸 줄바꿈, 잘못된 입력은 디코딩된 부분을 쓴 뒤 상태 1) |
| `unexpand` | 완료 (`-a --first-only -t 목록`; 탭 앞의 공백 하나 규칙까지 GNU 와 같음) |
| `expand` | 완료 (`-t N`, `-t 목록`, `/N` 과 `+N`, `-i`, 백스페이스, UTF-8) |
| `fold` | 완료 (`-w -N -b -s`; 탭, 백스페이스, CR, UTF-8 문자의 칸 계산) |
| `truncate` | 완료 (`-s [+-<>/%]크기`, `-r`, `-c`, `-o`; `K M G T P E` 와 `KB MB …` 접미사) |
| `readlink` | 완료 (`-f -e -m -n -q -s -v -z`; 정션/심볼릭 링크의 대상, `-f -e -m` 은 `realpath` 와 같은 정규 경로) |
| `mktemp` | 완료 (`-d -u -q --suffix -p/--tmpdir -t`; 템플릿의 `XXX…` 를 무작위 글자로 바꿔 파일/디렉터리를 만들고 이름을 출력) |
| `cksum` | 완료 (POSIX CRC 와 바이트 수; 표준 입력은 이름 없이 출력) |
| `sha512sum` | 완료 (`SHA-512`: `-b -c -t -w -z --tag --quiet --status --strict --ignore-missing`; 기본 `해시 *이름`, BSD 형식, 검사 결과 `이름: OK`) |
| `sha384sum` | 완료 (`SHA-384`: `-b -c -t -w -z --tag --quiet --status --strict --ignore-missing`; 기본 `해시 *이름`, BSD 형식, 검사 결과 `이름: OK`) |
| `sha256sum` | 완료 (`SHA-256`: `-b -c -t -w -z --tag --quiet --status --strict --ignore-missing`; 기본 `해시 *이름`, BSD 형식, 검사 결과 `이름: OK`) |
| `sha224sum` | 완료 (`SHA-224`: `-b -c -t -w -z --tag --quiet --status --strict --ignore-missing`; 기본 `해시 *이름`, BSD 형식, 검사 결과 `이름: OK`) |
| `sha1sum` | 완료 (`SHA-1`: `-b -c -t -w -z --tag --quiet --status --strict --ignore-missing`; 기본 `해시 *이름`, BSD 형식, 검사 결과 `이름: OK`) |
| `md5sum` | 완료 (`MD5 (RFC 1321)`: `-b -c -t -w -z --tag --quiet --status --strict --ignore-missing`; 기본 `해시 *이름`, BSD 형식, 검사 결과 `이름: OK`) |
| `factor` | 완료 (64비트까지, Miller-Rabin 과 Pollard rho; 인자 또는 표준 입력) |
| `split` | 완료 (`-l -b -C -n N K/N l/N l/K/N r/N r/K/N -a -d -x -e -t --additional-suffix --verbose`; 숫자 접미사는 모자라면 `x89` 다음 `x9000` 으로 늘어남) |
| `du` | 완료 (`-0 -a -b -B -c -d -h -k -l -m -P -L -S -s -x --apparent-size --exclude --si`; 디스크 사용량은 할당 크기, 하드 링크는 한 번만 셈) |
| `stat` | 완료 (`-c -f -L -t --printf`; 서식 `%a %A %b %B %d %D %f %F %g %G %h %i %m %n %N %o %s %t %T %u %U %w %W %x %X %y %Y %z %Z`; 장치는 볼륨 번호, inode 는 파일 인덱스, 사용자/그룹은 SID 의 마지막 번호) |
| `unlink` | 완료 (파일 하나를 지움, 디렉터리는 거부, 읽기 전용도 지움) |
| `link` | 완료 (하드 링크 하나) |
| `ln` | 완료 (`-s -f -i -n -v -b -S -r -T -t`; 하드 링크는 `CreateHardLink`, 심볼릭 링크는 권한 없이 만들 수 있으면 심볼릭 링크, 디렉터리는 정션으로 대신) |
| `od` | 완료 (`-A -j -N -t -v -w -S --endian`과 `-a -b -c -d -f -i -l -o -s -x -h`; 여러 종류를 아래위로 맞춰 출력, `z` 접미사, 같은 줄은 `*`; `-t fL`(80비트 long double)은 이 컴파일러에 없어 제외) |
| `join` | 완료 (`-1 -2 -j -a -v -e -o -t -i -z --check-order --nocheck-order --header`, `-o auto`; GNU의 합치기 순서와 정렬 검사 그대로) |
| `shuf` | 완료 (`-e -i -n -o -r -z --random-source`; 같은 무작위 바이트를 주면 GNU와 같은 순열, 큰 범위도 메모리를 쓰지 않음) |
| `sum` | 완료 (BSD `-r`, System V `-s`) |
| `tsort` | 완료 (GNU와 같은 순서, 순환은 표준 오류에 알리고 한 쌍을 버린 뒤 계속, 종료 코드 1) |
| `dd` | 완료 (`if of bs ibs obs cbs count skip seek status conv iflag oflag`; `conv=` notrunc noerror sync swab ucase lcase ascii ebcdic ibm block unblock excl nocreat sparse fsync; 블록 수와 속도는 표준 오류로) |
| `pathchk` | 완료 (`-p -P --portability`; 길이 한계는 리눅스 값 255/4095, `-p`는 14/255와 이식 가능한 문자) |
| `b2sum` | 완료 (BLAKE2b, `-l` 다이제스트 길이, `--tag`, `-c`는 줄마다 길이를 따름; 다른 `*sum`과 같은 옵션) |
| `users` | 완료 (열려 있는 쉘의 사용자 이름을 정렬해 한 줄로; 세션은 쉘이 시작할 때 남긴 기록에서 가져오고 FILE은 받아도 읽지 않음) |
| `who` | 완료 (`-a -b -d -H -l -m -p -q -r -s -t -T -u -w`, `who am i`; Windows에는 utmp가 없어서 **열려 있는 Winshell 쉘이 세션**: 이름은 `pts/N`, 시간은 쉘을 연 시각, PID는 쉘의 PID; `-b`는 시스템 부팅 시각; 죽은 프로세스·login·run level·시계 변경은 없어서 `-d -l -p -r -t`는 아무것도 내지 않음; 머리글과 열 모양은 GNU와 같음) |
| `pinky` | 완료 (`-l -b -h -p -s -f -w -i -q`; 짧은 형식은 열려 있는 쉘을 보여 주고, 긴 형식은 계정의 이름·홈·쉘(Winshell의 `shell.exe`)과 `.project`·`.plan`; 유휴 시간은 알 수 없어서 `?`) |
| `uptime` | 완료 (현재 시각, 부팅 후 시간(`GetTickCount64`), 사용자 수(열려 있는 쉘), load average; Windows에는 load average가 없어서 `0.00, 0.00, 0.00`) |
| `groups` | 완료 (그룹 이름; `USER...`를 주면 `user : 그룹...`; 계정의 그룹, 기본 그룹이 먼저) |
| `id` | 완료 (`-a -g -G -n -r -u -z`, `USER...`; ID는 보안 식별자(SID)의 마지막 번호, 실제/유효 ID는 같음, `-Z`는 SELinux가 없어 오류) |
| `hostid` | 완료 (16진수 8자리; 장치 GUID(MachineGuid)에서 만들어 같은 컴퓨터에서는 늘 같음) |
| `logname` | 완료 (계정 이름; Windows에는 따로 로그인 이름이 없음) |
| `csplit` | 완료 (`-b -f -k -n -s -q -z --suppress-matched`; 패턴 줄 번호, `/정규식/[오프셋]`, `%정규식%[오프셋]`, `{N}`, `{*}`; 정규식은 POSIX 기본; `%-3u`처럼 끝에 공백이 붙는 파일 이름은 Windows가 공백을 지워 GNU와 다름) |
| `numfmt` | 완료 (`--from --to --from-unit --to-unit --round --padding --format --suffix --field -d --header --invalid --debug -z`; GNU는 long double로 계산하지만 여기서는 큰 정수로 정확히 계산해, 16자리 이상 숫자는 마지막 자리가 다를 수 있음; `--grouping`은 C 로캘처럼 아무 효과 없음; CR LF는 줄 끝으로 받음) |
| `sync` | 완료 (`-d -f`, `FILE...`; 파일 버퍼를 디스크로 내보냄; FILE 없이는 시스템이 알아서 쓰므로 성공만 하고, `-f`는 관리자 권한이 없어 파일을 여는 것까지만) |
| `shred` | 완료 (`-f -n -s -u -v -x -z --remove[=HOW] --random-source`; 4번 이상은 무작위와 고정 무늬 패스; 크기는 4096 블록으로 올림(GNU는 파일 시스템의 블록); `-f`는 읽기 전용 속성을 지움; 표준 출력은 일반 파일일 때만) |
| `nice` | 완료 (`-n N`, `--adjustment`, 옛 형식 `-N` `--N` `-+N`; Windows의 우선순위 클래스로 대응: 보통 0, 낮음 이하 10, 유휴 19, 보통 이상 -5, 높음 -10, 실시간 -20) |
| `nohup` | 완료 (표준 입력이 터미널이면 NUL에서 읽고, 표준 출력이 터미널이면 `nohup.out`에 이어 쓰고, 터미널인 표준 오류는 표준 출력으로; Windows에는 SIGHUP이 없어 그 부분은 없음; 상태 125/126/127) |
| `df` | 완료 (`-a -B -h -H -i -k -l -P -t -T -x --output --total`, `DF_BLOCK_SIZE`; 파일 시스템은 드라이브(`C:` → `/c`), 형식은 ntfs/fat32 등, inode는 없어 `-`; `--sync`는 받기만 함) |
| `dircolors` | 완료 (`-b -c -p`, 내장 데이터베이스는 GNU 8.32의 것; `TERM` 줄, 키워드와 `.확장자`/`*글롭`, 쉘 인용; `SHELL`이 없으면 Bourne 쉘로 봄) |
| `install` | 완료 (`-b --backup -S -c -C -d -D -m -p -t -T -v`, `-o`/`-g`는 이름만 검사; `-s`와 SELinux 옵션은 받기만 함; 모드는 chmod처럼 읽기 전용 속성으로만 남음) |
| `chmod` | 완료 (`-c -f -v -R --reference --preserve-root`; 8진수와 기호 모드 계산은 GNU와 같음; Windows가 가진 것은 읽기 전용 속성뿐이라 쓰기 비트가 하나도 없으면 읽기 전용, 하나라도 있으면 쓰기 가능이 되고 나머지는 계산해 알려 주기만 함; 디렉터리는 바뀌지 않음; 특수 비트(`s` `t`)는 저장되지 않음) |
| `chgrp` | 완료 (chown과 같은 방식: 파일의 그룹은 현재 사용자의 기본 그룹으로 보이고, 다른 그룹으로 바꾸는 것은 거절) |
| `chown` | 완료 (`-c -f -v -h -R -H -L -P --from --reference --preserve-root`, `OWNER[:GROUP]`; Windows의 파일은 모두 현재 사용자 소유로 보이므로(stat과 같음) 그 사용자를 쓰면 "retained", 다른 사용자는 "Operation not permitted"로 거절) |
| `fmt` | 완료 (`-c -p -s -t -u -w -g`, 옛 형식 `-WIDTH`; GNU와 같은 비용 계산으로 줄을 나눔(문장 끝, 짧은 마지막 낱말, 괄호 등), 5000자/1000낱말이 넘는 문단의 분할까지 같음; 탭은 GNU처럼 들여쓰기에 씀; CR LF는 줄 끝으로 받음) |
| `pr` | 완료 (`+FIRST[:LAST]`/`--pages`, `-COLUMN`, `-a -c -d -D -e -f -F -h -i -J -l -m -n -N -o -r -s -S -t -T -v -w -W`; 쪽마다 5줄 머리글(날짜, 파일 이름 또는 `-h`, `Page N`)·56줄 본문·5줄 꼬리글, 열은 GNU처럼 탭으로 맞춤, 폼 피드는 쪽 나눔; 입력은 통째로 읽음(끝나지 않는 표준 입력은 처리하지 못함); `-D`는 `%e %k %l %P %s %F %T %R %D %h %n %t %z` 등을 포함한 strftime; CR은 폭 0인 문자로 GNU와 같이 그대로 통과; 알려진 차이: 아래로 채우는 열에서 `-s`와 `-n`을 함께 쓸 때, 그리고 번호가 붙은 좁은 열 안의 탭은 GNU와 위치가 다를 수 있음) |
| `ptx` | 완료 (`-A -G -F -M -O -R -S -T -W -b -f -g -i -o -r -t -w`, `--format=roff|tex`; 모든 낱말이 키워드이고 한 줄에 키워드와 앞뒤 문맥이 나옴; 필드(tail, before, keyword+after, head)에 안 들어가는 문맥은 `/` 표시와 함께 반대쪽으로 옮겨 감, GNU의 필드 계산(`maximum_word_length` 창, 공백 건너뛰기 등)과 같음; 기본은 문장 끝(`. ` 뒤 공백 두 칸이나 줄 끝)으로 문맥을 나누고, `-G`/`-r`은 줄 단위; 사용자 정규식은 Emacs 문법(`+ ? *`은 연산자); 알려진 차이: 여러 파일에서 같은 키워드가 나올 때의 순서(GNU는 버퍼 주소로 정함)와 `-b` 파일로 낱말이 아주 길어지는 경우) |

구현 예정 목록은 [README의 로드맵](../README.md#로드맵)을 참고하세요.

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
  - 중괄호 확장은 다른 확장보다 먼저, 따옴표 밖의 낱말에서 합니다: `a{b,c}d` → `abd acd`, `{1..5}`, `{5..1}`, `{01..10}`(앞의 0은 자릿수를 맞춥니다),
    `{a..e}`, `{1..10..3}`(증가값), 중첩 `x{a,b{1,2}}y`, 여러 개 `{a,b}{1,2}` → `a1 a2 b1 b2`. 최상위에 쉼표가 있거나 숫자/글자 두 개의
    범위일 때만 식이고, `{}`, `{a}`, `{ list; }`, `find -exec {}` 는 그대로 둡니다. 따옴표 안, `${...}`/`$(...)` 안, `NAME={a,b}` 의 값, 리다이렉션
    대상(`> out{1,2}`), `case` 의 패턴, `\{` 는 확장하지 않습니다(그 때의 `\` 는 그대로 남습니다). 한 낱말이 65536 개를 넘으면 그대로 둡니다.
  - 배열(인덱스 배열, 듬성듬성해도 됩니다): `a=(x "y z" w)`(원소는 명령의 인자처럼 펼쳐집니다: 중괄호, 변수, 따옴표, 와일드카드, 여러 줄),
    `a[2]=v`, `a+=(더)`(가장 큰 인덱스 뒤에), `a[i]+=문자열`, `a+=문자열`. 읽기는 `${a[i]}`(인덱스는 산술식: `${a[i+1]}`, 음수는 끝에서부터),
    `${a[@]}`(따옴표 안에서 원소마다 한 낱말, 원소가 없으면 낱말도 없음), `${a[*]}`(한 낱말), `${#a[@]}`(개수), `${#a[i]}`(원소 길이),
    `${!a[@]}`(인덱스들), `${a[@]:시작:개수}`, 그리고 원소 하나에 `:-` `#` `%` `/` 같은 연산자. `$a` 는 `${a[0]}` 이고 `a=x` 는 `a[0]` 을
    바꿉니다. 내보내지 않은 변수는 `a[1]=x` 로 배열이 되고(원래 값은 `a[0]`), 내보낸 변수는 배열이 될 수 없습니다. `unset a[i]`, `unset a`,
    `read -a 이름`(한 줄의 필드들)이 있고, 서브셸과 `$(...)` 는 배열도 원래대로 돌려 놓으며 백그라운드 작업은 배열을 물려받습니다.
    배열은 내보내지 않습니다. 아직 없는 것: `local` 로 배열 만들기, `a=([2]=x)`, 산술식 안의 `a[1]`, `${a[@]}` 에 연산자(`#` `%` `/`), 연관 배열(`declare -A`).
  - `select NAME [in 낱말...]; do 목록; done`: 낱말(`for` 와 같게 펼침, 없으면 `"$@"`)에 번호를 붙인 메뉴를 표준 오류에 보여 주고(`COLUMNS`,
    기본 80 칸에 맞춰 열을 나눕니다) `PS3`(기본 `#? `)로 물은 뒤 한 줄을 읽어 `REPLY` 에 넣습니다. 낱말의 번호이면 `NAME` 이 그 낱말이 되고, 아니면
    `NAME` 은 빈 값이 되며, 어느 쪽이든 목록을 실행합니다. 빈 줄은 메뉴를 다시 보여 줍니다. `break` 나 입력의 끝(상태는 마지막 목록의 것)에서 끝나고
    `continue` 는 다시 묻습니다. 입력은 파이프나 `<` 로 줄 수 있습니다(`printf '2\n' | select ...`).
  - `list &`: `&&`/`||` 로 이어진 목록 전체를 백그라운드 작업으로 실행하고 바로 다음으로 넘어갑니다(상태 0). `$!` 은 마지막 작업의
    프로세스 ID, `$$` 는 쉘의 프로세스 ID 입니다. 프로그램만으로 된 파이프라인은 기다리지 않고 바로 시작하고, 내장 명령·함수·제어문·묶음은
    `shell -c` 로 따로 실행하는데 이때 내보내지 않은 변수, 함수, 위치 매개변수를 넘겨 줍니다(그 뒤의 변경은 서로 보이지 않습니다).
    작업의 표준 입력은 NUL 이고 Ctrl+C 는 작업에 전달되지 않습니다. `jobs` 는 작업 목록(`[1]+  Running   sleep 5 &`), `wait [PID|%N]...` 는
    기다려서 마지막 작업의 상태를 돌려줍니다(없는 PID 는 127). 터미널에서는 시작할 때 `[1] PID` 를, 끝난 작업은 다음 프롬프트 전에
    `[1]+  Done   명령` 으로 알려 줍니다. 쉘이 끝나도 작업은 계속 돕니다.
  - 작업 이름은 `%N`(또는 `N`), `%%`/`%+`(현재: 마지막에 시작한 것), `%-`(그 전 것), `%문자열`(명령이 그 문자열로 시작), `%?문자열`(포함) 입니다.
    `fg [작업]` 은 명령을 보여 주고 작업이 끝나길 기다려 그 상태를 돌려줍니다(기다리는 중의 Ctrl+C 는 작업을 끝냅니다, 130). `bg [작업]` 은
    `kill -STOP` 으로 멈춘 작업을 다시 돌리고(`[1]+ 명령 &`), 이미 돌고 있으면 "already in background" 라고 합니다. `jobs` 는 멈춘 작업을
    `Stopped` 로 보여 주고 `-l`(PID 도), `-p`(PID 만)가 있습니다. `disown [작업|-a]` 는 끝내지 않고 표에서 지웁니다. `kill` 의 인자에 쓴
    `%작업` 은 그 작업의 모든 프로세스 ID 로 바뀝니다(`kill -9 %1`, `kill %+ %-`). 없는 작업은 메시지와 상태 1 입니다.
    Windows 의 콘솔에는 Ctrl+Z 로 멈추는 일이 없어서, 작업을 멈추려면 `kill -STOP` 을 씁니다. 작업은 NUL 을 입력으로 받으므로 `fg` 로 가져와도
    터미널에서 입력을 받지는 못합니다.
- **스크립트와 명령 치환**
  - `shell FILE [인자...]` 는 파일의 줄을 차례로 실행합니다(`$0` 은 FILE, `$1`..`$9`, `$#`, `$@`, `$*` 은 인자). `shell -c '명령' [이름 [인자...]]`
    도 같습니다. 인자는 몇 개든 됩니다(`${10}` 도 됩니다). `"$@"` 는 인자마다 하나의 단어로 풀리고(`"x$@y"` 는 앞뒤가 첫 인자와 마지막
    인자에 붙고, 인자가 없으면 단어가 없습니다), `"$*"` 는 공백으로 이은 한 단어이며, 따옴표 없는 `$@` 는 공백에서 나뉩니다. `for x; do` 는
    `"$@"` 를 돕니다. `shift [N]` 은 앞의 N 개를 버립니다(모자라면 상태 1). 함수와 서브셸, `$(...)` 는 끝나면 인자를 원래대로 돌려 놓습니다.
    `source FILE [인자...]` 와 `. FILE` 은 같은 쉘에서 실행하며, 인자가 없으면 호출한 쪽의 `$1`.. 을 그대로 봅니다.
    파일에서 `exit [N]` 은 쉘을 끝내고, 스크립트의 종료 상태는 마지막 명령(또는 `exit`)의 상태입니다. 한 줄이 문법 오류이면
    알리고 다음 줄을 실행합니다. 줄 끝은 LF 와 CR LF, 앞의 UTF-8 BOM 은 무시합니다.
  - `$(명령)` 과 `` `명령` `` 은 명령의 표준 출력으로 바뀌며 끝의 줄바꿈은 지워집니다. 따옴표 밖에서는 공백으로 단어가 나뉘고 따옴표 안에서는
    한 단어입니다. 중첩할 수 있고, 안에 `;`, `&&`, 따옴표가 있어도 됩니다. 작은따옴표 안에서는 글자 그대로입니다.
    서브셸처럼 현재 디렉터리와 변수(환경 포함)는 치환이 끝나면 원래대로 돌아오고, 안의 `exit` 는 치환만 끝냅니다.
    `x=$(명령)` 만 있는 줄의 `$?` 는 명령의 상태이고, 그 밖에는 치환 앞뒤의 `$?` 가 바뀌지 않습니다.
  - `#` 는 단어의 시작에서(따옴표 밖) 그 줄의 나머지를 주석으로 만듭니다.
- **산술, 매개변수 확장, read**
  - `$(( 식 ))`: 64비트 정수, C 의 연산자(`+ - * / % ** << >> & | ^ ~ ! && || ?: , < <= > >= == !=`와 대입 `= += -= ...`), 10진수/`0x`/`0` 8진수,
    변수 이름(안 쓴 이름은 0). 0 으로 나누면 오류이고 명령은 실행되지 않습니다. 택하지 않은 가지는 계산하지 않습니다.
  - `${NAME:-w}` `${NAME-w}` `${NAME:=w}` `${NAME:+w}` `${NAME:?w}`, `${#NAME}`, `${NAME#p}` `${NAME##p}` `${NAME%p}` `${NAME%%p}`,
    `${NAME:오프셋}` `${NAME:오프셋:길이}`. 단어 w 와 패턴은 명령의 인자처럼 펼쳐지고, 패턴은 `* ? [..]` 를 씁니다. `:` 가 있으면 빈 값도 없는 것으로 칩니다.
  - `${NAME/패턴/값}` 은 패턴과 맞는 첫 부분(가장 앞에서 시작하고 가장 긴 것)을 값으로 바꿉니다. `${NAME//패턴/값}` 은 모두, `${NAME/#패턴/값}` 은
    맨 앞, `${NAME/%패턴/값}` 은 맨 끝에 맞을 때만 바꾸고, `/값` 을 생략하면 지웁니다. 패턴 안의 `/` 는 `\/` 로 씁니다. 패턴과 값에서 변수와
    치환은 펼쳐지고 공백과 `| < > ;` 는 그냥 글자이며 중괄호 확장은 하지 않습니다. 값의 `&` 는 특별하지 않습니다.
  - `read [-r] [-p 안내] [이름...]`: 입력 한 줄을 `IFS`(기본은 공백)로 잘라 변수에 넣고 마지막 변수가 나머지를 받습니다. 이름이 없으면 `REPLY`.
    `-r` 이 없으면 `\` 가 다음 글자를 보호하고 줄 끝의 `\` 는 다음 줄을 잇습니다. 입력이 끝나면 상태 1. 터미널에서는 `-p` 안내를 표시합니다.
    `IFS=: read a b` 처럼 내장 명령 앞의 `NAME=값` 은 그 명령에만 적용됩니다.
- **제어문**
  - `if LIST; then LIST; [elif LIST; then LIST;]... [else LIST;] fi`, `while LIST; do LIST; done`, `until LIST; do LIST; done`,
    `for NAME [in WORD...]; do LIST; done` 과 `break [N]`, `continue [N]`, `:`. 예약어는 따옴표 없이 명령의 첫 단어일 때만 예약어이고,
    `;` 대신 줄바꿈도 쓸 수 있습니다. `for` 의 단어는 명령의 인자처럼(변수, 치환, 글롭) 펼쳐지고, `in` 이 없으면 `$1`..`$9` 를 하나씩 돕니다.
    `if` 의 상태는 실행된 가지의 상태(없으면 0), 반복문의 상태는 마지막 바퀴의 상태입니다.
  - 제어문은 `&&`/`||` 목록의 한 명령 자리에 쓸 수 있습니다(`if ...; fi && echo`). 끝에 리다이렉션을 달 수 있습니다:
    `while read a b; do ...; done < file`, `for ...; do ...; done > out 2>&1`, `fi >> log`. 리다이렉션은 쉘 자신의 표준 입출력에 적용되어
    안에서 실행되는 내장 명령과 프로그램이 모두 그곳으로 읽고 씁니다.
  - 파이프의 단계로 제어문, `{ ...; }`, `( ... )`, 함수, 내장 명령을 쓸 수 있습니다: `cmd | while read x; do ...; done`, `for ...; done | sort`,
    `printf 'a\n' | f | tr a-z A-Z`. 이런 단계가 하나라도 있으면 단계들을 차례로 실행합니다. 마지막 단계만 현재 쉘에서 돌고(변수 대입이
    남고 `echo v | read w` 가 됩니다) 나머지는 서브셸처럼 끝까지 실행해 출력을 모은 뒤 다음 단계의 입력으로 줍니다. 스트리밍이 아니므로 끝나지
    않는 단계(`yes | f`)는 안 되고, 상태는 마지막 단계의 것입니다. 프로그램만 있는 파이프라인은 그대로 스트리밍입니다.
  - `case WORD in PATTERN[|PATTERN]...) LIST ;; ... esac`: 단어와 패턴은 명령의 인자처럼 펼쳐지고(변수, 치환, 따옴표) 패턴은 `* ? [..]` 를 씁니다.
    따옴표 안의 `*` 는 글자 그대로이고, 처음 맞는 절 하나만 실행됩니다. 맞는 절이 없으면 상태 0, 마지막 절의 `;;` 는 생략할 수 있습니다.
  - 함수: `name() 복합명령` (`name() { ...; }`, `name() if ...; fi` 등). 정의는 세션 동안 남고, 같은 이름으로 다시 정의하면 바뀌며 `unset -f name` 으로 지웁니다.
    호출하면 인자가 `$1`..`$9`, `$#`, `$@`, `$*` 가 되고 끝나면 호출한 쪽의 값이 돌아옵니다(`$0` 은 그대로). `return [N]` 은 함수를 끝내고
    (N 이 없으면 마지막 명령의 상태), `local NAME[=값]` 은 변수를 함수의 것으로 만들어 끝날 때 되돌립니다. 함수는 같은 쉘에서 돌아 변수 대입이
    그대로 남고, 호출은 `&&`/`||` 목록, 리다이렉션(`f > file`), 명령 치환(`$(f x)`), 반복문 안에서 쓸 수 있습니다. 중첩 깊이는 200 까지입니다.
  - `{ LIST; }` 는 현재 쉘에서 목록을 묶어 하나의 명령으로 만들고(리다이렉션을 달 수 있고, 오른쪽에 파이프로 받을 수 있습니다), `( LIST )` 는 서브셸처럼
    현재 디렉터리와 변수가 끝나면 원래대로 돌아오고 안의 `exit` 는 서브셸만 끝냅니다. 예약어 `{` 와 `}` 는 명령의 첫 단어일 때만 그렇습니다.
  - `trap [ACTION] SIGNAL...`: `EXIT`(또는 0), `INT`(2, Ctrl+C), `ERR` 를 다룹니다. ACTION 은 명령 문자열이고 `''` 이면 무시, `-` 이면 되돌립니다.
    `trap` 이나 `trap -p` 는 지금 걸린 것을 `trap -- 'ACTION' NAME` 으로 보여 줍니다. `EXIT` 는 쉘이 끝날 때(스크립트 끝, `exit`) 한 번 실행되고
    트랩 안의 `exit N` 이 쉘의 상태가 됩니다. `INT` 는 Ctrl+C 가 실행 중이던 명령을 멈춘 뒤 트랩을 실행하고 스크립트는 계속됩니다(트랩이 없으면
    반복문이 멈춥니다). `ERR` 는 단순 명령이 실패한 뒤에 실행되며 조건(`if`, `while`), `&&`/`||` 의 왼쪽, 함수 안에서는 실행되지 않습니다.
    서브셸과 `$(...)` 는 트랩 없이 시작합니다(무시한 것만 이어받습니다). `HUP`, `TERM` 같은 그 밖의 신호는 Windows 에 없어서 "signal not supported" 입니다.
  - 입력은 최상위 명령 하나(다음 줄바꿈까지)씩 읽어 통째로 해석한 뒤 실행합니다. 그래서 문법 오류가 있는 줄은 아무것도 실행하지 않고,
    스크립트 파일에서는 오류 앞의 명령들만 실행된 뒤 상태 2로 끝납니다. 터미널에서는 끝나지 않은 `if`/`while`/`for`, 닫히지 않은 따옴표,
    `&&`/`||`/`$(` 로 끝난 줄은 `> ` 프롬프트로 다음 줄을 이어서 받습니다. 반복문 안에서 Ctrl+C 는 그 바퀴가 끝나면 반복을 멈춥니다(상태 130).
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

- 최상위 `Makefile`은 `bin\` 디렉터리를 만든 뒤, 각 서브 프로젝트(`shell`, `arch`, `b2sum`, `base32`, `base64`, `basename`, `cat`, `chgrp`, `chmod`, `chown`, `cksum`, `comm`, `cp`, `csplit`, `cut`, `date`, `dd`, `df`, `diff`, `dir`, `dircolors`, `dirname`, `du`, `echo`, `env`, `expand`, `expr`, `factor`, `false`, `find`, `fmt`, `fold`, `grep`, `groups`, `head`, `hostid`, `hostname`, `id`, `install`, `join`, `kill`, `link`, `ln`, `logname`, `ls`, `md5sum`, `mkdir`, `mktemp`, `mv`, `nice`, `nl`, `nohup`, `nproc`, `numfmt`, `od`, `paste`, `pathchk`, `pinky`, `pr`, `printenv`, `printf`, `ps`, `ptx`, `pwd`, `readlink`, `realpath`, `rm`, `rmdir`, `sed`, `seq`, `sha1sum`, `sha224sum`, `sha256sum`, `sha384sum`, `sha512sum`, `shred`, `shuf`, `sleep`, `sort`, `split`, `stat`, `sum`, `sync`, `tac`, `tail`, `tee`, `test_cmd`, `touch`, `tr`, `true`, `truncate`, `tsort`, `tty`, `uname`, `unexpand`, `uniq`, `unlink`, `uptime`, `users`, `vdir`, `wc`, `which`, `who`, `whoami`, `xargs`, `yes`)의 `Makefile`을 호출합니다.
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
| `dir_test` | `dir`: 열 출력, 이스케이프, 옵션, 오류; `ls -b` |
| `vdir_test` | `vdir`: 긴 형식, 이스케이프, 옵션 |
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
| `shell_control_test` | 쉘 제어문: `if`/`elif`/`else`, `while`, `until`, `for`, `break`/`continue`, 여러 줄, 문법 오류 |
| `test_cmd_test` | `test`, `[`, `true`, `false`: 문자열, 정수, 파일, 식, 인자 개수 규칙, 오류 |
| `shell_cd_test` | 쉘: `cd`, 가상 파일 시스템, 와일드카드 |
| `shell_lists_test` | 쉘: `; && \|\|`, 종료 상태, 변수, `exit` |
| `shell_pipeline_test` | 쉘: 명령들을 파이프, 리다이렉션, 와일드카드와 함께 |
| `equiv_test` | **속성 테스트**: 무작위 입력에서 LF, CRLF, 혼합 줄 끝이 같은 결과를 내는지 (`WS_ITER` 기본 8회, 오래 돌리려면 `WS_ITER=100`; `WS_SEED`) |
| `limits_test` | **한계**: 50만 줄 정렬, 20MB 한 줄, 버퍼 경계(65536±1), 200만 빈 줄, 깊은 경로, 많은 파일 (`WS_STRESS=1` 이면 10배) |
| `sharing_test` | **공유 위반**: 다른 프로세스가 연 파일/디렉터리에 대한 `rm`/`mv`/`cp`, 다른 볼륨으로의 `mv` |
| `async_test` | **비동기**: 조각으로 들어오는 입력, 입력이 닫히길 기다리는 프로그램, `tail -f` |
| `printf_test` | `printf`: 이스케이프, 변환과 플래그, 형식 반복, 문자 상수, 숫자 오류의 상태 |
| `seq_test` | `seq`: 정수/소수, `-w -s -f`, 음수 인자, 오류 |
| `yes_test` | `yes`: `head` 로 끊는 출력, 긴 줄과 많은 줄 |
| `whoami_test` | `whoami`: 사용자 이름 |
| `uname_test` | `uname`: 옵션별 출력과 `-a` 의 순서 |
| `hostname_test` | `hostname`: `-s -f -d`, 이름 바꾸기 거부 |
| `nproc_test` | `nproc`: `--all --ignore`, 최소 1 |
| `tty_test` | `tty`: 터미널이 아닐 때의 출력과 상태 |
| `mktemp_test` | `mktemp`: 이름의 모양, 파일/디렉터리 생성, `-u --suffix -p`, 오류 |
| `readlink_test` | `readlink`: 정션의 대상, 링크가 아닌 것, `-f -e -m` |
| `truncate_test` | `truncate`: 절대/상대 크기, 접미사, `-r -c`, 오류 |
| `fold_test` | `fold`: 너비, `-s -b`, 탭/백스페이스/CR, UTF-8, 여러 파일 |
| `expand_test` | `expand`: 탭 크기와 목록, `-i`, 백스페이스 |
| `unexpand_test` | `unexpand`: 앞 공백, `-a`, 탭 목록, 공백 하나 규칙 |
| `base64_test` | `base64`: RFC 4648 예, `-w`, 디코딩과 오류 |
| `base32_test` | `base32`: RFC 4648 예, `-w`, 디코딩과 오류 |
| `md5sum_test` | `md5sum`: 다이제스트, 줄 형식, `-c` 검사, 오류 |
| `sha1sum_test` | `sha1sum`: 다이제스트, 줄 형식, `-c` 검사, 오류 |
| `sha224sum_test` | `sha224sum`: 다이제스트, 줄 형식, `-c` 검사, 오류 |
| `sha256sum_test` | `sha256sum`: 다이제스트, 줄 형식, `-c` 검사, 오류 |
| `sha384sum_test` | `sha384sum`: 다이제스트, 줄 형식, `-c` 검사, 오류 |
| `sha512sum_test` | `sha512sum`: 다이제스트, 줄 형식, `-c` 검사, 오류 |
| `cksum_test` | `cksum`: 표준 입력과 파일의 CRC, 오류 |
| `ln_test` | `ln`: 하드 링크, 놓일 곳, `-f -i -b -v -T -t`, 디렉터리 정션, 오류 |
| `link_test` | `link`: 하드 링크와 거부되는 것 |
| `unlink_test` | `unlink`: 파일 지우기, 디렉터리 거부 |
| `stat_test` | `stat`: 서식, 기본/간결 출력, 형식, 시간, 링크, `-f` |
| `du_test` | `du`: 합계와 순서, 단위, 하드 링크, `--exclude`, 오류 |
| `split_test` | `split`: 줄/바이트/조각 나누기, 이름(`-d -x -a`, 늘어나는 접미사), 오류 |
| `factor_test` | `factor`: 작은 수, 큰 수(2^64-1), 여러 수, 오류 |
| `tsort_test` | `tsort`: 순서, 공백/줄 구분, CR LF, 순환(보고와 버림), 오류 |
| `sum_test` | `sum`: BSD와 System V 합계, 블록 수, 이름 붙는 경우 |
| `shuf_test` | `shuf`: `--random-source`로 GNU와 같은 순열, 순열이 보존하는 것, `-o -r -z -i -e -n`, 오류 |
| `join_test` | `join`: 짝짓기, `-a -v -e -o`, 구분자, CR LF, 정렬 검사, `--header`, 오류 |
| `od_test` | `od`: 기본 8진수, 형식과 크기, 주소 기수, `-j -N`, `*`, 여러 형식, `z`, `--endian`, 오류 |
| `b2sum_test` | `b2sum`: 알려진 값, `-l`, BSD 형식, 길이가 다른 합계 검사, 오류 |
| `pathchk_test` | `pathchk`: 길이 한계, 이식 가능한 문자, `-P`(빈 이름, 앞의 `-`), 경로 중간의 파일, 메시지 |
| `dd_test` | `dd`: 블록과 개수, skip/seek, 변환, 파일로 복사(seek, notrunc), 통계, 오류 |
| `logname_test` | `logname`: `whoami`와 같은 이름, 오류 |
| `hostid_test` | `hostid`: 16진수 8자리, 늘 같은 값, 오류 |
| `id_test` | `id`: 기본 형식, `-u -g -G -n -r -z`가 서로 맞는지, 이름 준 사용자, 옵션 조합 오류 |
| `groups_test` | `groups`: `id -Gn`과 같은 이름, 사용자 지정, 없는 사용자 |
| `users_test` | `users`: 한 줄의 모양, FILE, 오류 |
| `who_test` | `who`, `users`, `uptime`, `pinky`: 테스트가 쓴 세션 기록으로 출력, 머리글, `-q -T -u -m`, `who am i`, 죽은 프로세스의 기록, 쉘이 스스로 등록하고 끝나면 기록이 사라짐, 오류 |
| `sync_test` | `sync`: 파일과 디렉터리, 옵션 오류 |
| `numfmt_test` | `numfmt`: `--to`/`--from`, 반올림, 패딩, 형식, 접미사, 필드와 구분자, `--invalid`, 오류 |
| `csplit_test` | `csplit`: 줄 번호, 정규식과 오프셋, 건너뛰기, 반복, 옵션, 오류 |
| `df_test` | `df`: 머리글과 열, 단위(-B -h -H -P), inode 열, `--output`, `--total`, `-t -x`, 오류 |
| `nohup_test` | `nohup`: 명령 실행과 상태, 입력 전달, 오류 상태 |
| `nice_test` | `nice`: 우선순위 값, 조정과 옛 형식, 오류 상태 |
| `shred_test` | `shred`: 덮어쓰기와 크기, `-z`, `-u`와 `--remove`, `-v`, 오류 |
| `chmod_test` | `chmod`: 8진수와 기호 모드, 읽기 전용 속성(stat이 보여 주는 모드), `-R`, `--reference`, `-c -v -f`, 오류 |
| `install_test` | `install`: 복사와 덮어쓰기, 디렉터리 대상 `-t -T`, `-D -d`, `-m`, 백업, `-C -p`, 오류 |
| `dircolors_test` | `dircolors`: 쉘 문법, TERM 줄, 키워드와 코드, 인용, 내장 데이터베이스, 오류 |
| `fmt_test` | `fmt`: 문단 채우기, 폭과 목표 폭, 문장 끝과 공백, 들여쓰기, `-u -s -c -t -p`, 탭, 여러 파일, 오류 |
| `chown_test` | `chown`: 현재 사용자(retained), 다른 사용자 거절, `-R`, `--reference`, 없는 이름과 파일, 오류 |
| `chgrp_test` | `chgrp`: 기본 그룹(retained), 다른 그룹 거절, `--reference`, 없는 이름과 파일, 오류 |
| `pr_test` | `pr`: 쪽 모양과 쪽 길이, 열(아래로/가로/`-m`), 번호·들여쓰기·구분자·폭, 탭과 제어 문자, 폼 피드, 쪽 범위, 오류 |
| `ptx_test` | `ptx`: 평문·roff·TeX 형식, 필드와 반대쪽으로 옮겨 가는 문맥, 참조(`-A -r -R`), 폭과 간격, 잘림 표시, 무시/한정 목록, 대소문자, 구분 문자, 낱말·문장 정규식, `-G`, 오류 |

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
├─ printf/       printf 명령
├─ seq/          seq 명령
├─ yes/          yes 명령
├─ whoami/       whoami 명령
├─ uname/        uname 명령
├─ hostname/     hostname 명령
├─ nproc/        nproc 명령
├─ tty/          tty 명령
├─ mktemp/       mktemp 명령
├─ readlink/     readlink 명령
├─ truncate/     truncate 명령
├─ fold/         fold 명령
├─ expand/       expand 명령
├─ unexpand/     unexpand 명령
├─ base64/       base64 명령
├─ base32/       base32 명령
├─ md5sum/       md5sum 명령
├─ sha1sum/      sha1sum 명령
├─ sha224sum/    sha224sum 명령
├─ sha256sum/    sha256sum 명령
├─ sha384sum/    sha384sum 명령
├─ sha512sum/    sha512sum 명령
├─ cksum/        cksum 명령
├─ ln/           ln 명령
├─ link/         link 명령
├─ unlink/       unlink 명령
├─ stat/         stat 명령
├─ du/           du 명령
├─ split/        split 명령
├─ factor/       factor 명령
├─ tsort/        tsort 명령
├─ sum/          sum 명령
├─ shuf/         shuf 명령
├─ join/         join 명령
├─ od/           od 명령
├─ b2sum/        b2sum 명령
├─ pathchk/      pathchk 명령
├─ dd/           dd 명령
├─ logname/      logname 명령
├─ hostid/       hostid 명령
├─ id/           id 명령
├─ groups/       groups 명령
├─ users/        users 명령
├─ sync/         sync 명령
├─ numfmt/       numfmt 명령
├─ csplit/       csplit 명령
├─ df/           df 명령
├─ nohup/        nohup 명령
├─ nice/         nice 명령
├─ shred/        shred 명령
├─ chmod/        chmod 명령
├─ install/      install 명령
├─ dircolors/    dircolors 명령
├─ fmt/          fmt 명령
├─ chown/        chown 명령
├─ chgrp/        chgrp 명령
├─ pr/           pr 명령
├─ ptx/          ptx 명령
├─ dir/          dir 명령
├─ vdir/         vdir 명령
├─ pinky/        pinky 명령
├─ uptime/       uptime 명령
├─ who/          who 명령
├─ common/         여러 명령이 함께 쓰는 코드 (vpath: 가상 경로 변환, fsutil: 복사/삭제, textutil: 텍스트 입력과 줄 나누기, args: 옵션 파서, regex: 정규식, vec: 자라는 배열)
├─ test/           테스트 (nmake test)
└─ bin/            빌드 결과물 (git 추적 제외)
```

## 새 명령어 추가하기

1. `nmake new NAME=<이름>` 을 실행합니다. 동작하는 뼈대(`<이름>\main.c`, `Makefile`), `test\<이름>_test.c`, `test\gnu\<이름>.sh`
   를 만들고 두 레퍼런스(`docs\REFERENCE.md`, `docs\REFERENCE.en.md`)의 명령 표, 테스트 표, 디렉터리 구조에 항목을 넣습니다.
2. `main.c` 를 작성합니다. 옵션은 `common/args.h` 의 `args_parse` 로 처리합니다(`tee/main.c` 가 가장 작은 예입니다).
   도구의 `Makefile` 은 `NAME`, `SRCS`, `HDRS`(필요하면 `INCLUDES`, `LIBS`)만 정하고 규칙은 `common.mk` 를 포함해서 얻습니다.
3. 레퍼런스의 명령 표 행을 채우고(README 의 "명령어" 분류에도 이름을 넣어 주세요) 테스트와 GNU 대조(`test\gnu\<이름>.sh`)를 작성합니다.
   `nmake check-registry` 가 레퍼런스나 테스트에 빠진 도구를 알려 줍니다.

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

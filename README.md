<div align="center">

# Winshell

**Windows에서 네이티브로 도는 bash 스타일 쉘과 GNU 스타일 coreutils.**<br>
순수 C와 Win32 API만으로, 처음부터 직접 만들었습니다.

[English](README.en.md) | **한국어**

![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform: Windows](https://img.shields.io/badge/platform-Windows-0078D6.svg)
![Language: C](https://img.shields.io/badge/language-C-555555.svg)
![Runtime deps: none](https://img.shields.io/badge/runtime%20deps-none-brightgreen.svg)

</div>

<!-- TODO: 데모 GIF (예: docs/demo.gif) — shell.exe 에서 grep, find, 파이프, 한글 파일명을 쓰는 20초 녹화 -->

```sh
cd ~/projects                          # C:\Users\me\projects 는 /c/Users/me/projects
grep -rn TODO --include='*.c' . | head -3
find . -name '*.log' -mtime +7 -delete
ls *.md | xargs wc -l
for f in 보고서*.txt; do n=$(wc -l < "$f"); echo "$f: ${n}줄"; done
```

## 왜 Winshell인가

- **에뮬레이션 계층 없음.** MSYS2나 Cygwin처럼 POSIX 에뮬레이션 DLL 위에서 도는 것이 아니라, 모든 명령이 Win32 API를 직접 부르는 독립 실행 파일입니다.
  Windows 기본 DLL(`KERNEL32` 등)만 쓰고 CRT를 정적 링크하므로 Visual C++ 재배포 패키지도 필요 없습니다. 실행 파일 하나가 110–260KB입니다.
- **GNU와 같은 결과.** 29개 도구를 3천여 가지 옵션·입력 조합으로 실제 GNU 도구와 **출력 바이트 단위로 대조**합니다(`nmake gnu`).
  `diff` 는 GNU diff와 같은 Myers 알고리즘을 써서, 같은 답이 여럿일 때도 GNU와 같은 것을 고릅니다.
- **Windows를 이해하는 쉘.** `/c/Users/me` 와 `C:\Users\me` 를 둘 다 쓸 수 있고, CRLF 파일을 다뤄도 줄 끝이 그대로 남으며,
  한글 파일명과 `[가-힣]` 같은 글롭이 바이트가 아니라 글자 단위로 동작합니다.
- **진짜 쉘 언어.** 파이프와 리다이렉션은 물론 `if`/`while`/`for`/`case`, 함수와 `local`, `$(...)`, `$((...))`, `${NAME:-값}`, `trap`, `read`, 스크립트 실행까지 지원합니다.
- **읽을 수 있는 코드.** 정규식 엔진, 날짜 해석기, 옵션 파서까지 모두 직접 구현한 약 3.9만 줄(테스트 포함)의 C 코드입니다. 쉘과 coreutils가 어떻게 동작하는지 공부하기에도 좋습니다.

### 다른 선택지와 비교

|  | **Winshell** | Git Bash (MSYS2) | Cygwin | WSL |
| --- | --- | --- | --- | --- |
| 동작 방식 | Win32 네이티브 실행 파일 | `msys-2.0.dll` 위의 GNU 도구 | `cygwin1.dll` 위의 GNU 도구 | Linux 커널 위의 Linux 배포판 |
| 필요한 것 | `bin\` 폴더 하나 | Git for Windows 설치 | Cygwin 설치 | WSL 기능 + 배포판 |
| 기본 경로 표기 | `/c/...` (`C:\...` 도 그대로) | `/c/...` | `/cygdrive/c/...` | `/mnt/c/...` |
| GNU 호환성 | 주요 옵션 구현, GNU 출력과 대조 검증 | GNU 그 자체 | GNU 그 자체 | GNU 그 자체 |

GNU 도구의 모든 기능이 꼭 필요하다면 Git Bash나 WSL이 더 알맞습니다. Winshell은 **가볍고, 설치가 필요 없고, Windows 위에서 자연스럽게 동작하는** 쪽을 목표로 합니다.

## 빠른 시작

미리 빌드된 바이너리는 아직 없어서 직접 빌드해야 합니다. **Visual Studio** 또는 **Build Tools for Visual Studio**(C++ 데스크톱 개발 워크로드)가 필요합니다.

*Developer Command Prompt for VS* 에서:

```bat
git clone https://github.com/felix6631/Winshell.git
cd Winshell
nmake
bin\shell.exe
```

쉘은 자기 옆의 `bin\` 에서만 명령을 찾으므로 `PATH` 를 바꿀 필요가 없고, Windows의 `find`, `sort` 와 섞이지도 않습니다.

```sh
fact() {
  if [ $1 -le 1 ]; then echo 1; return; fi
  p=$(fact $(($1 - 1)))
  echo $(($1 * p))
}
fact 10        # 3628800
```

## 명령어

쉘(`shell`)과 43개의 명령이 있습니다. 명령별 지원 옵션은 [레퍼런스](docs/REFERENCE.md#구현-현황)에 정리되어 있습니다.

| 분류 | 명령 |
| --- | --- |
| 파일과 디렉터리 | `ls` `cp` `mv` `rm` `mkdir` `rmdir` `touch` `find` `realpath` `basename` `dirname` `pwd` |
| 텍스트 처리 | `cat` `head` `tail` `grep` `sed` `sort` `uniq` `cut` `tr` `wc` `nl` `paste` `tac` `tee` `comm` `diff` |
| 출력과 계산 | `echo` `printf` `expr` `date` |
| 스크립트 도우미 | `test` `[` `true` `false` `xargs` `env` `printenv` `which` `sleep` |
| 프로세스와 시스템 | `ps` `kill` `arch` |

<details>
<summary><b>구현 하이라이트</b></summary>

- **`diff`**: GNU diff와 같은 알고리즘(Myers, 중간 스네이크, 끝의 공통 줄, 이동, 큰 입력 휴리스틱). 일반 / `-u` / `-c` 형식과 디렉터리 비교.
- **정규식 엔진** (`common/regex.c`): 기본/확장 정규식, 역참조, 가장 왼쪽의 가장 긴 일치, UTF-8 문자 단위. `grep`, `sed`, `find`, `expr` 가 함께 씁니다.
- **`sed`**: `s y p P d D n N g G h H x l = a i c r R w W q Q b t T z F` 등 거의 모든 명령과 `-i[SUFFIX]`, `first~step`, `0,/re/` 주소.
- **`date -d`**: `3 weeks ago`, `next month`, `yesterday`, ISO 날짜, `@초`. 1970년 이전과 3000년 이후도 계산합니다.
- **`expr`**: 크기 제한 없는 정수.
- **`find`**: `-exec ... {} +`, `-execdir`, `-printf`, `-delete`, `-prune`. **`xargs -P`** 로 병렬 실행.
- **`kill`**: 시그널이 없는 Windows에 맞춰 `STOP`/`CONT` 는 중지/재개, `KILL` 은 즉시 종료, 나머지는 창에 `WM_CLOSE`.
- **안전장치**: 글롭은 숨김/시스템 파일에 일치하지 않고(`rm *` 이 시스템 파일을 건드리지 않음), `rm` 은 드라이브와 공유의 루트를 지우지 않습니다.

</details>

## 테스트

```bat
nmake test      :: 38개 테스트 프로그램을 병렬로 실행
nmake asan      :: 모든 도구와 테스트를 AddressSanitizer 로 다시 빌드해서 실행
nmake gnu       :: GNU 도구와 출력 대조 (MSYS2 나 Git for Windows 의 bash 필요, 약 30초)
```

단위 테스트 외에도 LF/CRLF/혼합 줄 끝이 같은 결과를 내는지 보는 **속성 테스트**, 50만 줄 정렬과 20MB 한 줄 같은 **한계 테스트**,
다른 프로세스가 연 파일을 다루는 **공유 위반 테스트**, 조각으로 들어오는 입력을 다루는 **비동기 테스트**가 있습니다. 자세한 내용은 [레퍼런스](docs/REFERENCE.md#테스트)를 보세요.

## 문서

- [레퍼런스](docs/REFERENCE.md): 명령별 옵션, 쉘 문법, 빌드 구조, 테스트, 디렉터리 구조, 설계 방향
- [새 명령어 추가하기](docs/REFERENCE.md#새-명령어-추가하기): `nmake new NAME=<이름>` 한 줄로 뼈대, 테스트, GNU 대조 파일이 만들어집니다

## 로드맵

Winshell은 활발히 개발 중입니다.

- **쉘**: Ctrl+Z 로 작업 멈추기, `select`, `trap` 의 다른 신호
- **호환성**: Windows XP까지 동작하는 것이 목표입니다(아직 검증 전). UTF-8 동작은 Windows 10 1903 이상에서 적용됩니다.
- **coreutils**

<details>
<summary>구현 예정 명령 펼치기</summary>

b2sum, base32, base64, chgrp, chmod, chown, cksum, csplit, dd, df, dir, dircolors, du, expand, factor, fmt, fold,
groups, hostid, hostname, id, install, join, link, ln, logname, md5sum, mkfifo, mknod, mktemp, nice, nohup, nproc,
numfmt, od, pathchk, pinky, pr, ptx, readlink, seq, sha1sum, sha224sum, sha256sum, sha384sum, sha512sum, shred,
shuf, split, stat, stdbuf, sum, sync, truncate, tsort, tty, uname, unexpand, unlink, uptime, users, vdir, who,
whoami, yes

대상 제외(Windows에서 의미가 없거나 대응 개념이 없음): chcon, chroot, runcon, stty, coreutils(멀티콜 바이너리)

</details>

## 기여하기

이슈와 PR 모두 환영합니다. 처음이라면 `yes`, `seq`, `whoami`, `base64`, `fold` 같은 작은 명령부터 시작해 보세요.

```bat
nmake new NAME=yes      :: yes\main.c, test\yes_test.c, test\gnu\yes.sh 와 레퍼런스 항목을 만듭니다
nmake test              :: 테스트
nmake check-registry    :: 레퍼런스와 테스트에 빠진 것이 없는지 확인
```

## 라이선스

[MIT](LICENSE)

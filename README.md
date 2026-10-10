# Winshell

[English](README.en.md) | **한국어**

Winshell은 리눅스 스타일의 쉘과 coreutils를 Windows에서 사용하기 위한 프로젝트이자 툴체인입니다.
순수 C와 Win32 API만으로 작성하며, 최종 목표는 Windows XP까지 동작하는 것입니다.

> **상태: 개발 초기.** 쉘은 한 줄을 읽어 `bin\<명령>.exe`를 실행하는 수준이며,
> 가상 루트 디렉터리, 환경 변수 확장, 글롭 등은 아직 구현되지 않았습니다.

## 구현 현황

| 명령 | 상태 |
| --- | --- |
| `shell` | 입력 루프, 인자 전달 및 명령 실행(종료 대기), `quit` 로 종료 |
| `arch` | 완료 |
| `echo` | 완료 (`-n`, `-e`, `-E`, `--help`, `--version`) |
| `pwd` | 완료 (현재는 Windows 경로를 그대로 출력) |

구현 예정 목록은 [아래](#구현-예정)를 참고하세요.

## 쉘 사용법

- 프로그램은 `shell.exe`가 있는 디렉터리(`bin\`)에서만 찾으며, 확장자 `.exe`는 생략할 수 있습니다.
- `quit` 으로 종료합니다. 입력 끝(Ctrl+Z, Enter)에서도 종료합니다.
- **따옴표**
  - `"a b"` : 공백을 유지합니다. 안에서 `\"` 는 따옴표 문자입니다.
  - `'a b'` : 안의 모든 문자를 그대로 사용합니다.
  - `a"b c"d` 처럼 섞어 쓸 수 있으며, `""` 는 빈 인자가 됩니다.
  - 백슬래시는 큰따옴표 앞에서만 특별하게 취급하므로 `C:\bin\echo` 같은 경로는 그대로 입력하면 됩니다.
  - 따옴표를 닫지 않으면 오류를 출력하고 명령을 실행하지 않습니다.
- **파이프와 리다이렉션**
  - `a | b` : a의 표준 출력을 b의 표준 입력으로 연결합니다. 모든 명령이 동시에 실행됩니다.
  - `< 파일` : 표준 입력을 파일에서 읽습니다.
  - `> 파일`, `>> 파일` : 표준 출력을 파일에 쓰거나(덮어쓰기), 이어 씁니다.
  - `2> 파일`, `2>> 파일` : 표준 오류를 파일에 쓰거나, 이어 씁니다.
  - 연산자 주변의 공백은 생략할 수 있습니다(`echo hi>out.txt`). 따옴표 안에서는 연산자로 해석하지 않습니다.
  - 리다이렉션은 파이프보다 우선합니다. 마지막 명령의 종료 코드가 결과가 됩니다.
  - 아직 `2>&1`, `&`, `&&`, `||`, `;`는 지원하지 않습니다.
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

- 최상위 `Makefile`은 `bin\` 디렉터리를 만든 뒤, 각 서브 프로젝트(`shell`, `arch`, `echo`, `pwd`)의 `Makefile`을 호출합니다.
- 각 서브 프로젝트의 `Makefile`은 자기 디렉터리의 소스를 빌드하여 `bin\<이름>.exe`를 만듭니다.
- `shell.exe`는 기본적으로 `bin\` 에 있는 실행 파일만을 사용합니다.

## 디렉터리 구조

```
Winshell/
├─ Makefile        최상위 빌드 규칙
├─ shell/          쉘 본체 (include/, src/)
├─ arch/           arch 명령
├─ echo/           echo 명령
├─ pwd/            pwd 명령
└─ bin/            빌드 결과물 (git 추적 제외)
```

## 새 명령어 추가하기

1. 최상위에 명령 이름의 디렉터리를 만들고 `main.c` 를 작성합니다.
2. `arch/Makefile` 을 복사하여 `NAME` 을 수정합니다.
3. 최상위 `Makefile` 의 `all`, `clean` 목록에 `build-<이름>`, `clean-<이름>` 을 추가합니다.

각 서브 프로젝트의 `Makefile` 은 `all`, `clean` 규칙을 제공하고 `BINDIR` 매크로로 출력 위치를 받아야 합니다.

## 설계 방향

- **가상 루트 디렉터리 (예정):** UNIX 파일 시스템과 유사하게 논리적인 루트 디렉터리를 두고, 각 드라이브를 그 아래에 배치할 계획입니다.
- **호환성 (목표):** 기초적인 시스템 콜을 이용해 Windows XP까지의 호환성을 확보하는 것이 목표이며, 아직 검증되지 않았습니다.

## 구현 예정

**쉘 기능**: `2>&1`, 환경 변수 확장, 글롭, 가상 루트 디렉터리

**coreutils**

<details>
<summary>목록 펼치기</summary>

`[`, b2sum, base32, base64, basename, cat, chgrp, chmod, chown, cksum, comm, cp, csplit, cut, date, dd, df, dir,
dircolors, dirname, du, env, expand, expr, factor, false, fmt, fold, groups, head, hostid, hostname, id, install,
join, kill, link, ln, logname, ls, md5sum, mkfifo, mknod, mktemp, mv, nice, nl, nohup, nproc, numfmt, od, paste,
pathchk, pinky, pr, printenv, printf, ptx, readlink, realpath, rm, rmdir, seq, sha1sum, sha224sum, sha256sum,
sha384sum, sha512sum, shred, shuf, sleep, sort, split, stat, stdbuf, sum, sync, tac, tail, tee, test, tr, true,
truncate, tsort, tty, uname, unexpand, uniq, unlink, uptime, users, vdir, wc, who, whoami, yes

</details>

**대상 제외 (Windows에서 의미가 없거나 대응 개념이 없음)**: chcon, chroot, runcon, stty, coreutils(멀티콜 바이너리)

## 라이선스

[Apache License 2.0](LICENSE)

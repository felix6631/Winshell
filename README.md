# Winshell by Felix Brown

## What is Winshell?
Winshell은 리눅스 스타일의 쉘과 coreutils를 Windows에서 사용하기 위한 프로젝트 및 툴체인입니다.
Winshell is project/toolchain to use linux style shell and coreutils in Windows.

## Implemented
2026.10.09. 기준 개발이 완료된 것은 아래와 같습니다.
Followings are implemented; by the time 2026. 10. 09.
- arch
- echo
- pwd

## TODO
- |
- <
- [
- b2sum
- base32
- base64
- basename
- cat
- chcon
- chgrp
- chmod
- chown
- chroot
- cksum
- comm
- coreutils
- cp
- csplit
- cut
- date
- dd
- df
- dir
- dircolors
- dirname
- du
- env
- expand
- expr
- factor
- false
- fmt
- fold
- groups
- head
- hostid
- hostname
- id
- install
- join
- kill
- link
- ln
- logname
- ls
- md5sum
- mkfifo
- mknod
- mktemp
- mv
- nice
- nl
- nohup
- nproc
- numfmt
- od
- paste
- pathchk
- pinky
- pr
- printenv
- printf
- ptx
- readlink
- realpath
- rm
- rmdir
- runcon
- seq
- sha1sum
- sha224sum
- sha256sum
- sha384sum
- sha512sum
- shred
- shuf
- sleep
- sort
- split
- stat
- stdbuv
- stty
- sum
- sync
- tac
- tail
- tee
- test
- tr
- true
- truncate
- tsort
- tty
- uname
- unexpand
- uniq
- unlink
- uptime
- users
- vdir
- wc
- who
- whoami
- yes

## Build
빌드는 다음과 같이 작동합니다.
- 최상위 디렉터리의 Makefile은 루트 디렉터리에 \bin 디렉터리를 만든 후, 각 하위 디렉터리의 Makefile을 실행합니다.
- 각 하위 디렉터리에 있는 Makefile은 해당 위치의 소스를 이용해 빌드를 시행합니다.
- 빌드 후, 각 실행파일은 프로젝트 루트\bin에 위치하게 됩니다. 셸 실행파일(shell.exe)는 기본적으로 이 위치에 있는 실행파일만을 이용합니다.

## 특이사항
- UNIX 파일 시스템과 유사하게, 루트 디렉터리가 논리적으로 존재합니다. 각 드라이브는 루트 디렉터리 아래에 배치되게 됩니다.
- 기초적인 시스템 콜을 이용하여, Windows XP까지의 호환성을 확보하는 것이 목표입니다.
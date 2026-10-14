## 테스트 빌드/실행 규칙 (NMAKE / MSVC). 보통은 최상위 Makefile의 `nmake test` 로 호출됩니다.
##
## 단위 테스트
##   vpath_test          common\vpath.c 의 단위 테스트 (가상 경로 변환)
##   parser_test         쉘 파서의 단위 테스트 (따옴표, 변수, 목록, 파이프, 리다이렉션)
##   args_test           common\args.c 의 단위 테스트 (도구의 옵션 파서)
##   regex_test          common\regex.c 의 단위 테스트 (정규식 엔진)
## 명령을 프로그램으로 실행해서 확인하는 테스트 (bin\ 의 프로그램이 필요)
##   basic_test          echo, arch, pwd
##   cat_test            cat
##   ls_test             ls
##   fileops_test        rm, cp, mv
##   dirs_test           mkdir, rmdir, touch
##   text_test           head, tail, wc
##   filter_test         sort, uniq, cut, tr
##   env_test            env, printenv
##   lines_test          tee, tac, nl, paste
##   shell_utf8_test     쉘: 한글 이름, 와일드카드가 글자 하나에 일치함, 변수
##   clock_test          sleep, date: 대기 시간, 형식, -d 해석, 종료 상태
##   which_test          which: 프로젝트 bin, PATH, PATHEXT, 경로가 있는 이름, 종료 상태
##   comm_test           comm: 열, 열 숨기기, CR LF, 정렬 검사, --total, 구분자, 표준 입력, 종료 상태
##   proc_test           kill, ps: 실제 프로세스(sleep)에 대한 선택, 형식, 정렬, 시그널, 종료 상태
##   realpath_test       realpath: 가상 경로, 존재 규칙, 상대 경로, 정션
##   names_test          basename, dirname: 구분자, 접미사, NUL 끝
##   xargs_test          xargs: 묶음, 항목 해석, 종료 상태, 병렬 실행
##   find_test           find: 걷기, 동작(-exec -delete), 가상 경로, 한글 이름, 종료 상태
##   expr_test           expr: 산술, 비교, 문자열 함수, 무제한 크기 정수, 종료 상태
##   sed_test            sed: 치환, 주소, 명령, -i, w/r 파일, 줄 끝, 종료 상태
##   grep_test           grep: CRLF, UTF-8, 바이너리, -r, 문맥, 흐르는 입력
##   shell_run_test      쉘: 명령 실행, 따옴표, 파이프, 리다이렉션
##   shell_cd_test       쉘: cd, 가상 파일 시스템, 와일드카드
##   shell_lists_test    쉘: ; && ||, 종료 상태, 변수, exit
##   shell_pipeline_test 쉘: 명령들을 파이프, 리다이렉션, 와일드카드와 함께
## 성질과 한계를 확인하는 테스트
##   equiv_test          LF 와 CRLF 입력의 동등성을 무작위 입력으로 확인 (속성 테스트)
##   limits_test         큰 입력과 경계 (버퍼 크기, 긴 줄, 많은 줄, 깊은 경로)
##   sharing_test        열려 있는 파일에 대한 rm/mv/cp, 다른 드라이브로의 mv
##   async_test          계속 실행되는 프로그램 (tail -f, 늦게 닫히는 표준 입력)
##
## 사용법
##   nmake check         빌드하고 모두 실행
##   nmake check-asan    AddressSanitizer 로 빌드한 프로그램과 테스트로 모두 실행
##                       (먼저 루트에서 nmake asan 으로 bin-asan\ 을 만들어야 합니다; nmake asan 이 둘 다 합니다)
##
## 새 테스트 프로그램은 이 디렉터리에 <이름>_test.c 를 만들면 됩니다(목록은 scripts\list-tests.bat 가 만듭니다).
## 다른 소스를 링크해야 하는 단위 테스트만 아래에 규칙을 따로 적고, 위 TESTS 와 scripts\list-tests.bat 의 제외 목록에 넣습니다.
##
## 결과물은 test\out\ (asan 은 test\out-asan\) 에 만들어지고(git 추적 제외),
## 작업 파일은 그 아래 work\ 에 만들어집니다.

OUT    = out
EXTRA  =
CFLAGS = /nologo /utf-8 /W3 /O2 /D_CRT_SECURE_NO_WARNINGS /I..\common /I..\shell\include /Fd$(OUT)\ /FS $(EXTRA)

# The unit tests that link a part of the project have a rule of their own below; every other
# *_test.c here is found by scripts\list-tests.bat (a new test file needs no entry in a list).
!IF [..\scripts\list-tests.bat > tests.inc] != 0
!ERROR ..\scripts\list-tests.bat could not be run
!ENDIF
!INCLUDE tests.inc

TESTS = $(OUT)\vpath_test.exe $(OUT)\parser_test.exe $(OUT)\args_test.exe $(OUT)\regex_test.exe $(GENERIC_TESTS)

all: $(TESTS)

# runs the test programs, several at a time
$(OUT)\runall.exe: runall.c
	@if not exist $(OUT) mkdir $(OUT)
	cl $(CFLAGS) /Fo$(OUT)\ /Fe$@ runall.c

check: all $(OUT)\runall.exe
	@$(OUT)\runall.exe $(OUT)

# the program that runs the combinations of gnu_compare.sh (nmake gnu at the top builds it)
gnu: $(OUT)\gnu_batch.exe

check-asan:
	@$(MAKE) /nologo OUT=out-asan EXTRA="/fsanitize=address /Zi" all out-asan\runall.exe
	@.\runasan.bat

.SUFFIXES: .c .exe

# a test program of one file that runs the programs of the project
{.}.c{$(OUT)}.exe:
	@if not exist $(OUT) mkdir $(OUT)
	cl $(CFLAGS) /Fo$(OUT)\ /Fe$@ $< /link /MANIFEST:EMBED /MANIFESTINPUT:..\common\utf8.manifest

$(GENERIC_TESTS): testlib.h testtools.h

$(OUT)\vpath_test.exe: vpath_test.c testlib.h ..\common\vpath.c ..\common\vpath.h
	@if not exist $(OUT) mkdir $(OUT)
	cl $(CFLAGS) /Fo$(OUT)\ /Fe$@ vpath_test.c ..\common\vpath.c /link /MANIFEST:EMBED /MANIFESTINPUT:..\common\utf8.manifest

$(OUT)\regex_test.exe: regex_test.c testlib.h ..\common\regex.c ..\common\utf8.c ..\common\regex.h ..\common\vec.h
	@if not exist $(OUT) mkdir $(OUT)
	cl $(CFLAGS) /Fo$(OUT)\ /Fe$@ regex_test.c ..\common\regex.c ..\common\utf8.c /link /MANIFEST:EMBED /MANIFESTINPUT:..\common\utf8.manifest

$(OUT)\args_test.exe: args_test.c testlib.h ..\common\args.c ..\common\args.h
	@if not exist $(OUT) mkdir $(OUT)
	cl $(CFLAGS) /Fo$(OUT)\ /Fe$@ args_test.c ..\common\args.c /link /MANIFEST:EMBED /MANIFESTINPUT:..\common\utf8.manifest

$(OUT)\parser_test.exe: parser_test.c testlib.h ..\shell\src\parser.c ..\shell\src\list.c ..\shell\src\variables.c ..\shell\include\parser.h ..\shell\include\variables.h ..\common\vpath.c
	@if not exist $(OUT) mkdir $(OUT)
	cl $(CFLAGS) /Fo$(OUT)\ /Fe$@ parser_test.c ..\shell\src\parser.c ..\shell\src\list.c ..\shell\src\variables.c ..\common\vpath.c /link /MANIFEST:EMBED /MANIFESTINPUT:..\common\utf8.manifest

clean:
	@if exist out rmdir /s /q out
	@if exist out-asan rmdir /s /q out-asan

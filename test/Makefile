## 테스트 빌드/실행 규칙 (NMAKE / MSVC). 보통은 최상위 Makefile의 `nmake test` 로 호출됩니다.
##
## 테스트 프로그램:
##   vpath_test      common\vpath.c 의 단위 테스트 (가상 경로 변환)
##   parser_test     쉘 파서의 단위 테스트 (따옴표, 파이프, 리다이렉션)
##   commands_test   bin\ 의 명령(echo, arch, pwd, cat)을 프로그램으로 실행해서 확인
##   shell_test      bin\shell.exe 에 스크립트를 입력해 쉘 전체를 확인
##
## commands_test, shell_test 는 bin\ 에 빌드된 프로그램이 필요합니다.
## 결과물은 test\out\ 에 만들어지고(git 추적 제외), 작업 파일은 test\out\work\ 에 만들어집니다.

OUT    = out
CFLAGS = /nologo /utf-8 /W3 /O2 /D_CRT_SECURE_NO_WARNINGS /I..\common /I..\shell\include
TESTS  = $(OUT)\vpath_test.exe $(OUT)\parser_test.exe $(OUT)\commands_test.exe $(OUT)\shell_test.exe

all: $(TESTS)

check: all
	@$(OUT)\vpath_test.exe
	@$(OUT)\parser_test.exe
	@$(OUT)\commands_test.exe
	@$(OUT)\shell_test.exe

$(OUT):
	@if not exist $(OUT) mkdir $(OUT)

$(OUT)\vpath_test.exe: vpath_test.c testlib.h ..\common\vpath.c ..\common\vpath.h
	@if not exist $(OUT) mkdir $(OUT)
	cl $(CFLAGS) /Fo$(OUT)\ /Fe$(OUT)\vpath_test.exe vpath_test.c ..\common\vpath.c

$(OUT)\parser_test.exe: parser_test.c testlib.h ..\shell\src\parser.c ..\shell\include\parser.h ..\common\vpath.c
	@if not exist $(OUT) mkdir $(OUT)
	cl $(CFLAGS) /Fo$(OUT)\ /Fe$(OUT)\parser_test.exe parser_test.c ..\shell\src\parser.c ..\common\vpath.c

$(OUT)\commands_test.exe: commands_test.c testlib.h
	@if not exist $(OUT) mkdir $(OUT)
	cl $(CFLAGS) /Fo$(OUT)\ /Fe$(OUT)\commands_test.exe commands_test.c

$(OUT)\shell_test.exe: shell_test.c testlib.h
	@if not exist $(OUT) mkdir $(OUT)
	cl $(CFLAGS) /Fo$(OUT)\ /Fe$(OUT)\shell_test.exe shell_test.c

clean:
	@if exist $(OUT) rmdir /s /q $(OUT)

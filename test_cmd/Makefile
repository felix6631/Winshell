## test 와 [ 빌드 규칙 (NMAKE / MSVC). 보통은 최상위 Makefile을 통해 호출됩니다.
##
## 디렉터리 test 는 테스트 모음이 쓰고 있어서 이 도구의 디렉터리는 test_cmd 입니다. 같은 프로그램이
## bin\test.exe 와 bin\[.exe 로 설치됩니다(프로그램이 자기 이름을 보고 [ 일 때는 ] 를 요구합니다).

NAME     = test
SRCS     = main.c ..\common\vpath.c ..\common\fsutil.c
HDRS     = ..\common\vpath.h ..\common\fsutil.h

!INCLUDE ..\common.mk

all: "$(BINDIR)\[.exe"

"$(BINDIR)\[.exe": $(TARGET)
	@copy /y "$(TARGET)" "$(BINDIR)\[.exe" >nul

clean: clean-bracket

clean-bracket:
	@if exist "$(BINDIR)\[.exe" del "$(BINDIR)\[.exe"

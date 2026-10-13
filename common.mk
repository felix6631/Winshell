## 모든 도구의 빌드 규칙 (NMAKE / MSVC). 각 도구의 Makefile 이 !INCLUDE 합니다.
##
## 도구의 Makefile 은 다음만 정의하면 됩니다.
##   NAME      도구 이름 (bin\NAME.exe 가 만들어집니다)
##   SRCS      소스 파일 (main.c 와 ..\common\ 의 공용 코드)
##   HDRS      헤더 파일 (바뀌면 다시 빌드합니다)
##   INCLUDES  (선택) 추가 /I 옵션, 예: /Iinclude
##   LIBS      (선택) 추가 라이브러리, 예: advapi32.lib
##
## BINDIR 과 EXTRA_CFLAGS 는 최상위 Makefile 이 명령줄로 넘깁니다 (nmake asan 이 /fsanitize=address 를 넣습니다).

BINDIR = ..\bin
OBJDIR = obj
TARGET = $(BINDIR)\$(NAME).exe
CFLAGS = /nologo /utf-8 /W3 /O2 /D_CRT_SECURE_NO_WARNINGS /I..\common $(INCLUDES) $(EXTRA_CFLAGS)

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS) ..\common\vec.h ..\common\utf8.manifest
	@if not exist "$(BINDIR)" mkdir "$(BINDIR)"
	@if not exist $(OBJDIR) mkdir $(OBJDIR)
	cl $(CFLAGS) /Fo$(OBJDIR)\ /Fe"$(TARGET)" $(SRCS) $(LIBS) /link /MANIFEST:EMBED /MANIFESTINPUT:..\common\utf8.manifest

clean:
	@if exist "$(TARGET)" del "$(TARGET)"
	@if exist $(OBJDIR) rmdir /s /q $(OBJDIR)

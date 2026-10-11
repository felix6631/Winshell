## wc 빌드 규칙 (NMAKE / MSVC). 보통은 최상위 Makefile을 통해 호출됩니다.

NAME   = wc
BINDIR = ..\bin
OBJDIR = obj
TARGET = $(BINDIR)\$(NAME).exe
CFLAGS = /nologo /utf-8 /W3 /O2 /D_CRT_SECURE_NO_WARNINGS /I..\common
SRCS   = main.c ..\common\vpath.c ..\common\fsutil.c ..\common\textutil.c
HDRS   = ..\common\vpath.h ..\common\fsutil.h ..\common\textutil.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	@if not exist "$(BINDIR)" mkdir "$(BINDIR)"
	@if not exist $(OBJDIR) mkdir $(OBJDIR)
	cl $(CFLAGS) /Fo$(OBJDIR)\ /Fe"$(TARGET)" $(SRCS)

clean:
	@if exist "$(TARGET)" del "$(TARGET)"
	@if exist $(OBJDIR) rmdir /s /q $(OBJDIR)

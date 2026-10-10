## shell 빌드 규칙 (NMAKE / MSVC). 보통은 최상위 Makefile을 통해 호출됩니다.

NAME   = shell
BINDIR = ..\bin
OBJDIR = obj
TARGET = $(BINDIR)\$(NAME).exe
CFLAGS = /nologo /utf-8 /W3 /O2 /D_CRT_SECURE_NO_WARNINGS /Iinclude
SRCS   = src\main.c src\parser.c src\executer.c src\winshellError.c
HDRS   = include\constants.h include\executer.h include\parser.h include\winshellError.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	@if not exist "$(BINDIR)" mkdir "$(BINDIR)"
	@if not exist $(OBJDIR) mkdir $(OBJDIR)
	cl $(CFLAGS) /Fo$(OBJDIR)\ /Fe"$(TARGET)" $(SRCS)

clean:
	@if exist "$(TARGET)" del "$(TARGET)"
	@if exist $(OBJDIR) rmdir /s /q $(OBJDIR)

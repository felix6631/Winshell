## arch 빌드 규칙 (NMAKE / MSVC). 보통은 최상위 Makefile을 통해 호출됩니다.

NAME   = arch
BINDIR = ..\bin
OBJDIR = obj
TARGET = $(BINDIR)\$(NAME).exe
CFLAGS = /nologo /utf-8 /W3 /O2 /D_CRT_SECURE_NO_WARNINGS

all: $(TARGET)

$(TARGET): main.c
	@if not exist "$(BINDIR)" mkdir "$(BINDIR)"
	@if not exist $(OBJDIR) mkdir $(OBJDIR)
	cl $(CFLAGS) /Fo$(OBJDIR)\ /Fe"$(TARGET)" main.c

clean:
	@if exist "$(TARGET)" del "$(TARGET)"
	@if exist $(OBJDIR) rmdir /s /q $(OBJDIR)

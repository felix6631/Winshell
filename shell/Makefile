## shell 빌드 규칙 (NMAKE / MSVC). 보통은 최상위 Makefile을 통해 호출됩니다.

NAME     = shell
SRCS     = src\main.c src\parser.c src\list.c src\executer.c src\builtin.c src\glob.c src\variables.c ..\common\utf8.c ..\common\cmdline.c ..\common\program.c ..\common\vpath.c
HDRS     = ..\common\utf8.h ..\common\cmdline.h ..\common\program.h include\builtin.h include\executer.h include\parser.h include\variables.h ..\common\vpath.h
INCLUDES = /Iinclude

!INCLUDE ..\common.mk

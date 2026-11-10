## chown 빌드 규칙 (NMAKE / MSVC). 보통은 최상위 Makefile을 통해 호출됩니다.

NAME     = chown
SRCS     = main.c ..\common\ownership.c ..\common\args.c ..\common\vpath.c ..\common\fsutil.c ..\common\textutil.c ..\common\ident.c
HDRS     = ..\common\ownership.h ..\common\args.h ..\common\vpath.h ..\common\fsutil.h ..\common\textutil.h ..\common\ident.h
LIBS     = advapi32.lib netapi32.lib

!INCLUDE ..\common.mk

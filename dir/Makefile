## dir 빌드 규칙 (NMAKE / MSVC). 보통은 최상위 Makefile을 통해 호출됩니다.
## dir은 ls와 같은 소스(..\ls\main.c)를 다른 이름과 기본 옵션(-C -b)으로 빌드합니다.

NAME     = dir
SRCS     = main.c ..\common\args.c ..\common\vpath.c
HDRS     = ..\ls\main.c ..\common\args.h ..\common\vpath.h ..\common\vec.h
LIBS     = advapi32.lib

!INCLUDE ..\common.mk

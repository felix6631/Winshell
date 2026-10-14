## Winshell 최상위 Makefile (NMAKE / MSVC)
##
## 최상위 디렉터리 아래의 각 디렉터리는 하나의 서브 프로젝트(도구)이며, 각자 Makefile을 가집니다.
## 도구의 Makefile 은 NAME, SRCS, HDRS 만 정하고 common.mk 를 포함하면 됩니다.
##
## 사용법 (cl.exe와 nmake.exe가 PATH에 등록된 터미널에서 실행)
##   nmake          모든 서브 프로젝트 빌드 (결과물: bin\)
##   nmake test     빌드한 뒤 테스트(test\) 실행
##   nmake asan     AddressSanitizer 로 빌드해서 테스트 실행 (bin-asan\)
##   nmake gnu      GNU 도구와 출력 대조 (bash 와 GNU coreutils 필요)
##   nmake clean    모든 빌드 결과물 삭제
##
##   nmake new NAME=이름   새 명령의 뼈대(소스, Makefile, 테스트, GNU 대조, 레퍼런스 항목)를 만듭니다
##   nmake check-registry  모든 도구가 레퍼런스(docs\)와 테스트에 있는지 확인합니다
##
## 새 명령어는 디렉터리와 Makefile 만 있으면 빌드 목록에 들어갑니다(nmake new 가 만들어 줍니다).

BINDIR = $(MAKEDIR)\bin

# 도구 목록은 직접 적지 않습니다: Makefile 이 있는 디렉터리(test 제외)는 모두 도구이고,
# scripts\list-tools.bat 가 시작할 때 tools.inc 에 "BUILD = build-<이름> ..." 을 씁니다.
# build-NAME 과 clean-NAME 은 NAME 디렉터리에서 그 도구의 Makefile 을 실행합니다.
!IF [scripts\list-tools.bat > tools.inc] != 0
!ERROR scripts\list-tools.bat 를 실행할 수 없습니다
!ENDIF
!INCLUDE tools.inc
CLEAN = $(BUILD:build-=clean-)

all: bindir $(BUILD)

bindir:
	@if not exist "$(BINDIR)" mkdir "$(BINDIR)"

$(BUILD):
	@cd $(@:build-=) && $(MAKE) /nologo BINDIR="$(BINDIR)" all

## 테스트: 먼저 모든 프로그램을 빌드한 뒤 test\ 의 테스트를 빌드하고 실행합니다.
test: all
	@cd test && $(MAKE) /nologo check

## AddressSanitizer: 모든 프로그램을 bin-asan\ 에 /fsanitize=address 로 빌드하고, 같은 방식으로 빌드한
## 테스트로 전체를 실행합니다. 오류가 있으면 프로그램이 오류 위치와 함께 중단됩니다.
asan:
	@$(MAKE) /nologo BINDIR="$(MAKEDIR)\bin-asan" EXTRA_CFLAGS="/fsanitize=address /Zi" all
	@cd test && $(MAKE) /nologo check-asan

## GNU 도구(MSYS2 의 bash 가 있는 환경)와 출력을 대조합니다: bash test/gnu_compare.sh
gnu: all
	@cd test && $(MAKE) /nologo gnu
	@bash test/gnu_compare.sh

## 새 명령의 뼈대: nmake new NAME=이름 (소스, Makefile, 테스트, GNU 대조, 레퍼런스 항목)
new:
	@if "$(NAME)"=="" (echo usage: nmake new NAME=이름 & exit /b 1)
	@powershell -NoProfile -ExecutionPolicy Bypass -File scripts\new-command.ps1 $(NAME)

## 레퍼런스(docs\REFERENCE*.md)의 도구 목록(빌드 구조 문장, 디렉터리 구조)을 디렉터리에 맞춥니다.
sync-readme:
	@powershell -NoProfile -ExecutionPolicy Bypass -File scripts\sync-readme.ps1

## 모든 도구가 레퍼런스와 테스트에 있는지 확인합니다.
check-registry:
	@powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-registry.ps1

clean: $(CLEAN) clean-test

$(CLEAN):
	@cd $(@:clean-=) && $(MAKE) /nologo BINDIR="$(BINDIR)" clean

clean-test:
	@cd test && $(MAKE) /nologo clean

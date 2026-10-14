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
## 새 명령어를 추가하려면 디렉터리와 Makefile을 만든 뒤, 아래 BUILD 목록에 build-<이름> 을 추가하세요.

BINDIR = $(MAKEDIR)\bin

# build-NAME 과 clean-NAME 은 NAME 디렉터리에서 그 도구의 Makefile 을 실행합니다.
# 새 도구는 아래 BUILD 목록에 build-<이름> 만 추가하면 됩니다.
BUILD = build-shell build-arch build-basename build-cat build-comm build-cp build-cut build-date build-diff build-dirname build-echo build-env build-expr build-find build-grep build-head build-kill build-ls build-mkdir \
        build-mv build-nl build-paste build-printenv build-ps build-pwd build-realpath build-rm build-rmdir build-sed build-sleep build-sort build-tac \
        build-tail build-tee build-touch build-tr build-uniq build-wc build-which build-xargs
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

clean: $(CLEAN) clean-test

$(CLEAN):
	@cd $(@:clean-=) && $(MAKE) /nologo BINDIR="$(BINDIR)" clean

clean-test:
	@cd test && $(MAKE) /nologo clean

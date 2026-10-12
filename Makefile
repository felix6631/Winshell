## Winshell 최상위 Makefile (NMAKE / MSVC)
##
## 최상위 디렉터리 아래의 각 디렉터리는 하나의 서브 프로젝트이며, 각자 Makefile을 가집니다.
## 서브 프로젝트의 Makefile은 다음을 만족해야 합니다.
##   - all   : 실행 파일을 $(BINDIR)에 빌드합니다.
##   - clean : 빌드 결과물을 삭제합니다.
##   - BINDIR 매크로로 출력 위치를 받습니다. (기본값 ..\bin)
##
## 사용법 (cl.exe와 nmake.exe가 PATH에 등록된 터미널에서 실행)
##   nmake          모든 서브 프로젝트 빌드 (결과물: bin\)
##   nmake test     빌드한 뒤 테스트(test\) 실행
##   nmake asan     AddressSanitizer 로 빌드해서 테스트 실행 (bin-asan\)
##   nmake gnu      GNU 도구와 출력 대조 (bash 와 GNU coreutils 필요)
##   nmake clean    모든 빌드 결과물 삭제
##
## 새 명령어를 추가하려면 디렉터리와 Makefile을 만든 뒤, 아래 목록에 이름을 추가하세요.

BINDIR = $(MAKEDIR)\bin

all: bindir build-shell build-arch build-cat build-cp build-cut build-echo build-env build-head build-ls build-mkdir build-mv build-printenv build-pwd build-rm build-rmdir build-sort build-tail build-touch build-tr build-uniq build-wc

bindir:
	@if not exist "$(BINDIR)" mkdir "$(BINDIR)"

build-shell:
	@cd shell && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-arch:
	@cd arch && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-cat:
	@cd cat && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-cp:
	@cd cp && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-echo:
	@cd echo && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-env:
	@cd env && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-printenv:
	@cd printenv && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-head:
	@cd head && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-ls:
	@cd ls && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-mkdir:
	@cd mkdir && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-mv:
	@cd mv && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-pwd:
	@cd pwd && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-rm:
	@cd rm && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-rmdir:
	@cd rmdir && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-tail:
	@cd tail && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-cut:
	@cd cut && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-sort:
	@cd sort && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-tr:
	@cd tr && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-uniq:
	@cd uniq && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-touch:
	@cd touch && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-wc:
	@cd wc && $(MAKE) /nologo BINDIR="$(BINDIR)" all

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
	@bash test/gnu_compare.sh

clean: clean-shell clean-arch clean-cat clean-cp clean-echo clean-env clean-head clean-ls clean-mkdir clean-mv clean-printenv clean-pwd clean-rm clean-rmdir clean-tail clean-touch clean-wc clean-test clean-cut clean-sort clean-tr clean-uniq

clean-shell:
	@cd shell && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-arch:
	@cd arch && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-cat:
	@cd cat && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-cp:
	@cd cp && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-echo:
	@cd echo && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-env:
	@cd env && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-printenv:
	@cd printenv && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-head:
	@cd head && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-ls:
	@cd ls && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-mkdir:
	@cd mkdir && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-mv:
	@cd mv && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-pwd:
	@cd pwd && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-rm:
	@cd rm && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-rmdir:
	@cd rmdir && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-tail:
	@cd tail && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-cut:
	@cd cut && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-sort:
	@cd sort && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-tr:
	@cd tr && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-uniq:
	@cd uniq && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-touch:
	@cd touch && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-wc:
	@cd wc && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-test:
	@cd test && $(MAKE) /nologo clean

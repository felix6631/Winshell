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
##   nmake clean    모든 빌드 결과물 삭제
##
## 새 명령어를 추가하려면 디렉터리와 Makefile을 만든 뒤, 아래 목록에 이름을 추가하세요.

BINDIR = $(MAKEDIR)\bin

all: bindir build-shell build-arch build-echo build-pwd

bindir:
	@if not exist "$(BINDIR)" mkdir "$(BINDIR)"

build-shell:
	@cd shell && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-arch:
	@cd arch && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-echo:
	@cd echo && $(MAKE) /nologo BINDIR="$(BINDIR)" all
build-pwd:
	@cd pwd && $(MAKE) /nologo BINDIR="$(BINDIR)" all

clean: clean-shell clean-arch clean-echo clean-pwd

clean-shell:
	@cd shell && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-arch:
	@cd arch && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-echo:
	@cd echo && $(MAKE) /nologo BINDIR="$(BINDIR)" clean
clean-pwd:
	@cd pwd && $(MAKE) /nologo BINDIR="$(BINDIR)" clean

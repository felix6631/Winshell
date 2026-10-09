#include <stdio.h>
#include <windows.h>

/**
 * All of recognizable architecture are defined on "winnt.h", line 5660
 */
int main() {
    SYSTEM_INFO sysInfo;
    GetNativeSystemInfo(&sysInfo);

    // coreutils arch 명령어 표준 출력과 1:1 매핑
    switch (sysInfo.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_INTEL:
            printf("i686\n"); // Linux 표준 32비트 x86 표기
            break;
            
        case PROCESSOR_ARCHITECTURE_AMD64:
        case PROCESSOR_ARCHITECTURE_IA32_ON_WIN64: 
            // WoW64 환경이거나 실제 64비트 하드웨어인 경우 모두 x86_64로 출력
            printf("x86_64\n");
            break;
            
        case PROCESSOR_ARCHITECTURE_ARM64:
        case PROCESSOR_ARCHITECTURE_IA32_ON_ARM64:
            // ARM64 하드웨어이거나 에뮬레이션 상태인 경우 aarch64로 출력
            printf("aarch64\n");
            break;
            
        case PROCESSOR_ARCHITECTURE_ARM:
        case PROCESSOR_ARCHITECTURE_ARM32_ON_WIN64:
            printf("armv7l\n"); // Linux 32비트 ARM 표준 표기 (또는 그냥 arm)
            break;
            
        case PROCESSOR_ARCHITECTURE_IA64:
            printf("ia64\n"); // 인텔 아이태니엄 아키텍처
            break;
            
        case PROCESSOR_ARCHITECTURE_MIPS:
            printf("mips\n");
            break;
            
        case PROCESSOR_ARCHITECTURE_ALPHA:
        case PROCESSOR_ARCHITECTURE_ALPHA64:
            printf("alpha\n"); // DEC 알파
            break;
            
        case PROCESSOR_ARCHITECTURE_PPC:
            printf("powerpc\n"); // PowerPC
            break;
            
        case PROCESSOR_ARCHITECTURE_SHX:
            printf("sh4\n"); // SuperH 아키텍처
            break;
            
        case PROCESSOR_ARCHITECTURE_MSIL:
        case PROCESSOR_ARCHITECTURE_NEUTRAL:
            printf("anycpu\n"); // 관리형(NET) 독립형 코드 표기
            break;
            
        default:
            printf("unknown\n");
            break;
    }
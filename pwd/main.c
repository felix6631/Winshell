#include <stdio.h>
#include <windows.h>

int main() {
    char buffer[MAX_PATH];
    if (GetCurrentDirectoryA(MAX_PATH, buffer)) {
        printf("%s\n", buffer);
    } else {
        fprintf(stderr, "Error getting current directory\n");
    }
    return 0;
}

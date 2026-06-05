#pragma once

#include <Windows.h>
#include <stdio.h>
int ReadDirectory(const wchar_t* directory, WIN32_FIND_DATA* files, size_t* nof, size_t* mfl);

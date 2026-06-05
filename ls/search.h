#pragma once
#include <winbase.h>
#include <stdio.h>
#include "OptionBit.h"
int search(const wchar_t* directory, WIN32_FIND_DATA** files, size_t* nof, size_t* mfl, OptionFlags option);
int search_recursive(const wchar_t* directory, WIN32_FIND_DATA** files, size_t* nof, size_t* mfl, OptionFlags option);
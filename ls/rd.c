#include "rd.h"

int ReadDirectory(const wchar_t* directory, WIN32_FIND_DATA* files, size_t* nof, size_t* mfl) {
	WIN32_FIND_DATA findData;
	HANDLE hFind = FindFirstFileW(directory, &findData);
	size_t numofFiles = 0;
	size_t _space = 64;
	size_t maxFileLength = 0;
	WIN32_FIND_DATA* searchPath = (WIN32_FIND_DATA*)malloc(_space * sizeof(WIN32_FIND_DATA)); // Allocate memory for search path
	if (searchPath == NULL) {
		wprintf(L"Memory allocation failed\n");
		return 1;
	}
	while (hFind != INVALID_HANDLE_VALUE) {
		searchPath[numofFiles] = findData;

		if (maxFileLength < wcslen(findData.cFileName)) {
			maxFileLength = wcslen(findData.cFileName);
		}
		numofFiles++;
		if (numofFiles >= _space) {
			_space *= 2;
			WIN32_FIND_DATA* temp = (WIN32_FIND_DATA*)realloc(searchPath, _space * sizeof(WIN32_FIND_DATA));
			if (temp == NULL) {
				wprintf(L"Memory reallocation failed\n");
				free(searchPath);
				return 1;
			}
			searchPath = temp;
		}
		if (!FindNextFileW(hFind, &findData)) {
			break;
		}
	}

	files = searchPath;
	*mfl = maxFileLength;
	*nof = numofFiles;

	CloseHandle(hFind);
	// Return 0 to indicate success
	return 0;
}
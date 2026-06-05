#include "search.h"

/**
 * @param 
 * directory: The directory to search in.
 * files: An array to store the found files.
 * nof: The number of files found so far.
 * mfl: The maximum number of files to find.
 * option: The options for the search, represented as bit flags.
 * 
 * Check out https://learn.microsoft.com/ko-kr/windows/win32/fileio/listing-the-files-in-a-directory
 */
int search(const wchar_t* directory, WIN32_FIND_DATA** files, size_t* nof, size_t* mfl, OptionFlags option) {
	HANDLE hFind;
	wchar_t searchPath[MAX_PATH];
	hFind = FindFirstFile(directory, &searchPath);



	return 0; // Return 0 to indicate success
}
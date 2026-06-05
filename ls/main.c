#include <stdio.h>
#include "rd.h"
#include "OptionBit.h"
#include "search.h"

int main(int argc, wchar_t* argv[]) {
	if (argc < 2) {
		wprintf(L"Usage: %ls <directory>\n", argv[0]);
		return 1;
	}
	wchar_t* directory = argv[1];
	WIN32_FIND_DATA* files = NULL;
	size_t numofFiles = 0;
	size_t maxFileLength = 0;
	// Execute the command
	int result = ReadDirectory(directory,files,&numofFiles,&maxFileLength);
	if (result != 0) {
		wprintf(L"Failed to read directory: %ls\n", directory);
		return 1;
	}
	
	OptionFlags options = 0;
	if (wcschr(argv[2], L'd') != NULL) {
		OPTION_ON(options, OPTION_DETAILS);
	}
	if (wcschr(argv[2], L'r') != NULL) {
		OPTION_ON(options, OPTION_RECURSIVE);
	}
	if (wcschr(argv[2], L'h') != NULL) {
		OPTION_ON(options, OPTION_HIDDEN);
	}

	if (OPTION_IS_ON(options, OPTION_RECURSIVE)) {
		search_recursive(directory, &files, &numofFiles, &maxFileLength,options);
	}
	else {
		search(directory, &files, &numofFiles, &maxFileLength,options);
	}

	return 0;
}
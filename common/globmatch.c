#include "globmatch.h"

int glob_match(const char* pattern, const char* text) {
    for (; *pattern != '\0'; pattern++) {
        switch (*pattern) {
        case '?':
            if (*text == '\0')
                return 0;
            text++;
            break;
        case '*':
            while (pattern[1] == '*')
                pattern++;
            if (pattern[1] == '\0')
                return 1;
            for (; *text != '\0'; text++) {
                if (glob_match(pattern + 1, text))
                    return 1;
            }
            return glob_match(pattern + 1, text);
        case '[': {
            const char* p = pattern + 1;
            int negate = 0, matched = 0, first = 1;

            if (*text == '\0')
                return 0;
            if (*p == '!' || *p == '^') {
                negate = 1;
                p++;
            }
            for (; *p != '\0' && (*p != ']' || first); p++, first = 0) {
                unsigned char low = (unsigned char)*p, high = low;

                if (*p == '\\' && p[1] != '\0') {
                    p++;
                    low = high = (unsigned char)*p;
                }
                if (p[1] == '-' && p[2] != '\0' && p[2] != ']') {
                    high = (unsigned char)p[2];
                    p += 2;
                }
                if ((unsigned char)*text >= low && (unsigned char)*text <= high)
                    matched = 1;
            }
            if (*p == '\0') {
                // no closing bracket: the '[' stands for itself
                if (*text != '[')
                    return 0;
                text++;
                break;
            }
            if (matched == negate)
                return 0;
            text++;
            pattern = p;
            break;
        }
        case '\\':
            if (pattern[1] != '\0')
                pattern++;
            // fall through
        default:
            if (*text != *pattern)
                return 0;
            text++;
        }
    }
    return *text == '\0';
}

#define UNICODE
#include <windows.h>
#include <stdio.h>

BOOL chomp(BYTE bite, LPCBYTE *json, LPCBYTE stop) {
    if (*json < stop && **json == bite) { *json += 1; return TRUE; }
    return FALSE;
}

BOOL chompRange(BYTE start, BYTE end, LPCBYTE *json, LPCBYTE stop) {
    if (*json < stop && **json >= start && **json <= end) {
        *json += 1;
        return TRUE;
    }

    return FALSE;
}

BOOL chompAny(LPCBYTE bite, LPCBYTE *json, LPCBYTE stop) {
    if (*json >= stop) { return FALSE; }
    for (; *bite != '\0'; bite++) {
        if (**json == *bite) { *json += 1; return TRUE; }
    }

    return FALSE;
}

BOOL chompToken(LPCBYTE bite, LPCBYTE *json, LPCBYTE stop) {
    int length = strlen(bite);
    if (*json + length <= stop && !strncmp(*json, bite, length)) {
        *json += length;
        return TRUE;
    }

    return FALSE;
}

BOOL parseNumber(LPCBYTE *json, LPCBYTE stop) {
    chomp('-', json, stop);
    if (!chomp('0', json, stop)) {
        if (!chompRange('1', '9', json, stop)) { return FALSE; }
        while (chompRange('0', '9', json, stop));
    }

    if (chomp('.', json, stop)) {
        if (!chompRange('0', '9', json, stop)) { return FALSE; }
        while (chompRange('0', '9', json, stop));
    }

    if (chompAny("eE", json, stop)) {
        chompAny("+-", json, stop);
        if (!chompRange('0', '9', json, stop)) { return FALSE; }
        while (chompRange('0', '9', json, stop));
    }

    return TRUE;
}

typedef enum {
    VALUE_CONTEXT = 0, FIRST_KEY_CONTEXT, KEY_CONTEXT, CONTEXT_COUNT,
} StringContext;
int getCharSize(LPCBYTE json, LPCBYTE stop, LPCWSTR **errors) {
    if (json >= stop) {
        LPCWSTR eofErrors[CONTEXT_COUNT] = {
            L"Unexpected %2$s in %1$s",
            L"Unexpected %2$s in first key of %1$s object",
            L"Unexpected %2$s in key after %1$s",
        };
        *errors = eofErrors; // Unexpected end of file
    } else if (*json < 0x20) {
        LPCWSTR unescapedErrors[CONTEXT_COUNT] = {
            L"Unescaped %2$s in %1$s",
            L"Unescaped %2$s in first key of %1$s object",
            L"Unescaped %2$s in key after %1$s",
        };
        *errors = unescapedErrors; // Unescaped U+1F
    } else if (*json < 0x80) {
        return 1;
    } else {
        // https://en.wikipedia.org/wiki/UTF-8#Encoding
        LPWSTR invalidErrors[CONTEXT_COUNT] = {
            L"%2$s in %1$s",
            L"%2$s in first key of %1$s object",
            L"%2$s in key after %1$s",
        };
        if (*json < 0xc0) {
            // Invalid UTF-8 byte 0x95
        } else if (json + 1 >= stop || (json[1] & 0xc0) != 0x80) {
            // Incomplete UTF-8 character 0xd3
        } else if (*json < 0xe0) {
            return 2;
        } else if (json + 2 >= stop || (json[2] & 0xc0) != 0x80) {
            // Incomplete UTF-8 character 0xe481
        } else if (*json < 0xf0) {
            return 3;
        } else if (json + 3 >= stop || (json[3] & 0xc0) != 0x80) {
            // Incomplete UTF-8 character 0xf48182
        } else if (*json < 0xf8) {
            return 4;
        } else {
            // Invalid UTF-8 byte 0xfb
        }

        *errors = invalidErrors;
    }

    return 0;
}

/*
Invalid escape sequence \u%%%%%%%% in first key of %1$s object0
*/
#define ESCAPE_ERROR_LENGTH 200
WCHAR parseStringOut[ESCAPE_ERROR_LENGTH];
LPCWSTR parseString(LPCBYTE *json, LPCBYTE stop, StringContext context) {
    if (!chomp('"', json, stop)) {
        if (*json >= stop) {
            LPCWSTR errors[CONTEXT_COUNT] = {
                L"Missing %1$s string before %2$s",
                L"Unexpected %2$s at start of %1$s object",
                L"Unexpected %2$s after %1$s",
            };
            return errors[context];
        } else {
            LPCWSTR errors[CONTEXT_COUNT] = {
                L"%1$s string must start with \", not %2$s",
                L"%1$s object missing \" before first key %2$s",
                L"After %1$s, missing \" before next key %2$s",
            };
            return errors[context];
        }
    }

    while (TRUE) {
        if (chomp('"', json, stop)) { return NULL; }
        if (chomp('\\', json, stop)) {
            if (chomp('u', json, stop)) {
                LPCBYTE temp = *json;
                for (int i = 0; i < 4; i++) {
                    if (
                        !chompRange('0', '9', &temp, stop)
                            && !chompRange('a', 'f', &temp, stop)
                            && !chompRange('A', 'F', &temp, stop)
                    ) {
                        LPCWSTR escapeErrors[CONTEXT_COUNT] = {
                            L"Invalid escape sequence \\u%2$.4s in %1$s",
                            L"Invalid escape sequence \\u%2$.4s in first key"
                                L" of %1$s object",
                            L"Invalid escape sequence \\u%2$.4s in key after"
                                L" %1$s",
                        };
                        LPCWSTR *errors = escapeErrors;
                        if (getCharSize(temp, stop, &errors) <= 0) {
                            *json = temp;
                        }

                        return errors[context];
                    }
                }

                *json = temp;
            } else if (!chompAny("\"\\/bfnrt", json, stop)) {
                LPCWSTR escapeErrors[CONTEXT_COUNT] = {
                    L"Invalid escape sequence \\%2$.1s in %1$s",
                    L"Invalid escape sequence \\%2$.1s in first key of %1$s"
                        L" object",
                    L"Invalid escape sequence \\%2$.1s in key after %1$s",
                };
                LPCWSTR *errors = escapeErrors;
                getCharSize(*json, stop, &errors);
                return errors[context];
            }
        } else {
            LPCWSTR *errors = NULL;
            *json += getCharSize(*json, stop, &errors);
            if (errors != NULL) { return errors[context]; }
        }
    }
}

#define MAX_FRAME_COUNT 250
// https://stackoverflow.com/a/798311
#define STRINGIFY_HELP(x) L#x
#define STRINGIFY(x) STRINGIFY_HELP(x)
typedef struct { int index; LPCBYTE key; } Frame;
typedef struct { int count; Frame frames[MAX_FRAME_COUNT]; } Stack;
LPCWSTR pushElement(Stack *stack, int index) {
    if (stack->count >= MAX_FRAME_COUNT) {
        return L"JSON structure is more than " STRINGIFY(MAX_FRAME_COUNT)
            L" levels deep";
    }

    stack->frames[stack->count].index = index;
    stack->frames[stack->count].key = NULL;
    stack->count++;
    return NULL;
}

LPCWSTR pushMember(Stack *stack, LPCBYTE key) {
    if (stack->count >= MAX_FRAME_COUNT) {
        return L"JSON structure is more than " STRINGIFY(MAX_FRAME_COUNT)
            L" levels deep";
    }

    stack->frames[stack->count].index = -1;
    stack->frames[stack->count].key = key;
    stack->count++;
    return NULL;
}

LPCBYTE WHITESPACE = " \n\r\t";
LPCWSTR parseValue(LPCBYTE *json, LPCBYTE stop, Stack *stack);

LPCWSTR parseArray(LPCBYTE *json, LPCBYTE stop, Stack *stack) {
    if (!chomp('[', json, stop)) {
        if (*json >= stop) { return L"Missing %1$s array before %2$s"; }
        return L"%1$s array must start with [, not %2$s";
    }

    while (chompAny(WHITESPACE, json, stop));
    if (chomp(']', json, stop)) { return NULL; }
    for (int i = 0; TRUE; i++) {
        LPCWSTR error = pushElement(stack, i);
        if (error) { return error; }
        error = parseValue(json, stop, stack);
        if (error) { return error; }
        while (chompAny(WHITESPACE, json, stop));
        if (chomp(']', json, stop)) { stack->count--; return NULL; }
        LPCBYTE beforeComma = *json;
        if (*json >= stop) {
            return L"Unexpected %2$s after %1$s";
        } else if (!chomp(',', json, stop)) {
            return L"Missing comma or ] between %1$s and %2$s";
        }

        while (chompAny(WHITESPACE, json, stop));
        if (*json < stop && **json == ']' || **json == ',') {
            if (**json == ']') { *json = beforeComma; }
            return L"Extra comma after %1$s";
        }

        stack->count--;
    }
}

LPCWSTR parseObject(LPCBYTE *json, LPCBYTE stop, Stack *stack) {
    if (!chomp('{', json, stop)) {
        if (*json >= stop) { return L"Missing %1$s object before %2$s"; }
        return L"%1$s object must start with {, not %2$s";
    }

    while (chompAny(WHITESPACE, json, stop));
    if (chomp('}', json, stop)) { return NULL; }
    StringContext context = FIRST_KEY_CONTEXT;
    while (TRUE) {
        LPCBYTE key = *json;
        LPCWSTR error = parseString(json, stop, context);
        if (error) { return error; }
        if (context == KEY_CONTEXT) { stack->count--; }
        error = pushMember(stack, key);
        if (error) { return error; }
        context = KEY_CONTEXT;
        while (chompAny(WHITESPACE, json, stop));
        if (!chomp(':', json, stop)) {
            return L"Missing colon between %1$s key and %2$s";
        }

        error = parseValue(json, stop, stack);
        if (error) { return error; }
        while (chompAny(WHITESPACE, json, stop));
        if (chomp('}', json, stop)) { stack->count--; return NULL; }
        LPCBYTE beforeComma = *json;
        if (*json >= stop) {
            return L"Unexpected %2$s after %1$s";
        } else if (!chomp(',', json, stop)) {
            return L"Missing comma or } between %1$s value and %2$s";
        }

        while (chompAny(WHITESPACE, json, stop));
        if (*json < stop && **json == '}' || **json == ',') {
            if (**json == '}') { *json = beforeComma; }
            return L"Extra comma after %1$s value";
        }
    }
}

LPCWSTR parseValue(LPCBYTE *json, LPCBYTE stop, Stack *stack) {
    while (chompAny(WHITESPACE, json, stop));
    if (*json >= stop) { return L"Missing %1$s value before %2$s"; }
    if (**json == '{') { return parseObject(json, stop, stack); }
    if (**json == '[') { return parseArray(json, stop, stack); }
    if (**json == '"') { return parseString(json, stop, VALUE_CONTEXT); }
    LPCBYTE temp = *json;
    BOOL valid;
    if (*temp == '-' || (*temp >= '0' && *temp <= '9')) {
        valid = parseNumber(&temp, stop);
    } else {
        valid = chompToken("true", &temp, stop)
            || chompToken("false", &temp, stop)
            || chompToken("null", &temp, stop);
    }

    if (
        !valid || chompRange('0', '9', &temp, stop)
            || chompRange('a', 'z', &temp, stop)
            || chompRange('A', 'Z', &temp, stop)
            || chompAny("_.+-", &temp, stop)
    ) { return L"Could not parse %1$s value %2$s"; }
    *json = temp;
    return NULL;
}

LPCWSTR parseJSON(LPCBYTE *json, LPCBYTE stop, Stack *stack) {
    // https://en.wikipedia.org/wiki/Byte_order_mark#Usage
    if (*json + 2 < stop) {
        if (**json == 0xfe && json[0][1] == 0xff) {
            return L"Found byte order mark indicating UTF-16BE encoding. "
                L"Please save the file with UTF-8 encoding.";
        } else if (**json == 0xff && json[0][1] == 0xfe) {
            return L"Found byte order mark indicating UTF-16LE encoding. "
                L"Please save the file with UTF-8 encoding.";
        } else if (**json == 0 && json[0][1] != 0) {
            return L"Found null byte suggesting UTF-16BE encoding. "
                L"Please save the file with UTF-8 encoding.";
        } else if (**json != 0 && json[0][1] == 0) {
            return L"Found null byte suggesting UTF-16LE encoding. "
                L"Please save the file with UTF-8 encoding.";
        }
    }

    chompToken("\xEF\xBB\xBF", json, stop);
    // TODO: replace with parseObject
    LPCWSTR error = parseValue(json, stop, stack);
    if (error) { return error; }
    while (chompAny(WHITESPACE, json, stop));
    if (*json < stop) {
        return L"Expected end of file after %1$s object, not %2$s";
    }

    return NULL;
}

#define MAX_TOKEN_LENGTH 80
WCHAR tokenOut[MAX_TOKEN_LENGTH];
LPCWSTR getToken(LPCBYTE json, LPCBYTE stop, BOOL uppercase) {
    if (json >= stop) {
        wcsncpy_s(tokenOut, MAX_TOKEN_LENGTH, L"end of file", _TRUNCATE);
        if (uppercase) { *tokenOut = L'E'; }
        return tokenOut;
    } else if (*json < 0x20) {
        LPCWSTR names[0x20] = {
            L"null character", L"U+0001", L"U+0002", L"U+0003", L"U+0004",
            L"U+0005", L"U+0006", L"U+0007", L"U+0008", L"tab character",
            L"newline", L"U+000B", L"U+000C", L"carriage return", L"U+000E",
            L"U+000F", L"U+0010", L"U+0011", L"U+0012", L"U+0013", L"U+0014",
            L"U+0015", L"U+0016", L"U+0017", L"U+0018", L"U+0019", L"U+001A",
            L"U+001B", L"U+001C", L"U+001D", L"U+001E", L"U+001F",
        };
        wcsncpy_s(tokenOut, MAX_TOKEN_LENGTH, names[*json], _TRUNCATE);
        if (uppercase) {
            *tokenOut = L"NUUUUUUUUTNUUCUUUUUUUUUUUUUUUUUU"[*json];
        }

        return tokenOut;
    } else if (*json >= 0x80) {
        // https://en.wikipedia.org/wiki/UTF-8#Encoding
        int byteCount = 0;
        if (*json < 0xc0) {
            _snwprintf_s(
                tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
                L"invalid UTF-8 byte 0x%02x", *json, json[1]
            );
        } else if (json + 1 >= stop || (json[1] & 0xc0) != 0x80) {
            _snwprintf_s(
                tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
                L"incomplete UTF-8 character 0x%02x", *json
            );
        } else if (*json < 0xe0) {
            byteCount = 2;
        } else if (json + 2 >= stop || (json[2] & 0xc0) != 0x80) {
            _snwprintf_s(
                tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
                L"incomplete UTF-8 character 0x%02x%02x", *json, json[1]
            );
        } else if (*json < 0xf0) {
            byteCount = 3;
        } else if (json + 3 >= stop || (json[3] & 0xc0) != 0x80) {
            _snwprintf_s(
                tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
                L"incomplete UTF-8 character 0x%02x%02x%02x", *json, json[1],
                json[2]
            );
        } else if (*json < 0xf8) {
            byteCount = 4;
        } else {
            _snwprintf_s(
                tokenOut, MAX_TOKEN_LENGTH, _TRUNCATE,
                L"invalid UTF-8 byte 0x%02x", *json, json[1]
            );
        }

        if (byteCount <= 0) {
            if (uppercase) { *tokenOut = L'I'; }
            return tokenOut;
        }
    }

    LPCBYTE temp = json;
    while (
        chompRange('0', '9', &temp, stop)
            || chompRange('a', 'z', &temp, stop)
            || chompRange('A', 'Z', &temp, stop)
            || chompAny("_.+-", &temp, stop)
    );
    int tokenLength = min(max(temp - json, 1), MAX_TOKEN_LENGTH);
    MultiByteToWideChar(
        CP_UTF8, MB_PRECOMPOSED, json, stop - json, tokenOut, tokenLength
    );
    if (tokenLength >= MAX_TOKEN_LENGTH) {
        tokenLength--;
        tokenOut[tokenLength - 1] = L'\u2026'; // horizontal ellipsis
    }

    tokenOut[tokenLength] = L'\0';
    return tokenOut;
}

#define MAX_STACK_LENGTH 150
WCHAR stackOut[MAX_STACK_LENGTH];
LPCWSTR getStack(const Stack *stack, LPCBYTE stop) {
    if (stack->count <= 0) { return L"top-level"; }
    LPWSTR stackStop = stackOut + MAX_STACK_LENGTH - 1;
    *stackStop = L'\0';
    for (int i = stack->count - 1; i >= 0; i--) {
        LPCBYTE key = stack->frames[i].key;
        int index = stack->frames[i].index;
        if (key) {
            LPCBYTE keyStop = key + 1;
            for (; keyStop < stop; keyStop++) {
                if (*keyStop == '"' && keyStop[-1] != '\\') {
                    keyStop++; break;
                }
            }
            LPCBYTE temp = key + 1;
            if (!chompRange('0', '9', &temp, keyStop - 1)) {
                while (
                    chompRange('0', '9', &temp, keyStop - 1)
                        || chompRange('a', 'z', &temp, keyStop - 1)
                        || chompRange('A', 'Z', &temp, keyStop - 1)
                        || chomp('_', &temp, keyStop - 1)
                );
                if (temp > key + 1 && temp >= keyStop - 1) {
                    key++; keyStop--;
                }
            }

            int keyLength = MultiByteToWideChar(
                CP_UTF8, MB_PRECOMPOSED, key, keyStop - key, NULL, 0
            );
            LPWSTR keyStart = stackStop - keyLength;
            LPWSTR stackStart = keyStart - 2 * (i > 0);
            if (stackStart < stackOut) {
                stackStop--;
                *stackStop = L'\u2026'; // horizontal ellipsis
                if (i < stack->count - 1) { return stackStop; }
                keyStart += stackOut - stackStart;
                stackStart = stackOut;
            }

            if (i > 0) { stackStart[1] = L'.'; }
            MultiByteToWideChar(
                CP_UTF8, MB_PRECOMPOSED, key, keyStop - key,
                keyStart, stackStop - keyStart
            );
            stackStop = stackStart + (i > 0);
        } else {
            int digitCount = index <= 0;
            for (int test = abs(index); test > 0; test /= 10) {
                digitCount++;
            }
            if ((digitCount) > 1) {
                digitCount = digitCount;
            }

            LPWSTR digitStart = stackStop - 1 - digitCount;
            LPWSTR stackStart = digitStart - 1 - (i > 0);
            if (stackStart < stackOut) {
                stackStop[-1] = L'\u2026'; // horizontal ellipsis
                return stackStop - 1;
            }

            digitStart[-1] = L'[';
            _itow_s(index, digitStart, digitCount + 1, 10);
            stackStop[-1] = L']';
            stackStop = stackStart + (i > 0);
        }
    }

    return stackStop + (*stackStop == L'.');
}

OVERLAPPED fileReadIn;
struct { DWORD errorCode; DWORD byteCount; } fileReadOut;
void CALLBACK fileReadComplete(
    DWORD errorCode, DWORD byteCount, LPOVERLAPPED overlapped
) {
    fileReadOut.errorCode = errorCode;
    fileReadOut.byteCount = byteCount;
}

LPBYTE readFile(LPCWSTR path, LPBYTE *stop) {
    // https://docs.microsoft.com/en-us/windows/win32/fileio/opening-a-file-for-reading-or-writing
    HANDLE file = CreateFile(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL, // default security settings
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL // no attribute template
    );
    if (file == INVALID_HANDLE_VALUE) { return NULL; }
    while (TRUE) { // fake loop as a substitute for context manager
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(file, &fileSize)) { break; }
        if (fileSize.HighPart) { break; }
        LPBYTE buffer = malloc(fileSize.LowPart);
        if (fileSize.LowPart > 0 && !buffer) { break; }
        while (TRUE) {
            fileReadIn.Offset = fileReadIn.OffsetHigh = 0;
            fileReadOut.errorCode = 0;
            fileReadOut.byteCount = 0;
            if (
                !ReadFileEx(
                    file,
                    buffer,
                    fileSize.LowPart,
                    &fileReadIn,
                    fileReadComplete
                )
            ) { break; }
            SleepEx(1000, TRUE);
            if (fileReadOut.errorCode) { break; }
            *stop = buffer + fileReadOut.byteCount;
            CloseHandle(file);
            return buffer;
        }

        free(buffer);
        break;
    }

    CloseHandle(file);
    *stop = NULL;
    return NULL;
}

#define MAX_FORMAT_LENGTH 200
#define MAX_STACK_LENGTH 150
#define FORMATTED_ERROR_LENGTH MAX_FORMAT_LENGTH + MAX_STACK_LENGTH \
    + MAX_TOKEN_LENGTH - 2
int main() {
    WCHAR path[MAX_PATH] = L"../JSONTestSuite/test_parsing/";
    LPWSTR name = path + wcsnlen(path, MAX_PATH);
    // https://docs.microsoft.com/en-us/windows/win32/fileio/listing-the-files-in-a-directory
    WIN32_FIND_DATA info;
    wcsncpy(name, L"*", path + MAX_PATH - name);
    HANDLE find = FindFirstFile(path, &info);
    if (find == INVALID_HANDLE_VALUE) { return 1; }
    do {
        if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { continue; }
        wcsncpy(name, info.cFileName, path + MAX_PATH - name);
        LPBYTE stop;
        LPBYTE buffer = readFile(path, &stop);
        LPBYTE json = buffer;
        Stack stack = { .count = 0 };
        LPCWSTR errorFormat = parseJSON(&json, stop, &stack);
        WCHAR error[FORMATTED_ERROR_LENGTH];
        if (errorFormat) {
            LPCWSTR stackString = getStack(&stack, stop);
            LPCWSTR token = getToken(
                json, stop, !wcsncmp(errorFormat, L"%2$", 3)
            );
            int order[2];
            _swprintf_p(
                error, FORMATTED_ERROR_LENGTH, errorFormat, stackString, token
            );
        } else {
            wcsncpy_s(error, FORMATTED_ERROR_LENGTH, L"No error!", _TRUNCATE);
        }

        if (errorFormat || info.cFileName[0] == L'n') {
            wprintf(L"%-50s %s\n", info.cFileName, error);
        }

        free(buffer);
    } while (FindNextFile(find, &info));

    FindClose(find);
    wprintf(L"tests complete\n");
    return 0;
}

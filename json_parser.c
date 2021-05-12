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

void bytesToHex(LPWSTR dest, size_t destSize, LPCBYTE source, size_t count) {
    int i = 0;
    for (; i < min(destSize - 1, 2 * count); i++) {
        BYTE b = source[i / 2];
        if (i % 2) { b &= 0xf; } else { b >>= 4; }
        dest[i] = b < 0xa ? L'0' + b : L'a' + b - 0xa;
    }

    dest[i] = L'\0';
}

int percentEscape(LPWSTR string, int oldLength) {
    int newLength = oldLength;
    for (int i = 0; i < oldLength; i++) { newLength += string[i] == L'%'; }
    int newI = newLength - 1;
    for (int i = oldLength - 1; i >= 0; i--) {
        string[newI] = string[i];
        if (string[newI] == L'%') { newI--; string[newI] = L'%'; }
    }

    return newLength;
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
                        if (getCharSize(temp, stop, &errors) > 0) {
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

LPCBYTE WHITESPACE = " \n\r\t";
LPCWSTR parseValue(LPCBYTE *json, LPCBYTE stop, int depth);

LPCWSTR parseArray(LPCBYTE *json, LPCBYTE stop, int depth) {
    if (!chomp('[', json, stop)) {
        if (*json >= stop) { return L"Missing %1$s array before %2$s"; }
        return L"%1$s array must start with [, not %2$s";
    }

    while (chompAny(WHITESPACE, json, stop));
    if (chomp(']', json, stop)) { return NULL; }
    while (TRUE) {
        LPCWSTR error = parseValue(json, stop, depth + 1);
        if (error) { return error; }
        while (chompAny(WHITESPACE, json, stop));
        if (chomp(']', json, stop)) { return NULL; }
        if (*json >= stop) {
            return L"Unexpected %2$s after %1$s";
        } else if (!chomp(',', json, stop)) {
            return L"Missing comma or ] between %1$s and %2$s";
        }

        while (chompAny(WHITESPACE, json, stop));
    }
}

LPCWSTR parseObject(LPCBYTE *json, LPCBYTE stop, int depth) {
    if (!chomp('{', json, stop)) {
        if (*json >= stop) { return L"Missing %1$s object before %2$s"; }
        return L"%1$s object must start with {, not %2$s";
    }

    while (chompAny(WHITESPACE, json, stop));
    if (chomp('}', json, stop)) { return NULL; }
    StringContext context = FIRST_KEY_CONTEXT;
    while (TRUE) {
        LPCWSTR error = parseString(json, stop, context);
        if (error) { return error; }
        context = KEY_CONTEXT;
        while (chompAny(WHITESPACE, json, stop));
        if (!chomp(':', json, stop)) {
            return L"Missing colon between %1$s key and %2$s";
        }
        error = parseValue(json, stop, depth + 1);
        if (error) { return error; }
        while (chompAny(WHITESPACE, json, stop));
        if (chomp('}', json, stop)) { return NULL; }
        if (*json >= stop) {
            return L"Unexpected %2$s after %1$s";
        } else if (!chomp(',', json, stop)) {
            return L"Missing comma or } between %1$s value and %2$s";
        }

        while (chompAny(WHITESPACE, json, stop));
    }
}

LPCWSTR parseValue(LPCBYTE *json, LPCBYTE stop, int depth) {
    if (depth > 250) {
        return L"JSON structure is more than 250 levels deep";
    }

    while (chompAny(WHITESPACE, json, stop));
    if (*json >= stop) { return L"Missing %1$s value before %2$s"; }
    if (**json == '{') { return parseObject(json, stop, depth); }
    if (**json == '[') { return parseArray(json, stop, depth); }
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

LPCWSTR parseJSON(LPCBYTE *json, LPCBYTE stop) {
    // TODO: replace with parseObject
    LPCWSTR error = parseValue(json, stop, 0);
    if (error) { return error; }
    while (chompAny(WHITESPACE, json, stop));
    if (*json < stop) {
        return L"Expected end of file after %1$s object, not %2$s";
    }

    return NULL;
}

#define MAX_TOKEN_LENGTH 80
WCHAR tokenOut[MAX_TOKEN_LENGTH];
LPCWSTR getToken(LPCBYTE json, LPCBYTE stop) {
    int tokenLength = MultiByteToWideChar(
        CP_UTF8, MB_PRECOMPOSED,
        // give this function extra space because it sometimes complains about
        // ERROR_INSUFFICIENT_BUFFER when given invalid characters
        json, stop - json, tokenOut, MAX_TOKEN_LENGTH
    );
    if (tokenLength >= MAX_TOKEN_LENGTH) {
        tokenLength--;
        tokenOut[tokenLength - 1] = L'\u2026'; // horizontal ellipsis
    }

    tokenOut[tokenLength] = L'\0';
    LPWSTR tokenStop = tokenOut;
    while (
        (*tokenStop >= L'0' && *tokenStop <= L'9')
            || (*tokenStop >= L'a' && *tokenStop <= L'z')
            || (*tokenStop >= L'A' && *tokenStop <= L'Z')
            || *tokenStop == L'_' || *tokenStop == L'.' || *tokenStop == L'+'
            || *tokenStop == L'-'
    ) { tokenStop++; }
    if (tokenStop == tokenOut) { tokenStop++; }
    *tokenStop = L'\0';
    return tokenOut;
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
        LPCWSTR errorFormat = parseJSON(&json, stop);
        WCHAR error[FORMATTED_ERROR_LENGTH];
        if (errorFormat) {
            WCHAR stack[MAX_STACK_LENGTH] = L"root";
            LPCWSTR token = getToken(json, stop);
            int order[2];
            _swprintf_p(
                error, FORMATTED_ERROR_LENGTH, errorFormat, stack, token
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

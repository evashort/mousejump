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

typedef enum {
    VALUE_CONTEXT = 0, FIRST_KEY_CONTEXT, KEY_CONTEXT, CONTEXT_COUNT,
} StringContext;
#define UNICODE_ESCAPE_ERROR_LENGTH 200
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
        if (*json + 1 < stop && chomp('\\', json, stop)) {
            LPWSTR errors[CONTEXT_COUNT] = {
                L"%1$s string has unknown escape sequence \\12345",
                L"First key in %1$s object has unknown escape sequence "
                    L"\\12345",
                L"After %1$s, next key has unknown escape sequence \\12345",
            };
            LPCBYTE escapeStart = *json;
            if (chomp('u', json, stop)) {
                for (int i = 0; i < 4; i++) {
                    if (
                        !chompRange('0', '9', json, stop)
                            && !chompRange('a', 'f', json, stop)
                            && !chompRange('A', 'F', json, stop)
                    ) {
                        LPWSTR escape = errors[context];
                        while (*escape != L'\\') { escape++; } escape++;
                        int escapeLength = MultiByteToWideChar(
                            CP_UTF8, MB_PRECOMPOSED,
                            escapeStart, stop - escapeStart, escape, 5
                        );
                        escape[escapeLength] = L'\0';
                        return errors[context];
                    }
                }
            } else if (!chompAny("\"\\/bfnrt", json, stop)) {
                LPWSTR escape = errors[context];
                while (*escape != L'\\') { escape++; } escape++;
                int escapeLength = MultiByteToWideChar(
                    CP_UTF8, MB_PRECOMPOSED,
                    escapeStart, stop - escapeStart, escape, 1
                );
                escape[escapeLength] = L'\0';
                return errors[context];
            }
        } else if (*json >= stop) {
            LPCWSTR errors[CONTEXT_COUNT] = {
                L"Unexpected %2$s in %1$s string",
                L"Unexpected %2$s while parsing first key in %1$s object",
                L"Unexpected %2$s while parsing key after %1$s",
            };
            return errors[context];
        } else if (**json < 0x20) {
            LPCWSTR errors[CONTEXT_COUNT] = {
                L"%1$s string has unescaped %2$s",
                L"First key in %1$s object has unescaped %2$s",
                L"Key after %1$s has unescaped %2$s",
            };
            return errors[context]; // has unescaped U+1F
        } else if (**json < 0x80) {
            *json += 1;
        } else {
            // https://en.wikipedia.org/wiki/UTF-8#Encoding
            LPWSTR errors[CONTEXT_COUNT] = {
                L"%1$s string has %2$s",
                L"First key in %1$s object has %2$s",
                L"Key after %1$s has %2$s",
            };
            if (
                **json < 0xc0
                    || *json + 1 >= stop || (json[0][1] & 0xc0) != 0x80
            ) {
                // has invalid UTF-8 byte 0x95
                // has incomplete UTF-8 character 0xd3
                return errors[context];
            } else if (**json < 0xe0) {
                *json += 2;
            } else if (*json + 2 >= stop || (json[0][2] & 0xc0) != 0x80) {
                // has incomplete UTF-8 character 0xe481
                return errors[context];
            } else if (**json < 0xf0) {
                *json += 3;
            } else if (*json + 3 >= stop || (json[0][3] & 0xc0) != 0x80) {
                // has incomplete UTF-8 character 0xf48182
                return errors[context];
            } else if (**json < 0xf8) {
                *json += 4;
            } else {
                return errors[context]; // has invalid UTF-8 byte 0xfb
            }
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
        LPCWSTR error = parseJSON(&json, stop);
        BOOL valid = !error;
        if (info.cFileName[0] != L'i') {
            BOOL expected = info.cFileName[0] == L'y';
            if (valid != expected) {
                wprintf(L"%s\n", info.cFileName);
            }
        }

        free(buffer);
    } while (FindNextFile(find, &info));

    FindClose(find);
    wprintf(L"tests complete\n");
    return 0;
}

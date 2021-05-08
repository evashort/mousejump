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

BOOL parseString(LPCBYTE *json, LPCBYTE stop) {
    if (!chomp('"', json, stop)) { return FALSE; }
    while (TRUE) {
        if (chomp('"', json, stop)) { return TRUE; }
        if (chomp('\\', json, stop)) {
            if (chomp('u', json, stop)) {
                for (int i = 0; i < 4; i++) {
                    if (
                        !chompRange('0', '9', json, stop)
                            && !chompRange('a', 'f', json, stop)
                            && !chompRange('A', 'F', json, stop)
                    ) { return FALSE; }
                }
            } else if (!chompAny("\"\\/bfnrt", json, stop)) { return FALSE; }
        } else if (*json >= stop || **json < 0x20) {
            return FALSE;
        } else if (**json < 0x80) {
            *json += 1;
        } else if (
            // https://en.wikipedia.org/wiki/UTF-8#Encoding
            **json < 0xc0 || *json + 1 >= stop || (json[0][1] & 0xc0) != 0x80
        ) {
            return FALSE;
        } else if (**json < 0xe0) {
            *json += 2;
        } else if (*json + 2 >= stop || (json[0][2] & 0xc0) != 0x80) {
            return FALSE;
        } else if (**json < 0xf0) {
            *json += 3;
        } else if (*json + 3 >= stop || (json[0][3] & 0xc0) != 0x80) {
            return FALSE;
        } else if (**json < 0xf8) {
            *json += 4;
        } else {
            return FALSE;
        }
    }
}

LPCBYTE WHITESPACE = " \n\r\t";
BOOL parseValue(LPCBYTE *json, LPCBYTE stop, int depth);

BOOL parseArray(LPCBYTE *json, LPCBYTE stop, int depth) {
    if (!chomp('[', json, stop)) { return FALSE; }
    while (chompAny(WHITESPACE, json, stop));
    if (chomp(']', json, stop)) { return TRUE; }
    while (TRUE) {
        if (!parseValue(json, stop, depth + 1)) { return FALSE; }
        while (chompAny(WHITESPACE, json, stop));
        if (chomp(']', json, stop)) { return TRUE; }
        if (!chomp(',', json, stop)) { return FALSE; }
        while (chompAny(WHITESPACE, json, stop));
    }
}

BOOL parseObject(LPCBYTE *json, LPCBYTE stop, int depth) {
    if (!chomp('{', json, stop)) { return FALSE; }
    while (chompAny(WHITESPACE, json, stop));
    if (chomp('}', json, stop)) { return TRUE; }
    while (TRUE) {
        if  (!parseString(json, stop)) { return FALSE; }
        while (chompAny(WHITESPACE, json, stop));
        if (!chomp(':', json, stop)) { return FALSE; }
        if (!parseValue(json, stop, depth + 1)) { return FALSE; }
        while (chompAny(WHITESPACE, json, stop));
        if (chomp('}', json, stop)) { return TRUE; }
        if (!chomp(',', json, stop)) { return FALSE; }
        while (chompAny(WHITESPACE, json, stop));
    }
}

BOOL parseValue(LPCBYTE *json, LPCBYTE stop, int depth) {
    if (depth > 250) { return FALSE; }
    while (chompAny(WHITESPACE, json, stop));
    if (*json >= stop) { return FALSE; }
    if (**json == '{') { return parseObject(json, stop, depth); }
    if (**json == '[') { return parseArray(json, stop, depth); }
    if (**json == '"') { return parseString(json, stop); }
    if (**json == '-' || (**json >= '0' && **json <= '9')) {
        return parseNumber(json, stop);
    }

    return chompToken("true", json, stop)
        || chompToken("false", json, stop)
        || chompToken("null", json, stop);
}

BOOL parseJSON(LPCBYTE json, LPCBYTE stop) {
    if (!parseValue(&json, stop, 0)) { return FALSE; }
    while (chompAny(WHITESPACE, &json, stop));
    return json >= stop;
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
        LPBYTE json = readFile(path, &stop);
        BOOL valid = parseJSON(json, stop);
        if (info.cFileName[0] != L'i') {
            BOOL expected = info.cFileName[0] == L'y';
            if (valid != expected) {
                wprintf(L"%s\n", info.cFileName);
            }
        }

        free(json);
    } while (FindNextFile(find, &info));

    FindClose(find);
    wprintf(L"tests complete\n");
    return 0;
}

#define UNICODE
#include <windows.h>
#include <stdio.h>
#include "./json_parser.h"

OVERLAPPED fileReadIn;
struct { DWORD errorCode; DWORD byteCount; } fileReadOut;
void CALLBACK fileReadComplete(
    DWORD errorCode, DWORD byteCount, LPOVERLAPPED overlapped
) {
    fileReadOut.errorCode = errorCode;
    fileReadOut.byteCount = byteCount;
}

LPBYTE readFile(LPCWSTR path, LPCBYTE *stop) {
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
    COLORREF labelColor = RGB(1, 2, 3);
    COLORREF borderColor = RGB(4, 5, 6);
    Hook hooks[3] = {
        { .call = expectObject, .frameCount = 0 },
        {
            .call = parseColor,
            .param = &borderColor,
            .frames = { "borderColor" },
            .frameCount = 1,
        },
        {
            .call = parseColor,
            .param = &labelColor,
            .frames = { "labelColor" },
            .frameCount = 1,
        },
    };
    LPCBYTE stop;
    LPBYTE buffer = readFile(SETTINGS_FILENAME, &stop);
    LPCWSTR error = parseJSON(buffer, stop, hooks, 3);
    if (error) { wprintf(L"%s\n", error); }
    wprintf(
        L"labelColor: red = %d, green = %d, blue = %d\n",
        GetRValue(labelColor), GetGValue(labelColor), GetBValue(labelColor)
    );
    wprintf(
        L"borderColor: red = %d, green = %d, blue = %d\n",
        GetRValue(borderColor), GetGValue(borderColor), GetBValue(borderColor)
    );

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
        LPCBYTE stop;
        LPBYTE buffer = readFile(path, &stop);
        LPCWSTR error = parseJSON(buffer, stop, NULL, 0);
        if (error || info.cFileName[0] == L'n') {
            wprintf(
                L"%-50s %s\n", info.cFileName, error ? error : L"No error!"
            );
        }

        free(buffer);
    } while (FindNextFile(find, &info));

    FindClose(find);
    wprintf(L"tests complete\n");
    return 0;
}

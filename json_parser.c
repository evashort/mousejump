#define UNICODE
#include <windows.h>
#include <stdio.h>
#include "./json_parser.h"
#include "./file_reader.h"

int main() {
    COLORREF labelColor = RGB(1, 2, 3);
    COLORREF borderColor = RGB(4, 5, 6);
    Hook hooks[3] = {
        { .call = expectObject, .frameCount = 0 },
        {
            .call = parseColor,
            .param = NULL,
            .dest = &borderColor,
            .frames = { "borderColor" },
            .frameCount = 1,
        },
        {
            .call = parseColor,
            .param = NULL,
            .dest = &labelColor,
            .frames = { "labelColor" },
            .frameCount = 1,
        },
    };
    LPCBYTE stop;
    LPBYTE buffer = readFile(L"settings.json", &stop);
    int lineNumber;
    LPCWSTR error = parseJSON(buffer, stop, hooks, 3, &lineNumber);
    free(buffer);
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
        LPCWSTR error = parseJSON(buffer, stop, NULL, 0, &lineNumber);
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

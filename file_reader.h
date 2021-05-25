#define UNICODE
#include <windows.h>

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
        LPBYTE buffer = (LPBYTE)malloc(fileSize.LowPart);
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

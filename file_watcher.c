#define UNICODE
#include <windows.h>
#include <stdio.h>

#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")

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

LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM, LPARAM);

typedef struct {
    HANDLE signal;
    HWND window;
    HANDLE folder;
    LPBYTE changeBuffer;
    size_t bufferSize;
    LPOVERLAPPED overlapped;
    LPCWSTR filename;
    int filenameLength;
} ThreadParam;
// https://docs.microsoft.com/en-us/windows/win32/procthread/creating-threads
DWORD WINAPI myThreadFunction(LPVOID param) {
    ThreadParam *params = (ThreadParam*)param;
    HANDLE handles[2];
    handles[0] = params->signal;
    handles[1] = params->overlapped->hEvent;
    int i = 0;
    while (TRUE) {
        DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, 5000);
        if (waitResult == WAIT_OBJECT_0) {
            return 0;
        } else if (waitResult == WAIT_OBJECT_0 + 1) {
            DWORD bufferLength;
            BOOL overlappedResult = GetOverlappedResult(
                params->folder, params->overlapped, &bufferLength, FALSE
            );
            if (!overlappedResult) { break; }
            FILE_NOTIFY_INFORMATION *change
                = (FILE_NOTIFY_INFORMATION*)params->changeBuffer;
            BOOL match = FALSE;
            while ((LPBYTE)change < params->changeBuffer + bufferLength) {
                if (
                    change->FileNameLength / sizeof(WCHAR)
                        == params->filenameLength
                ) {
                    match = TRUE;
                    for (int i = 0; i < params->filenameLength; i++) {
                        if (change->FileName[i] != params->filename[i]) {
                            match = FALSE;
                            break;
                        }
                    }
                }

                if (match || change->NextEntryOffset == 0) { break; }
                (LPBYTE)change += change->NextEntryOffset;
            }

            BOOL watchResult = ReadDirectoryChangesW(
                params->folder,
                params->changeBuffer,
                params->bufferSize,
                FALSE, // bWatchSubtree
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE
                    | FILE_NOTIFY_CHANGE_SIZE,
                NULL, // lpBytesReturned (synchronous calls only)
                params->overlapped,
                NULL // lpCompletionRoutine
            );
            if (match) {
                i++;
                PostMessage(params->window, WM_APP, i, 0);
            }

            if (!watchResult) { break; }
        } else { break; }
    }

    return 1;
}

HANDLE thread = INVALID_HANDLE_VALUE;
HANDLE signalHandle = INVALID_HANDLE_VALUE;
HANDLE folder;
OVERLAPPED overlapped = { .hEvent = INVALID_HANDLE_VALUE };
byte changeBuffer[2048];
int CALLBACK WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine,
                     int nCmdShow) {
    folder = CreateFile(
        L"watch_folder",
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        // FILE_FLAG_OVERLAPPED makes ReadDirectoryChangesW asynchronous
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );
    if (folder == INVALID_HANDLE_VALUE) {
        MessageBox(NULL, L"could not open folder", L"file watcher", MB_OK);
        return 1;
    }

    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (overlapped.hEvent == INVALID_HANDLE_VALUE) {
        MessageBox(NULL, L"could not create event", L"file watcher", MB_OK);
        return 1;
    }

    BOOL watchResult = ReadDirectoryChangesW(
        folder,
        &changeBuffer,
        sizeof(changeBuffer),
        FALSE, // bWatchSubtree
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE
            | FILE_NOTIFY_CHANGE_SIZE,
        NULL, // lpBytesReturned (synchronous calls only)
        &overlapped,
        NULL // lpCompletionRoutine
    );
    if (!watchResult) {
        MessageBox(NULL, L"could not start watching", L"file watcher", MB_OK);
        if (folder != INVALID_HANDLE_VALUE) { CloseHandle(folder); }
        return 1;
    }

    LPBYTE bufferStop;
    LPBYTE buffer = readFile(L"watch_me.txt", &bufferStop);
    int contentLength = MultiByteToWideChar(
        CP_UTF8, MB_PRECOMPOSED, (LPCSTR)buffer, bufferStop - buffer, NULL, 0
    );
    LPWSTR content = malloc(contentLength + 1 * sizeof(WCHAR));
    MultiByteToWideChar(
        CP_UTF8, MB_PRECOMPOSED, (LPCSTR)buffer, bufferStop - buffer, content,
        contentLength
    );
    free(buffer);
    content[contentLength] = L'\0';
    LPCWSTR caption;
    WNDCLASS windowClass;
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.style = CS_HREDRAW | CS_VREDRAW; // repaint if resized
    windowClass.lpfnWndProc   = WndProc; // message handler, see part 3
    windowClass.hInstance     = hInstance;
    windowClass.hCursor       = LoadCursor(0, IDC_ARROW);
    windowClass.hbrBackground = GetStockObject(WHITE_BRUSH);
    windowClass.lpszClassName = L"MyWindowClass";
    RegisterClass(&windowClass);
    HWND window = CreateWindow(
        L"MyWindowClass",
        content,
        WS_OVERLAPPEDWINDOW,
        // x, y, width, height
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, // no parent window, no menu bar
        hInstance,
        NULL // lpParam
    );
    ShowWindow(window, SW_SHOWNORMAL); // not minimized or maximized
    MSG message;
    while (GetMessage(&message, NULL, 0, 0)) { // main loop
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    if (thread != INVALID_HANDLE_VALUE) {
        SetEvent(signalHandle);
        if (WaitForSingleObject(thread, 2000) != WAIT_OBJECT_0) {
            MessageBox(NULL, L"thread didn't stop", L"file watcher", MB_OK);
        }

        DWORD exitCode = 1;
        GetExitCodeThread(thread, &exitCode);
        if (exitCode != 0) {
            MessageBox(NULL, L"error in thread", L"file watcher", MB_OK);
        }

        CloseHandle(thread);
    }

    if (folder != INVALID_HANDLE_VALUE) { CloseHandle(folder); }
    free(content);
    if (signalHandle != INVALID_HANDLE_VALUE) { CloseHandle(signalHandle); }
    MessageBox(NULL, L"exited cleanly", L"file watcher", MB_OK);
    return 0;
}

LRESULT CALLBACK WndProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CREATE) {
        signalHandle = CreateEvent(NULL, TRUE, FALSE, L"signal");
        if (signalHandle == INVALID_HANDLE_VALUE) {
            MessageBox(
                NULL, L"could not create signal", L"file watcher", MB_OK
            );
        }

        static const WCHAR settingsFilename[] = L"settings.json";
        static ThreadParam params;
        params.signal = signalHandle;
        params.window = window;
        params.folder = folder;
        params.changeBuffer = changeBuffer;
        params.bufferSize = sizeof(changeBuffer);
        params.overlapped = &overlapped;
        params.filename = settingsFilename;
        params.filenameLength = sizeof(settingsFilename) / sizeof(WCHAR) - 1;
        DWORD threadID;
        thread = CreateThread(
            NULL, 0, myThreadFunction, &params, 0, &threadID
        );
        if (thread == NULL) {
            MessageBox(
                NULL, L"could not create thread", L"file watcher", MB_OK
            );
        }

        return 0;
    } else if (message == WM_PAINT) {
        PAINTSTRUCT paintStruct;
        HDC device = BeginPaint(window, &paintStruct);
        RECT clientRect;
        GetClientRect(window, &clientRect);
        DrawText(device, // device context handle
                 L"hello world",
                 -1, // character count, -1 for null-terminated string
                 &clientRect, // bounding box is entire window area
                 DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        EndPaint(window, &paintStruct);
        return 0;
    } else if (message == WM_APP) {
        WCHAR caption[25];
        SetWindowText(window, _itow(wParam, caption, 10));
        return 0;
    } else if (message == WM_DESTROY) {
        PostQuitMessage(0); // exit when window is closed
        return 0;
    } else // use default behavior for all other messages
        return DefWindowProc(window, message, wParam, lParam);
}

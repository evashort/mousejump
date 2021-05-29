#define UNICODE
#include <windows.h>
#include <stdio.h>

#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")

LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM, LPARAM);

typedef struct {
    HANDLE exitEvent;
    HWND window;
    HANDLE folder;
    LPBYTE changeBuffer;
    size_t bufferSize;
    LPOVERLAPPED changeIO;
    LPCWSTR path;
    int pathLength;
    UINT changeMessage;
    HANDLE *file;
    LPOVERLAPPED contentIO;
    LPBYTE *content;
    UINT parseMessage;
} ThreadParam;
typedef struct {
    HANDLE file;
    LPBYTE *buffer;
    OVERLAPPED *io;
} ReadRequest;
typedef struct {
    LPBYTE buffer;
    int size;
    HANDLE event;
} ParseRequest;
// https://docs.microsoft.com/en-us/windows/win32/procthread/creating-threads
DWORD WINAPI myThreadFunction(LPVOID param) {
    ThreadParam *params = (ThreadParam*)param;
    int i = 0;
    // saving a file usually generates two or three separate
    // FILE_ACTION_MODIFIED notifications. waiting 30 milliseconds for another
    // notification is enough to deduplicate them at least 99% of the time,
    // although that percentage is probably much lower before the system warms
    // up.
    int consecutiveChanges = 0;
    DWORD waitIntervals[4] = { INFINITE, 30, 30, 0 };
    BOOL shouldRead = FALSE;
    static const int EXIT_INDEX = 0, CHANGE_INDEX = 1, CONTENT_INDEX = 2;
    HANDLE handles[3];
    handles[EXIT_INDEX] = params->exitEvent;
    handles[CHANGE_INDEX] = params->changeIO->hEvent;
    handles[CONTENT_INDEX] = params->contentIO->hEvent;
    static const int WATCH_STATE = 0, READ_STATE = 1, PARSE_STATE = 2;
    int state = WATCH_STATE;
    BOOL fileChanged = FALSE;
    while (TRUE) {
        BOOL shouldRead = FALSE;
        DWORD waitResult = WaitForMultipleObjects(
            2 + (state != WATCH_STATE), handles, FALSE,
            (state == WATCH_STATE && fileChanged) ? 30 : INFINITE
        );
        if (waitResult == WAIT_OBJECT_0 + EXIT_INDEX) {
            return 0;
        } else if (waitResult == WAIT_OBJECT_0 + CHANGE_INDEX) {
            DWORD bufferLength;
            BOOL overlappedResult = GetOverlappedResult(
                params->folder, params->changeIO, &bufferLength, FALSE
            );
            if (!overlappedResult) { break; }
            FILE_NOTIFY_INFORMATION *change
                = (FILE_NOTIFY_INFORMATION*)params->changeBuffer;
            // https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw#remarks
            BOOL match = bufferLength == 0;
            while ((LPBYTE)change < params->changeBuffer + bufferLength) {
                int length = change->FileNameLength / sizeof(WCHAR);
                int offset = params->pathLength - length;
                if (
                    offset == 0 || params->path[offset - 1] == L'/'
                        || params->path[offset - 1] == L'\\'
                ) {
                    match = TRUE;
                    for (int i = 0; i < length; i++) {
                        if (change->FileName[i] != params->path[offset + i]) {
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
                params->changeIO,
                NULL // lpCompletionRoutine
            );
            if (!watchResult) {
                break;
            }

            shouldRead = match && state == WATCH_STATE && !fileChanged;
            fileChanged = fileChanged || match;
        } else if (
            state == WATCH_STATE && fileChanged && waitResult == WAIT_TIMEOUT
        ) {
            shouldRead = TRUE;
        } else if (
            state == READ_STATE && waitResult == WAIT_OBJECT_0 + CONTENT_INDEX
        ) {
            DWORD contentSize;
            BOOL overlappedResult = GetOverlappedResult(
                *params->file, params->contentIO, &contentSize, FALSE
            );
            if (!overlappedResult) { break; }
            if (!CloseHandle(*params->file)) { break; }
            *params->file = INVALID_HANDLE_VALUE;
            if (!ResetEvent(params->contentIO->hEvent)) { break; }
            static ParseRequest request;
            request.buffer = *params->content;
            request.size = contentSize;
            request.event = params->contentIO->hEvent;
            PostMessage(
                params->window, params->parseMessage, 0, (LPARAM)&request
            );
            state = PARSE_STATE;
        } else if (
            state == PARSE_STATE
                && waitResult == WAIT_OBJECT_0 + CONTENT_INDEX
        ) {
            if (!ResetEvent(params->contentIO->hEvent)) { break; }
            state = WATCH_STATE;
            shouldRead = fileChanged;
        } else {
            break;
        }

        if (shouldRead) {
            // https://docs.microsoft.com/en-us/windows/win32/fileio/opening-a-file-for-reading-or-writing
            *params->file = CreateFile(
                params->path,
                GENERIC_READ,
                FILE_SHARE_READ,
                NULL, // default security settings
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                NULL // no attribute template
            );
            if (*params->file == INVALID_HANDLE_VALUE) {
                state = WATCH_STATE;
                fileChanged = GetLastError() == ERROR_SHARING_VIOLATION;
            } else {
                i++;
                static ReadRequest request;
                request.file = *params->file;
                request.buffer = params->content;
                request.io = params->contentIO;
                BOOL result = PostMessage(
                    params->window, params->changeMessage, i,
                    (LPARAM)&request
                );
                if (!result) { break; }
                state = READ_STATE;
                fileChanged = FALSE;
            }
        }
    }

    return 1;
}

HANDLE thread = INVALID_HANDLE_VALUE;
HANDLE exitEvent = INVALID_HANDLE_VALUE;
HANDLE folder;
OVERLAPPED changeIO = { .hEvent = INVALID_HANDLE_VALUE };
byte changeBuffer[2048];
HANDLE file = NULL;
LPBYTE content = NULL;
OVERLAPPED contentIO = { .hEvent = INVALID_HANDLE_VALUE };
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

    changeIO.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (changeIO.hEvent == INVALID_HANDLE_VALUE) {
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
        &changeIO,
        NULL // lpCompletionRoutine
    );
    if (!watchResult) {
        MessageBox(NULL, L"could not start watching", L"file watcher", MB_OK);
        if (folder != INVALID_HANDLE_VALUE) { CloseHandle(folder); }
        return 1;
    }

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
        L"file watcher",
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
        SetEvent(exitEvent);
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
    if (exitEvent != INVALID_HANDLE_VALUE) { CloseHandle(exitEvent); }
    if (changeIO.hEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(changeIO.hEvent);
    }

    if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); }
    if (contentIO.hEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(contentIO.hEvent);
    }

    MessageBox(NULL, L"exited cleanly", L"file watcher", MB_OK);
    return 0;
}

// TODO: wait no this function won't get called unless the thread is in an
// alertable wait state.
void CALLBACK contentCallback(
    DWORD errorCode, DWORD byteCount, LPOVERLAPPED io
) {
    PostMessage((HWND)io->hEvent, WM_APP + 1, errorCode, byteCount);
}

LRESULT CALLBACK WndProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CREATE) {
        exitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (exitEvent == INVALID_HANDLE_VALUE) {
            MessageBox(
                NULL, L"could not create exit event", L"file watcher", MB_OK
            );
        }

        contentIO.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (contentIO.hEvent == INVALID_HANDLE_VALUE) {
            MessageBox(
                NULL, L"could not create read event", L"file watcher", MB_OK
            );
        }

        static const WCHAR settingsPath[] = L"watch_folder/settings.json";
        static ThreadParam params;
        params.exitEvent = exitEvent;
        params.window = window;
        params.folder = folder;
        params.changeBuffer = changeBuffer;
        params.bufferSize = sizeof(changeBuffer);
        params.changeIO = &changeIO;
        params.path = settingsPath;
        params.pathLength = sizeof(settingsPath) / sizeof(WCHAR) - 1;
        params.changeMessage = WM_APP;
        params.file = &file;
        params.contentIO = &contentIO;
        params.content = &content;
        params.parseMessage = WM_APP + 1;
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
        ReadRequest *request = (ReadRequest*)lParam;
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(request->file, &fileSize)) {
            MessageBox(
                NULL, L"could not get file size", L"file watcher", MB_OK
            );
        } else if (fileSize.HighPart != 0) {
            MessageBox(NULL, L"file is too big", L"file watcher", MB_OK);
        }

        *request->buffer = (LPBYTE)realloc(
            *request->buffer, fileSize.LowPart
        );
        if (fileSize.LowPart > 0 && *request->buffer == NULL) {
            MessageBox(
                NULL, L"not enough memory for file", L"file watcher", MB_OK
            );
        }

        request->io->Offset = request->io->OffsetHigh = 0;
        BOOL result = ReadFile(
            request->file,
            *request->buffer,
            fileSize.LowPart,
            NULL,
            request->io
        );
        if (result && GetLastError() != ERROR_IO_PENDING) {
            MessageBox(NULL, L"could not read file", L"file watcher", MB_OK);
        }

        return 0;
    } else if (message == WM_APP + 1) {
        ParseRequest *request = (ParseRequest*)lParam;
        WCHAR caption[100];
        int captionLength = MultiByteToWideChar(
            CP_UTF8, MB_PRECOMPOSED, (LPCSTR)request->buffer, request->size,
            caption, 99
        );
        caption[captionLength] = L'\0';
        SetWindowText(window, caption);
        SetEvent(request->event);
        return 0;
    } else if (message == WM_DESTROY) {
        PostQuitMessage(0); // exit when window is closed
        return 0;
    } else // use default behavior for all other messages
        return DefWindowProc(window, message, wParam, lParam);
}

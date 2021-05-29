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
    LPBYTE changes;
    size_t changesSize;
    LPOVERLAPPED watchIO;
    LPCWSTR path;
    int pathLength;
    UINT changeMessage;
    HANDLE *file;
    LPOVERLAPPED loadIO;
    LPBYTE *content;
    UINT loadedMessage;
} ThreadParam;
typedef struct {
    HANDLE file;
    LPBYTE *buffer;
    OVERLAPPED *io;
} LoadRequest;
typedef struct {
    LPBYTE buffer;
    int size;
    HANDLE event;
} ParseRequest;
// https://docs.microsoft.com/en-us/windows/win32/procthread/creating-threads
DWORD WINAPI myThreadFunction(LPVOID param) {
    ThreadParam *params = (ThreadParam*)param;
    static const int EXIT_EVENT = 0, CHANGE_EVENT = 1, LOADED_EVENT = 2;
    HANDLE events[3];
    events[EXIT_EVENT] = params->exitEvent;
    events[CHANGE_EVENT] = params->watchIO->hEvent;
    events[LOADED_EVENT] = params->loadIO->hEvent;
    static const int WATCH_STATE = 0, LOAD_STATE = 1, PARSE_STATE = 2;
    int state = WATCH_STATE;
    BOOL fileChanged = FALSE;
    while (TRUE) {
        BOOL shouldRead = FALSE;
        DWORD waitResult = WaitForMultipleObjects(
            2 + (state != WATCH_STATE), events, FALSE,
            (state == WATCH_STATE && fileChanged) ? 30 : INFINITE
        );
        if (waitResult == WAIT_OBJECT_0 + EXIT_EVENT) {
            return 0;
        } else if (waitResult == WAIT_OBJECT_0 + CHANGE_EVENT) {
            DWORD bufferLength;
            BOOL overlappedResult = GetOverlappedResult(
                params->folder, params->watchIO, &bufferLength, FALSE
            );
            if (!overlappedResult) { break; }
            FILE_NOTIFY_INFORMATION *change
                = (FILE_NOTIFY_INFORMATION*)params->changes;
            // https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw#remarks
            BOOL match = bufferLength == 0;
            while ((LPBYTE)change < params->changes + bufferLength) {
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
                params->changes,
                params->changesSize,
                FALSE, // bWatchSubtree
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE
                    | FILE_NOTIFY_CHANGE_SIZE,
                NULL, // lpBytesReturned (synchronous calls only)
                params->watchIO,
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
            state == LOAD_STATE && waitResult == WAIT_OBJECT_0 + LOADED_EVENT
        ) {
            DWORD contentSize;
            BOOL overlappedResult = GetOverlappedResult(
                *params->file, params->loadIO, &contentSize, FALSE
            );
            if (!overlappedResult) { break; }
            if (!CloseHandle(*params->file)) { break; }
            *params->file = INVALID_HANDLE_VALUE;
            if (!ResetEvent(params->loadIO->hEvent)) { break; }
            static ParseRequest request;
            request.buffer = *params->content;
            request.size = contentSize;
            request.event = params->loadIO->hEvent;
            PostMessage(
                params->window, params->loadedMessage, 0, (LPARAM)&request
            );
            state = PARSE_STATE;
        } else if (
            state == PARSE_STATE
                && waitResult == WAIT_OBJECT_0 + LOADED_EVENT
        ) {
            if (!ResetEvent(params->loadIO->hEvent)) { break; }
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
                static LoadRequest request;
                request.file = *params->file;
                request.buffer = params->content;
                request.io = params->loadIO;
                BOOL result = PostMessage(
                    params->window, params->changeMessage, 0, (LPARAM)&request
                );
                if (!result) { break; }
                state = LOAD_STATE;
                fileChanged = FALSE;
            }
        }
    }

    return 1;
}

HANDLE thread = INVALID_HANDLE_VALUE;
HANDLE exitEvent = INVALID_HANDLE_VALUE;
HANDLE folder;
OVERLAPPED watchIO = { .hEvent = INVALID_HANDLE_VALUE };
byte changes[2048];
HANDLE file = INVALID_HANDLE_VALUE;
LPBYTE content = NULL;
OVERLAPPED loadIO = { .hEvent = INVALID_HANDLE_VALUE };
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

    watchIO.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (watchIO.hEvent == INVALID_HANDLE_VALUE) {
        MessageBox(NULL, L"could not create event", L"file watcher", MB_OK);
        return 1;
    }

    BOOL watchResult = ReadDirectoryChangesW(
        folder,
        &changes,
        sizeof(changes),
        FALSE, // bWatchSubtree
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE
            | FILE_NOTIFY_CHANGE_SIZE,
        NULL, // lpBytesReturned (synchronous calls only)
        &watchIO,
        NULL // lpCompletionRoutine
    );
    if (!watchResult) {
        MessageBox(NULL, L"could not start watching", L"file watcher", MB_OK);
        if (folder != INVALID_HANDLE_VALUE) { CloseHandle(folder); }
        return 1;
    }

    // https://docs.microsoft.com/en-us/windows/win32/fileio/opening-a-file-for-reading-or-writing
    file = CreateFile(
        L"watch_folder/settings.json",
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL, // default security settings
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL // no attribute template
    );
    if (file == INVALID_HANDLE_VALUE) {
        MessageBox(NULL, L"could not open file", L"file watcher", MB_OK);
        return 1;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(file, &fileSize)) {
        MessageBox(NULL, L"could not get file size", L"file watcher", MB_OK);
        return 1;
    } else if (fileSize.HighPart != 0) {
        MessageBox(NULL, L"file is too big", L"file watcher", MB_OK);
        return 1;
    }

    content = (LPBYTE)realloc(content, fileSize.LowPart);
    if (fileSize.LowPart > 0 && content == NULL) {
        MessageBox(
            NULL, L"not enough memory for file", L"file watcher", MB_OK
        );
        return 1;
    }

    DWORD contentSize;
    BOOL readResult = ReadFile(
        file, content, fileSize.LowPart, &contentSize, NULL
    );
    if (!readResult) {
        MessageBox(NULL, L"could not read file", L"file watcher", MB_OK);
        return 1;
    }

    if (!CloseHandle(file)) {
        MessageBox(NULL, L"could not close file", L"file watcher", MB_OK);
        return 1;
    }

    file = INVALID_HANDLE_VALUE;

    WCHAR caption[100];
    int captionLength = MultiByteToWideChar(
        CP_UTF8, MB_PRECOMPOSED, (LPCSTR)content, contentSize, caption, 99
    );
    caption[captionLength] = L'\0';

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
        caption,
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
    if (watchIO.hEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(watchIO.hEvent);
    }

    if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); }
    if (loadIO.hEvent != INVALID_HANDLE_VALUE) {
        CloseHandle(loadIO.hEvent);
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

        loadIO.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (loadIO.hEvent == INVALID_HANDLE_VALUE) {
            MessageBox(
                NULL, L"could not create read event", L"file watcher", MB_OK
            );
        }

        static const WCHAR settingsPath[] = L"watch_folder/settings.json";
        static ThreadParam params;
        params.exitEvent = exitEvent;
        params.window = window;
        params.folder = folder;
        params.changes = changes;
        params.changesSize = sizeof(changes);
        params.watchIO = &watchIO;
        params.path = settingsPath;
        params.pathLength = sizeof(settingsPath) / sizeof(WCHAR) - 1;
        params.changeMessage = WM_APP;
        params.file = &file;
        params.loadIO = &loadIO;
        params.content = &content;
        params.loadedMessage = WM_APP + 1;
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
        LoadRequest *request = (LoadRequest*)lParam;
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
        if (!result && GetLastError() != ERROR_IO_PENDING) {
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

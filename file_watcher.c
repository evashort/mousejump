#define UNICODE
#include <windows.h>

#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")

LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM, LPARAM);

typedef struct {
    HANDLE thread;
    HANDLE exitEvent;
    HWND window;
    HANDLE folder;
    byte changes[2048];
    OVERLAPPED watchIO;
    LPCWSTR path;
    int pathLength;
    UINT changeMessage;
    HANDLE file;
    OVERLAPPED loadIO;
    LPBYTE content;
    UINT loadedMessage;
} WatcherData;

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

typedef enum {
    WATCHER_THREAD_SUCCESS = 0,
    WATCHER_THREAD_GET_CHANGE,
    WATCHER_THREAD_WATCH,
    WATCHER_THREAD_LOAD_RESULT,
    WATCHER_THREAD_CLOSE_FILE,
    WATCHER_THREAD_RESET_EVENT,
    WATCHER_THREAD_SEND_MESSAGE,
    WATCHER_THREAD_BAD_STATE,
    WATCHER_THREAD_TIMEOUT,
    WATCHER_THREAD_CLOSE_FOLDER,
    WATCHER_THREAD_CLOSE_EVENT,
    WATCHER_THREAD_CLOSE_THREAD,
} WatcherThreadError;
// https://docs.microsoft.com/en-us/windows/win32/procthread/creating-threads
DWORD WINAPI fileWatcherThread(LPVOID param) {
    WatcherData *data = (WatcherData*)param;
    static const int EXIT_EVENT = 0, CHANGE_EVENT = 1, LOADED_EVENT = 2;
    HANDLE events[3];
    events[EXIT_EVENT] = data->exitEvent;
    events[CHANGE_EVENT] = data->watchIO.hEvent;
    events[LOADED_EVENT] = data->loadIO.hEvent;
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
            return WATCHER_THREAD_SUCCESS;
        } else if (waitResult == WAIT_OBJECT_0 + CHANGE_EVENT) {
            DWORD bufferLength;
            BOOL overlappedResult = GetOverlappedResult(
                data->folder, &data->watchIO, &bufferLength, FALSE
            );
            if (!overlappedResult) {
                return WATCHER_THREAD_GET_CHANGE;
            }

            FILE_NOTIFY_INFORMATION *change
                = (FILE_NOTIFY_INFORMATION*)data->changes;
            // https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw#remarks
            BOOL match = bufferLength == 0;
            while ((LPBYTE)change < data->changes + bufferLength) {
                int length = change->FileNameLength / sizeof(WCHAR);
                int offset = data->pathLength - length;
                if (
                    offset == 0 || data->path[offset - 1] == L'/'
                        || data->path[offset - 1] == L'\\'
                ) {
                    match = TRUE;
                    for (int i = 0; i < length; i++) {
                        if (change->FileName[i] != data->path[offset + i]) {
                            match = FALSE;
                            break;
                        }
                    }
                }

                if (match || change->NextEntryOffset == 0) { break; }
                (LPBYTE)change += change->NextEntryOffset;
            }

            BOOL watchResult = ReadDirectoryChangesW(
                data->folder,
                data->changes,
                sizeof(data->changes),
                FALSE, // bWatchSubtree
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE
                    | FILE_NOTIFY_CHANGE_SIZE,
                NULL, // lpBytesReturned (synchronous calls only)
                &data->watchIO,
                NULL // lpCompletionRoutine
            );
            if (!watchResult) {
                return WATCHER_THREAD_WATCH;
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
            state = PARSE_STATE;
            DWORD contentSize;
            BOOL overlappedResult = GetOverlappedResult(
                data->file, &data->loadIO, &contentSize, FALSE
            );
            if (!overlappedResult) {
                return WATCHER_THREAD_LOAD_RESULT;
            }

            if (!CloseHandle(data->file)) {
                return WATCHER_THREAD_CLOSE_FILE;
            }

            data->file = INVALID_HANDLE_VALUE;
            if (!ResetEvent(data->loadIO.hEvent)) {
                return WATCHER_THREAD_RESET_EVENT;
            }

            static ParseRequest request;
            request.buffer = data->content;
            request.size = contentSize;
            request.event = data->loadIO.hEvent;
            BOOL result = PostMessage(
                data->window, data->loadedMessage, 0, (LPARAM)&request
            );
            if (!result) {
                return WATCHER_THREAD_SEND_MESSAGE;
            }
        } else if (
            state == PARSE_STATE
                && waitResult == WAIT_OBJECT_0 + LOADED_EVENT
        ) {
            state = WATCH_STATE;
            shouldRead = fileChanged;
            if (!ResetEvent(data->loadIO.hEvent)) {
                return WATCHER_THREAD_RESET_EVENT;
            }
        } else {
            return WATCHER_THREAD_BAD_STATE;
        }

        if (shouldRead) {
            // https://docs.microsoft.com/en-us/windows/win32/fileio/opening-a-file-for-reading-or-writing
            data->file = CreateFile(
                data->path,
                GENERIC_READ,
                FILE_SHARE_READ,
                NULL, // default security settings
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                NULL // no attribute template
            );
            if (data->file == INVALID_HANDLE_VALUE) {
                state = WATCH_STATE;
                fileChanged = GetLastError() == ERROR_SHARING_VIOLATION;
            } else {
                state = LOAD_STATE;
                fileChanged = FALSE;
                static LoadRequest request;
                request.file = data->file;
                request.buffer = &data->content;
                request.io = &data->loadIO;
                BOOL result = PostMessage(
                    data->window, data->changeMessage, 0, (LPARAM)&request
                );
                if (!result) {
                    return WATCHER_THREAD_SEND_MESSAGE;
                }
            }
        }
    }
}

typedef enum {
    WATCHER_INIT_SUCCESS = 0,
    WATCHER_INIT_OPEN_FOLDER,
    WATCHER_INIT_CREATE_EVENT,
    WATCHER_INIT_START_WATCHING,
} WatcherInitError;
WatcherInitError initializeWatcher(
    WatcherData *data, LPWSTR path, UINT changeMessage, UINT loadedMessage
) {
    data->thread = INVALID_HANDLE_VALUE;
    data->exitEvent = INVALID_HANDLE_VALUE;
    data->window = NULL;
    data->folder = INVALID_HANDLE_VALUE;
    data->watchIO.hEvent = INVALID_HANDLE_VALUE;
    data->path = path;
    // store pathLength so fileWatcherThread can avoid calling wcslen because
    // standard library functions are not thread safe.
    data->pathLength = wcslen(path);
    data->changeMessage = changeMessage;
    data->file = INVALID_HANDLE_VALUE;
    data->loadIO.hEvent = INVALID_HANDLE_VALUE;
    data->content = NULL;

    data->loadedMessage = loadedMessage;
    data->folder = CreateFile(
        L"watch_folder",
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        // FILE_FLAG_OVERLAPPED makes ReadDirectoryChangesW asynchronous
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );
    if (data->folder == INVALID_HANDLE_VALUE) {
        return WATCHER_INIT_OPEN_FOLDER;
    }

    data->watchIO.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (data->watchIO.hEvent == NULL) {
        data->watchIO.hEvent = INVALID_HANDLE_VALUE;
        CloseHandle(data->folder);
        data->folder = INVALID_HANDLE_VALUE;
        return WATCHER_INIT_CREATE_EVENT;
    }

    BOOL watchResult = ReadDirectoryChangesW(
        data->folder,
        data->changes,
        sizeof(data->changes),
        FALSE, // bWatchSubtree
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE
            | FILE_NOTIFY_CHANGE_SIZE,
        NULL, // lpBytesReturned (synchronous calls only)
        &data->watchIO,
        NULL // lpCompletionRoutine
    );
    if (!watchResult) {
        CloseHandle(data->watchIO.hEvent);
        data->watchIO.hEvent = INVALID_HANDLE_VALUE;
        CloseHandle(data->folder);
        data->folder = INVALID_HANDLE_VALUE;
        return WATCHER_INIT_START_WATCHING;
    }

    return WATCHER_INIT_SUCCESS;
}

typedef enum {
    WATCHER_LOAD_SUCCESS = 0,
    WATCHER_LOAD_OPEN_FILE,
    WATCHER_LOAD_FILE_SIZE,
    WATCHER_LOAD_TOO_BIG,
    WATCHER_LOAD_NO_MEMORY,
    WATCHER_LOAD_READ_FILE,
    WATCHER_LOAD_CLOSE_FILE,
} WatcherLoadError;
WatcherLoadError readFile(LPCWSTR path, LPBYTE *buffer, DWORD *size) {
    // https://docs.microsoft.com/en-us/windows/win32/fileio/opening-a-file-for-reading-or-writing
    HANDLE file = CreateFile(
        L"watch_folder/settings.json",
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL, // default security settings
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL // no attribute template
    );
    if (file == INVALID_HANDLE_VALUE) {
        return WATCHER_LOAD_OPEN_FILE;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(file, &fileSize)) {
        CloseHandle(file); return WATCHER_LOAD_FILE_SIZE;
    } else if (fileSize.HighPart != 0) {
        CloseHandle(file); return WATCHER_LOAD_TOO_BIG;
    }

    *buffer = (LPBYTE)realloc(*buffer, fileSize.LowPart);
    if (fileSize.LowPart > 0 && *buffer == NULL) {
        CloseHandle(file); return WATCHER_LOAD_NO_MEMORY;
    }

    BOOL readResult = ReadFile(
        file, *buffer, fileSize.LowPart, size, NULL
    );
    if (!readResult) {
        CloseHandle(file); return WATCHER_LOAD_READ_FILE;
    }

    if (!CloseHandle(file)) {
        return WATCHER_LOAD_CLOSE_FILE;
    }

    return WATCHER_LOAD_SUCCESS;
}


typedef enum {
    WATCHER_START_SUCCESS = 0,
    WATCHER_START_CREATE_EVENT,
    WATCHER_START_CREATE_THREAD,
} WatcherStartError;
WatcherStartError startWatcher(WatcherData *data, HWND window) {
    data->window = window;
    data->exitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (data->exitEvent == NULL) {
        data->exitEvent = INVALID_HANDLE_VALUE;
        return WATCHER_START_CREATE_EVENT;
    }

    data->loadIO.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (data->loadIO.hEvent == NULL) {
        data->loadIO.hEvent = INVALID_HANDLE_VALUE;
        return WATCHER_START_CREATE_EVENT;
    }

    DWORD threadID;
    data->thread = CreateThread(
        NULL, 0, fileWatcherThread, data, 0, &threadID
    );
    if (data->thread == NULL) {
        data->thread = INVALID_HANDLE_VALUE;
        return WATCHER_START_CREATE_THREAD;
    }

    return WATCHER_START_SUCCESS;
}

WatcherThreadError stopWatcher(WatcherData *data) {
    WatcherThreadError error = WATCHER_THREAD_SUCCESS;
    BOOL closeHandleFailed = FALSE;
    if (data->thread != INVALID_HANDLE_VALUE) {
        SetEvent(data->exitEvent);
        if (WaitForSingleObject(data->thread, 2000) != WAIT_OBJECT_0) {
            error = WATCHER_THREAD_TIMEOUT;
        } else {
            GetExitCodeThread(data->thread, (LPDWORD)&error);
        }

        if (!CloseHandle(data->thread) && error == WATCHER_THREAD_SUCCESS) {
            error = WATCHER_THREAD_CLOSE_THREAD;
        }
    }

    if (
        data->folder != INVALID_HANDLE_VALUE
            && !CloseHandle(data->folder)
            && error == WATCHER_THREAD_SUCCESS
    ) { error = WATCHER_THREAD_CLOSE_FOLDER; }
    free(data->content);
    if (
        data->exitEvent != INVALID_HANDLE_VALUE
            && !CloseHandle(data->exitEvent)
            && error == WATCHER_THREAD_SUCCESS
    ) { error = WATCHER_THREAD_CLOSE_EVENT; }
    if (
        data->watchIO.hEvent != INVALID_HANDLE_VALUE
            && !CloseHandle(data->watchIO.hEvent)
            && error == WATCHER_THREAD_SUCCESS
    ) { error = WATCHER_THREAD_CLOSE_EVENT; }
    if (
        data->file != INVALID_HANDLE_VALUE && !CloseHandle(data->file)
            && error == WATCHER_THREAD_SUCCESS
    ) { error = WATCHER_THREAD_CLOSE_FILE; }
    if (
        data->loadIO.hEvent != INVALID_HANDLE_VALUE
            && !CloseHandle(data->loadIO.hEvent)
            && error == WATCHER_THREAD_SUCCESS
    ) { error = WATCHER_THREAD_CLOSE_EVENT; }
    return error;
}

WatcherData watcherData;
int CALLBACK WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine,
                     int nCmdShow) {
    WatcherInitError initResult = initializeWatcher(
        &watcherData, L"watch_folder/settings.json", WM_APP, WM_APP + 1
    );
    if (initResult != WATCHER_INIT_SUCCESS) {
        LPCWSTR message = L"missing message for error";
        if (initResult == WATCHER_INIT_OPEN_FOLDER) {
            message = L"could not open folder";
        } else if (initResult == WATCHER_INIT_CREATE_EVENT) {
            message = L"could not create change event";
        } else if (initResult == WATCHER_INIT_START_WATCHING) {
            message = L"could not start watching";
        }

        MessageBox(NULL, message, L"file watcher", MB_OK);
        return 1;
    }

    DWORD contentSize;
    WatcherLoadError loadResult = readFile(
        watcherData.path, &watcherData.content, &contentSize
    );
    if (loadResult != WATCHER_LOAD_SUCCESS) {
        LPCWSTR message = L"missing message for error";
        if (initResult == WATCHER_LOAD_OPEN_FILE) {
            message = L"could not open file";
        } else if (initResult == WATCHER_LOAD_FILE_SIZE) {
            message = L"could not get file size";
        } else if (initResult == WATCHER_LOAD_TOO_BIG) {
            message = L"file is too big";
        } else if (initResult == WATCHER_LOAD_NO_MEMORY) {
            message = L"not enough memory to load file";
        } else if (initResult == WATCHER_LOAD_READ_FILE) {
            message = L"could not read file";
        } else if (initResult == WATCHER_LOAD_CLOSE_FILE) {
            message = L"could not close file";
        }

        MessageBox(NULL, message, L"file watcher", MB_OK);
        free(watcherData.content);
        return 1;
    }

    WCHAR caption[100];
    int captionLength = MultiByteToWideChar(
        CP_UTF8, MB_PRECOMPOSED, (LPCSTR)watcherData.content, contentSize, caption, 99
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

    WatcherThreadError stopResult = stopWatcher(&watcherData);
    if (stopResult != WATCHER_THREAD_SUCCESS) {
        MessageBox(NULL, L"did not exit cleanly", L"file watcher", MB_OK);
    }

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
        WatcherStartError result = startWatcher(&watcherData, window);
        if (result != WATCHER_START_SUCCESS) {
            LPCWSTR message = L"missing message for error";
            if (result == WATCHER_START_CREATE_EVENT) {
                message = L"could not create event";
            } else if (result == WATCHER_START_CREATE_THREAD) {
                message = L"could not create thread";
            }

            MessageBox(NULL, message, L"file watcher", MB_OK);
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

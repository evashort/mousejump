#ifndef FILE_WATCHER_H
#define FILE_WATCHER_H

#define UNICODE
#include <windows.h>

// https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw
// "ReadDirectoryChangesW fails with ERROR_NOACCESS when the buffer is not
// aligned on a DWORD boundary."
// https://docs.microsoft.com/en-us/cpp/cpp/align-cpp?view=msvc-160
typedef __declspec(align(4)) struct {
    byte changes[2048];
    HANDLE thread;
    HANDLE exitEvent;
    HWND window;
    HANDLE folder;
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
    HANDLE *file;
    LPBYTE *buffer;
    OVERLAPPED *io;
} LoadRequest;

typedef struct {
    LPBYTE buffer;
    int size;
    HANDLE event;
} ParseRequest;

typedef enum {
    WATCHER_SUCCESS = 0,
    WATCHER_OPEN_FOLDER,
    WATCHER_CREATE_EVENT,
    WATCHER_WATCH,
    WATCHER_CREATE_THREAD,
    WATCHER_GET_CHANGES,
    WATCHER_OPEN_FILE,
    WATCHER_FILE_SIZE,
    WATCHER_TOO_BIG,
    WATCHER_NO_MEMORY,
    WATCHER_READ_FILE,
    WATCHER_RESULT_OF_READ,
    WATCHER_CLOSE_FILE,
    WATCHER_RESET_EVENT,
    WATCHER_SEND_MESSAGE,
    WATCHER_BAD_STATE,
    WATCHER_THREAD_TIMEOUT,
    WATCHER_CLOSE_FOLDER,
    WATCHER_CLOSE_EVENT,
    WATCHER_CLOSE_THREAD,
} WatcherError;

LPCWSTR watcherVerbs[] = {
   L"fail",
   L"open folder to watch for changes",
   L"create an event object",
   L"watch for file changes",
   L"create file watcher thread",
   L"retrieve file changes",
   L"open file",
   L"get file size",
   L"fit file in 2.15 GB",
   L"fit file in available memory",
   L"read file",
   L"get result of file read",
   L"close file",
   L"reset event object",
   L"send message to main thread",
   L"recover from bad state",
   L"stop file watcher thread in time",
   L"close folder",
   L"clean up event object",
   L"clean up file watcher thread",
};
#define WATCHER_VERB_LENGTH 33

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
            return WATCHER_SUCCESS;
        } else if (waitResult == WAIT_OBJECT_0 + CHANGE_EVENT) {
            DWORD bufferLength;
            BOOL overlappedResult = GetOverlappedResult(
                data->folder, &data->watchIO, &bufferLength, FALSE
            );
            if (!overlappedResult) {
                return WATCHER_GET_CHANGES;
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
                change = (FILE_NOTIFY_INFORMATION*)(
                    (LPBYTE)change + change->NextEntryOffset
                );
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
                return WATCHER_WATCH;
            }

            shouldRead = match && state == WATCH_STATE && !fileChanged;
            fileChanged = fileChanged || match;
        } else if (
            state == WATCH_STATE && fileChanged && waitResult == WAIT_TIMEOUT
        ) {
            shouldRead = TRUE;
        } else if (
            state == LOAD_STATE && waitResult == WAIT_OBJECT_0 + LOADED_EVENT
                && data->file != INVALID_HANDLE_VALUE
        ) {
            state = PARSE_STATE;
            DWORD contentSize;
            BOOL overlappedResult = GetOverlappedResult(
                data->file, &data->loadIO, &contentSize, FALSE
            );
            if (!overlappedResult) {
                CloseHandle(data->file);
                data->file = INVALID_HANDLE_VALUE;
                return WATCHER_RESULT_OF_READ;
            }

            if (!CloseHandle(data->file)) {
                data->file = INVALID_HANDLE_VALUE;
                return WATCHER_CLOSE_FILE;
            }

            data->file = INVALID_HANDLE_VALUE;
            if (!ResetEvent(data->loadIO.hEvent)) {
                return WATCHER_RESET_EVENT;
            }

            static ParseRequest request;
            request.buffer = data->content;
            request.size = contentSize;
            request.event = data->loadIO.hEvent;
            BOOL result = PostMessage(
                data->window, data->loadedMessage, 0, (LPARAM)&request
            );
            if (!result) {
                return WATCHER_SEND_MESSAGE;
            }
        } else if (
            waitResult == WAIT_OBJECT_0 + LOADED_EVENT && (
                state == PARSE_STATE || (
                    state == LOAD_STATE && data->file == INVALID_HANDLE_VALUE
                )
            )
        ) {
            state = WATCH_STATE;
            shouldRead = fileChanged;
            if (!ResetEvent(data->loadIO.hEvent)) {
                return WATCHER_RESET_EVENT;
            }
        } else {
            return WATCHER_BAD_STATE;
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
                if (
                    GetLastError() == ERROR_FILE_NOT_FOUND
                        || GetLastError() == ERROR_PATH_NOT_FOUND
                ) {
                    static ParseRequest request;
                    request.buffer = NULL;
                    request.size = 0;
                    request.event = INVALID_HANDLE_VALUE;
                    BOOL result = PostMessage(
                        data->window, data->loadedMessage, 0, (LPARAM)&request
                    );
                }
            } else {
                state = LOAD_STATE;
                fileChanged = FALSE;
                static LoadRequest request;
                request.file = &data->file;
                request.buffer = &data->content;
                request.io = &data->loadIO;
                BOOL result = PostMessage(
                    data->window, data->changeMessage, 0, (LPARAM)&request
                );
                if (!result) {
                    return WATCHER_SEND_MESSAGE;
                }
            }
        }
    }
}

WatcherError initializeWatcher(
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
    int i = data->pathLength - 1;
    WCHAR oldChar = L'\0';
    for (; i >= 0; i--) {
        if (path[i] == L'\\' || path[i] == L'/') {
            oldChar = path[i];
            path[i] = L'\0';
            break;
        }
    }

    data->folder = CreateFile(
        i >= 0 ? path : L".",
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        // FILE_FLAG_OVERLAPPED makes ReadDirectoryChangesW asynchronous
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );
    if (i >= 0) { path[i] = oldChar; }
    if (data->folder == INVALID_HANDLE_VALUE) {
        return WATCHER_OPEN_FOLDER;
    }

    data->watchIO.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (data->watchIO.hEvent == NULL) {
        data->watchIO.hEvent = INVALID_HANDLE_VALUE;
        CloseHandle(data->folder);
        data->folder = INVALID_HANDLE_VALUE;
        return WATCHER_CREATE_EVENT;
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
        return WATCHER_WATCH;
    }

    return WATCHER_SUCCESS;
}

WatcherError watcherReadFile(LPCWSTR path, LPBYTE *buffer, DWORD *size) {
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
        return WATCHER_OPEN_FILE;
    }

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(file, &fileSize)) {
        CloseHandle(file); return WATCHER_FILE_SIZE;
    } else if (fileSize.HighPart != 0 || fileSize.LowPart > (DWORD)INT_MAX) {
        CloseHandle(file); return WATCHER_TOO_BIG;
    }

    *buffer = (LPBYTE)realloc(*buffer, fileSize.LowPart);
    if (fileSize.LowPart > 0 && *buffer == NULL) {
        CloseHandle(file); return WATCHER_NO_MEMORY;
    }

    BOOL readResult = ReadFile(
        file, *buffer, fileSize.LowPart, size, NULL
    );
    if (!readResult) {
        CloseHandle(file); return WATCHER_READ_FILE;
    }

    if (!CloseHandle(file)) {
        return WATCHER_CLOSE_FILE;
    }

    return WATCHER_SUCCESS;
}

WatcherError startWatcher(WatcherData *data, HWND window) {
    data->window = window;
    data->exitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (data->exitEvent == NULL) {
        data->exitEvent = INVALID_HANDLE_VALUE;
        return WATCHER_CREATE_EVENT;
    }

    data->loadIO.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (data->loadIO.hEvent == NULL) {
        data->loadIO.hEvent = INVALID_HANDLE_VALUE;
        return WATCHER_CREATE_EVENT;
    }

    DWORD threadID;
    data->thread = CreateThread(
        NULL, 0, fileWatcherThread, data, 0, &threadID
    );
    if (data->thread == NULL) {
        data->thread = INVALID_HANDLE_VALUE;
        return WATCHER_CREATE_THREAD;
    }

    return WATCHER_SUCCESS;
}

WatcherError readWatchedFile(LoadRequest *request) {
    WatcherError error = WATCHER_BAD_STATE;
    while (TRUE) {
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(*request->file, &fileSize)) {
            error = WATCHER_FILE_SIZE; break;
        } else if (
            fileSize.HighPart != 0 || fileSize.LowPart > (DWORD)INT_MAX
        ) {
            error = WATCHER_TOO_BIG; break;
        }

        *request->buffer = (LPBYTE)realloc(
            *request->buffer, fileSize.LowPart
        );
        if (fileSize.LowPart > 0 && *request->buffer == NULL) {
            error = WATCHER_NO_MEMORY; break;
        }

        request->io->Offset = request->io->OffsetHigh = 0;
        BOOL result = ReadFile(
            *request->file,
            *request->buffer,
            fileSize.LowPart,
            NULL,
            request->io
        );
        if (!result && GetLastError() != ERROR_IO_PENDING) {
            error = WATCHER_READ_FILE; break;
        }

        error = WATCHER_SUCCESS; break;
    }

    if (error != WATCHER_SUCCESS) {
        CloseHandle(*request->file);
        *request->file = INVALID_HANDLE_VALUE;
        SetEvent(request->io->hEvent);
    }

    return error;
}

WatcherError stopWatcher(WatcherData *data) {
    WatcherError error = WATCHER_SUCCESS;
    BOOL closeHandleFailed = FALSE;
    if (data->thread != INVALID_HANDLE_VALUE) {
        SetEvent(data->exitEvent);
        if (WaitForSingleObject(data->thread, 2000) != WAIT_OBJECT_0) {
            error = WATCHER_THREAD_TIMEOUT;
        } else {
            GetExitCodeThread(data->thread, (LPDWORD)&error);
        }

        if (!CloseHandle(data->thread) && error == WATCHER_SUCCESS) {
            error = WATCHER_CLOSE_THREAD;
        }
    }

    if (
        data->folder != INVALID_HANDLE_VALUE
            && !CloseHandle(data->folder) && error == WATCHER_SUCCESS
    ) { error = WATCHER_CLOSE_FOLDER; }
    free(data->content);
    if (
        data->exitEvent != INVALID_HANDLE_VALUE
            && !CloseHandle(data->exitEvent) && error == WATCHER_SUCCESS
    ) { error = WATCHER_CLOSE_EVENT; }
    if (
        data->watchIO.hEvent != INVALID_HANDLE_VALUE
            && !CloseHandle(data->watchIO.hEvent) && error == WATCHER_SUCCESS
    ) { error = WATCHER_CLOSE_EVENT; }
    if (
        data->file != INVALID_HANDLE_VALUE
            && !CloseHandle(data->file) && error == WATCHER_SUCCESS
    ) { error = WATCHER_CLOSE_FILE; }
    if (
        data->loadIO.hEvent != INVALID_HANDLE_VALUE
            && !CloseHandle(data->loadIO.hEvent) && error == WATCHER_SUCCESS
    ) { error = WATCHER_CLOSE_EVENT; }
    return error;
}

#endif

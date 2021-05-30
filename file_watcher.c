#define UNICODE
#include <windows.h>
#include <stdio.h>
#include "./file_watcher.h"

#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")

LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM, LPARAM);
WatcherData watcherData;
const WCHAR watcherErrorFormat[] = L"File watcher error: Could not %s";
WCHAR watcherErrorString[
    sizeof(watcherErrorFormat) / sizeof(WCHAR) - 2 + WATCHER_VERB_LENGTH - 1
];
const int watcherErrorLength = sizeof(watcherErrorString) / sizeof(WCHAR);
int CALLBACK WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine,
                     int nCmdShow) {
    WatcherError initResult = initializeWatcher(
        &watcherData, L"watch_folder/settings.json", WM_APP, WM_APP + 1
    );
    if (
        initResult != WATCHER_SUCCESS && initResult != WATCHER_OPEN_FOLDER
    ) {
        _snwprintf_s(
            watcherErrorString, watcherErrorLength, _TRUNCATE,
            watcherErrorFormat, watcherVerbs[initResult]
        );
        MessageBox(NULL, watcherErrorString, L"file watcher", MB_OK);
        return 1;
    }

    DWORD contentSize;
    WatcherError loadResult = watcherReadFile(
        watcherData.path, &watcherData.content, &contentSize
    );
    if (
        loadResult != WATCHER_SUCCESS && (
            loadResult != WATCHER_OPEN_FILE || (
                GetLastError() != ERROR_FILE_NOT_FOUND
                    && GetLastError() != ERROR_PATH_NOT_FOUND
            )
        )
    ) {
        _snwprintf_s(
            watcherErrorString, watcherErrorLength, _TRUNCATE,
            watcherErrorFormat, watcherVerbs[loadResult]
        );
        MessageBox(NULL, watcherErrorString, L"file watcher", MB_OK);
        return 1;
    }

    WCHAR caption[100] = L"default";
    if (watcherData.content != NULL) {
        int captionLength = MultiByteToWideChar(
            CP_UTF8, MB_PRECOMPOSED, (LPCSTR)watcherData.content, contentSize,
            caption, 99
        );
        caption[captionLength] = L'\0';
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

    WatcherError stopResult = stopWatcher(&watcherData);
    if (stopResult != WATCHER_SUCCESS) {
        _snwprintf_s(
            watcherErrorString, watcherErrorLength, _TRUNCATE,
            watcherErrorFormat, watcherVerbs[stopResult]
        );
        MessageBox(NULL, watcherErrorString, L"file watcher", MB_OK);
        return 1;
    }

    return 0;
}

LRESULT CALLBACK WndProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CREATE) {
        if (watcherData.folder == INVALID_HANDLE_VALUE) { return 0; }
        WatcherError result = startWatcher(&watcherData, window);
        if (result != WATCHER_SUCCESS) {
            _snwprintf_s(
                watcherErrorString, watcherErrorLength, _TRUNCATE,
                watcherErrorFormat, watcherVerbs[result]
            );
            MessageBox(NULL, watcherErrorString, L"file watcher", MB_OK);
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
        readWatchedFile((LoadRequest*)lParam);
        return 0;
    } else if (message == WM_APP + 1) {
        ParseRequest *request = (ParseRequest*)lParam;
        WCHAR caption[100] = L"default";
        if (request->event != INVALID_HANDLE_VALUE) {
            int captionLength = MultiByteToWideChar(
                CP_UTF8, MB_PRECOMPOSED,
                (LPCSTR)request->buffer, request->size, caption, 99
            );
            caption[captionLength] = L'\0';
            SetEvent(request->event);
        }

        SetWindowText(window, caption);
        return 0;
    } else if (message == WM_DESTROY) {
        PostQuitMessage(0); // exit when window is closed
        return 0;
    } else // use default behavior for all other messages
        return DefWindowProc(window, message, wParam, lParam);
}

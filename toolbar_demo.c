// https://docs.microsoft.com/en-us/windows/win32/controls/common-control-versions
#define _WIN32_IE 0x0600

#define UNICODE
#include <ShellScalingApi.h>
#include <commctrl.h>
#include <windows.h>
#include <windowsx.h>

#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "SHCore")
#pragma comment(lib, "comctl32")

// https://docs.microsoft.com/en-us/windows/win32/controls/cookbook-overview
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

typedef struct {
    HWND window;
    HWND dialog;
    HWND toolbar;
} Model;
Model model;

LRESULT CALLBACK WndProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam
) {
    if (message == WM_ACTIVATE) {
        if (LOWORD(wParam) == WA_ACTIVE || LOWORD(wParam) == WA_CLICKACTIVE) {
            if (IsWindowVisible(model.dialog)) {
                SetActiveWindow(model.dialog);
            }
        }

        return 0;
    } else if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    } else {
        return DefWindowProc(window, message, wParam, lParam);
    }
}

HMENU dropdownMenuOut = NULL;
HMENU getDropdownMenu() {
    if (dropdownMenuOut) { return dropdownMenuOut; }
    return dropdownMenuOut = LoadMenu(GetModuleHandle(NULL), L"INITIAL_MENU");
}

UINT systemFontIn;
HFONT systemFontOut = NULL;
HFONT getSystemFont(UINT dpi) {
    if (systemFontOut) {
        if (dpi == systemFontIn) { return systemFontOut; }
        DeleteObject(systemFontOut);
    }

    systemFontIn = dpi;
    NONCLIENTMETRICS metrics;
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoForDpi(
        SPI_GETNONCLIENTMETRICS,
        sizeof(metrics),
        &metrics,
        0,
        dpi
    );
    return systemFontOut = CreateFontIndirect(&metrics.lfMessageFont);
}

BOOL CALLBACK SetFontRedraw(HWND child, LPARAM font){
    SendMessage(child, WM_SETFONT, font, TRUE);
    return TRUE;
}

LRESULT CALLBACK DlgProc(
    HWND dialog, UINT message, WPARAM wParam, LPARAM lParam
) {
    if (message == WM_INITDIALOG) {
        // https://docs.microsoft.com/en-us/windows/win32/controls/create-toolbars
        model.toolbar = CreateWindowEx(
            0,
            TOOLBARCLASSNAME,
            NULL,
            WS_CHILD | WS_TABSTOP | TBSTYLE_LIST,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            dialog,
            NULL,
            GetWindowInstance(dialog),
            NULL
        );
        SendMessage(
            model.toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS
        );
        TBBUTTON button = {
            .iBitmap = 0,
            .idCommand = 1234,
            .fsState = TBSTATE_ENABLED,
            .fsStyle = BTNS_WHOLEDROPDOWN,
            .bReserved = { 0 },
            .dwData = 0,
            .iString = -1,
        };
        SendMessage(model.toolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(0, 0));
        SendMessage(model.toolbar, TB_BUTTONSTRUCTSIZE, sizeof(button), 0);
        SendMessage(model.toolbar, TB_ADDBUTTONS, 1, (LPARAM)&button);
        SendMessage(model.toolbar, TB_SETPADDING, 0, MAKELPARAM(0, 23));
        // this can be any arbitrary number
        SendMessage(model.toolbar, TB_SETBUTTONWIDTH, 0, MAKELPARAM(10, 10));
        RECT buttonRect;
        SendMessage(model.toolbar, TB_GETRECT, 1234, (LPARAM)&buttonRect);
        RECT client;
        GetClientRect(dialog, &client);
        RECT toolbarFrame;
        GetWindowRect(model.toolbar, &toolbarFrame);
        ScreenToClient(dialog, (LPPOINT)&toolbarFrame.left);
        ScreenToClient(dialog, (LPPOINT)&toolbarFrame.right);
        POINT offset = { buttonRect.right, buttonRect.top };
        MapWindowPoints(model.toolbar, dialog, &offset, 1);
        // this can be any arbitrary number
        SendMessage(model.toolbar, TB_SETBUTTONWIDTH, 0, MAKELPARAM(10, 10));
        SetWindowPos(
            model.toolbar,
            NULL,
            client.right - offset.x - toolbarFrame.left,
            30 - offset.y - toolbarFrame.top,
            0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
        );
        ShowWindow(model.toolbar,  TRUE);
        UINT dpi = GetDpiForWindow(dialog);
        // EnumChildWindows(dialog, SetFontRedraw, (WPARAM)getSystemFont(dpi));
        return TRUE;
    } else if (message == WM_ACTIVATE) {
        // make MouseJump topmost only when the dialog is active
        if (LOWORD(wParam) == WA_ACTIVE || LOWORD(wParam) == WA_CLICKACTIVE) {
            // if the dialog is re-created while MouseJump is not in the
            // foreground, the dialog recieves WM_ACTIVATE without actually
            // being activated. we don't ever want MouseJump to be topmost
            // when it's not active, hence the GetForegroundWindow check.
            if (GetForegroundWindow() == dialog) {
                SetWindowPos(
                    GetWindowOwner(dialog), HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
                );
            }
        } else if (LOWORD(wParam) == WA_INACTIVE) {
            HWND window = GetWindowOwner(dialog);
            SetWindowPos(
                window, HWND_NOTOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
            );
            HWND newlyActive = GetForegroundWindow();
            if (
                newlyActive && !(
                    GetWindowLong(newlyActive, GWL_EXSTYLE) & WS_EX_TOPMOST
                )
            ) {
                HWND insertAfter = GetWindow(newlyActive, GW_HWNDNEXT);
                if (insertAfter) {
                    SetWindowPos(
                        window, insertAfter, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
                    );
                }
            }
        }

        return TRUE;
    }  else if (
        message == WM_NOTIFY && ((LPNMHDR)lParam)->code == TBN_DROPDOWN
    ) {
        // https://docs.microsoft.com/en-us/windows/win32/controls/handle-drop-down-buttons
        LPNMTOOLBAR toolbarMessage = (LPNMTOOLBAR)lParam;
        RECT buttonRect;
        SendMessage(
            toolbarMessage->hdr.hwndFrom,
            TB_GETRECT,
            (WPARAM)toolbarMessage->iItem,
            (LPARAM)&buttonRect
        );
        MapWindowPoints(
            toolbarMessage->hdr.hwndFrom, HWND_DESKTOP,
            (LPPOINT)&buttonRect, 2
        );
        buttonRect.bottom--; // unexplained off-by-one
        TPMPARAMS popupParams;
        popupParams.cbSize = sizeof(popupParams);
        popupParams.rcExclude = buttonRect;
        TrackPopupMenuEx(
            GetSubMenu(getDropdownMenu(), 0),
            TPM_VERTICAL | TPM_LEFTBUTTON | TPM_RIGHTALIGN,
            buttonRect.right, buttonRect.top,
            dialog,
            &popupParams
        );
        return TRUE;
    }else if (message == WM_CLOSE) {
        PostQuitMessage(0);
        return TRUE;
    } else {
        return FALSE;
    }
}

int CALLBACK WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow
) {
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    WNDCLASS windowClass = {
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = WndProc,
        .hInstance = hInstance,
        .hCursor = LoadCursor(0, IDC_ARROW),
        .hbrBackground = GetStockObject(WHITE_BRUSH),
        .lpszClassName = L"windowClass",
    };
    RegisterClass(&windowClass);
    model.window = CreateWindow(
        L"windowClass",
        L"main window",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL,
        NULL,
        hInstance,
        NULL
    );
    model.dialog = CreateDialog(hInstance, L"TOOL", model.window, DlgProc);
    ShowWindow(model.window, SW_SHOWNORMAL);
    MSG message;
    while (GetMessage(&message, NULL, 0, 0)) {
        // https://devblogs.microsoft.com/oldnewthing/20120416-00/?p=7853
        if (!IsDialogMessage(model.dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
    }

    if (systemFontOut != NULL) { DeleteFont(systemFontOut); }
    if (dropdownMenuOut) { DestroyMenu(dropdownMenuOut); }
    return 0;
}

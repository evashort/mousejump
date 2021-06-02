// https://docs.microsoft.com/en-us/windows/win32/controls/common-control-versions
#define _WIN32_IE 0x0600

#define UNICODE
#include <math.h>
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
    double textBoxWidthPt;
    double textBoxHeightPt;
} Model;
Model model = {
    .textBoxWidthPt = 23.25,
    .textBoxHeightPt = 17.25,
};

int ptToIntPx(double pt, UINT dpi) {
    return (int)round(pt * (double)dpi / 72);
}

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
        SendMessage(dialog, WM_DPICHANGED, 0, 0);
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
    } else if (message == WM_SIZE) {
        RECT client;
        GetClientRect(dialog, &client);
        HWND toolbar = GetDlgItem(dialog, 12345);
        RECT buttonRect;
        SendMessage(toolbar, TB_GETRECT, 1234, (LPARAM)&buttonRect);
        HWND textBox = GetDlgItem(dialog, 123456);
        RECT textBoxRect;
        GetWindowRect(textBox, &textBoxRect);
        int textBoxHeight = textBoxRect.bottom - textBoxRect.top;
        int controlTop = (client.bottom - textBoxHeight) / 2;
        SetWindowPos(
            textBox,
            NULL,
            0, controlTop,
            client.right - buttonRect.right + buttonRect.left, textBoxHeight,
            SWP_NOZORDER | SWP_NOACTIVATE
        );
        RECT toolbarFrame;
        GetWindowRect(toolbar, &toolbarFrame);
        ScreenToClient(dialog, (LPPOINT)&toolbarFrame.left);
        ScreenToClient(dialog, (LPPOINT)&toolbarFrame.right);
        POINT offset = { buttonRect.right, buttonRect.top };
        MapWindowPoints(toolbar, dialog, &offset, 1);
        SetWindowPos(
            toolbar,
            NULL,
            toolbarFrame.left + client.right - offset.x,
            toolbarFrame.top + controlTop - offset.y,
            0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
        );
        return 0;
    } else if (message == WM_GETMINMAXINFO) {
        HWND toolbar = GetDlgItem(dialog, 12345);
        UINT dpi = GetDpiForWindow(dialog);
        int textBoxWidthPx = ptToIntPx(model.textBoxWidthPt, dpi);
        RECT buttonRect;
        SendMessage(toolbar, TB_GETRECT, 1234, (LPARAM)&buttonRect);
        RECT textBoxRect;
        GetWindowRect(GetDlgItem(dialog, 123456), &textBoxRect);
        SIZE minSize = {
            .cx = textBoxWidthPx + buttonRect.right - buttonRect.left,
            .cy = textBoxRect.bottom - textBoxRect.top,
        };
        LPMINMAXINFO minMaxInfo = (LPMINMAXINFO)lParam;
        RECT client, frame;
        GetClientRect(dialog, &client); GetWindowRect(dialog, &frame);
        minMaxInfo->ptMinTrackSize.x
            = frame.right + minSize.cx - client.right - frame.left;
        minMaxInfo->ptMinTrackSize.y = minSize.cy
            = frame.bottom + minSize.cy - client.bottom - frame.top;
        return 0;
    } else if (message == WM_DPICHANGED) {
        HWND toolbar = GetDlgItem(dialog, 12345);
        if (toolbar != NULL) { DestroyWindow(toolbar); }
        // https://docs.microsoft.com/en-us/windows/win32/controls/create-toolbars
        toolbar = CreateWindow(
            TOOLBARCLASSNAME,
            NULL,
            WS_VISIBLE | WS_CHILD | WS_TABSTOP | TBSTYLE_LIST,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            dialog,
            (HMENU)12345,
            GetWindowInstance(dialog),
            0
        );
        SendMessage(toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS);
        SendMessage(toolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(0, 0));
        TBBUTTON button = {
            .iBitmap = 0,
            .idCommand = 1234,
            .fsState = TBSTATE_ENABLED,
            .fsStyle = BTNS_WHOLEDROPDOWN | BTNS_AUTOSIZE,
            .bReserved = { 0 },
            .dwData = 0,
            .iString = -1,
        };
        SendMessage(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(button), 0);
        SendMessage(toolbar, TB_ADDBUTTONS, 1, (LPARAM)&button);
        UINT dpi = GetDpiForWindow(dialog);
        int textBoxHeightPx = ptToIntPx(model.textBoxHeightPt, dpi);
        SendMessage(
            toolbar, TB_SETPADDING, 0, MAKELPARAM(1, textBoxHeightPx)
        );
        // this can be any arbitrary number
        SendMessage(toolbar, TB_SETBUTTONWIDTH, 0, MAKELPARAM(10, 10));
        SendMessage(toolbar, TB_AUTOSIZE, 0, 0);
        HWND textBox = GetDlgItem(dialog, 123456);
        RECT textBoxRect;
        GetWindowRect(textBox, &textBoxRect);
        SetWindowPos(
            textBox,
            NULL,
            0, 0,
            textBoxRect.right - textBoxRect.left, textBoxHeightPx,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
        );
        //EnumChildWindows(dialog, SetFontRedraw, (WPARAM)getSystemFont(dpi));
        RECT frame;
        GetWindowRect(dialog, &frame);
        SetWindowPos(
            dialog, NULL, 0, 0,
            frame.right - frame.left, frame.bottom - frame.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
        );
        return 0;
    } else if (
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

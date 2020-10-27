// rc mousejump.rc && cl mousejump.c /link mousejump.res && mousejump.exe

// https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-loadbitmapa#remarks
#define OEMRESOURCE

#define UNICODE
#include <math.h>
#include "mousejump.h"
#include <stdlib.h>
#include <ShellScalingApi.h>
#include <windows.h>

// GET_X_LPARAM, GET_Y_LPARAM
#include <windowsx.h>

#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "SHCore")

#pragma region debug
int debugCount = 0;
void debugIncrement(HWND window) {
    debugCount += 1;
    wchar_t x[25];
    SetWindowText(window, _itow(debugCount, x, 10));
}

int debugCount2 = 0;
void debugIncrement2(HWND window) {
    debugCount2 += 1;
    wchar_t x[25];
    SetWindowText(window, _itow(debugCount2, x, 10));
}

void setNumber(HWND window, int number) {
    wchar_t x[25];
    SetWindowText(window, _itow(number, x, 10));
}

void showNumber(int number, LPCSTR caption) {
    char x[25];
    MessageBoxA(NULL, _itoa(number, x, 10), caption, MB_OK);
}

void showHex(int number, LPCSTR caption) {
    char x[25];
    MessageBoxA(NULL, _itoa(number, x, 16), caption, MB_OK);
}

LPCWSTR str(int number) {
    static WCHAR string[25];
    _itow(number, string, 10);
    return string;
}

// for (int i = 0; i < count - 1; i++) {
//     int j = i + rand() % (count - i);
//     int tmp = shuffledIndicesOut[i];
//     shuffledIndicesOut[i] = shuffledIndicesOut[j];
//     shuffledIndicesOut[j] = tmp;
// }

#pragma endregion

#pragma region keyBitmap

COLORREF keyBrushIn;
HBRUSH keyBrushOut = NULL;
HBRUSH getKeyBrush(COLORREF color) {
    if (keyBrushOut) {
        if (color == keyBrushIn) { return keyBrushOut; }
        DeleteObject(keyBrushOut);
    }

    keyBrushIn = color;
    return keyBrushOut = CreateSolidBrush(color);
}

typedef struct { int width; int height; COLORREF color; } KeyBitmapIn;
KeyBitmapIn keyBitmapIn = { .width = 0, .height = 0 };
HBITMAP keyBitmapOut = NULL;
void selectKeyBitmap(
    HDC device, HDC memory, int width, int height, COLORREF color
) {
    // please clear the bitmap when you are done with it!
    KeyBitmapIn in = {
        max(width, keyBitmapIn.width),
        max(height, keyBitmapIn.height),
        color
    };
    if (keyBitmapOut) {
        if (!memcmp(&in, &keyBitmapIn, sizeof(in))) {
            SelectObject(memory, keyBitmapOut);
            return;
        }

        DeleteObject(keyBitmapOut);
        keyBitmapIn.width = width;
        keyBitmapIn.height = height;
    }

    keyBitmapIn = in;
    // https://docs.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createcompatiblebitmap#remarks
    keyBitmapOut = CreateCompatibleBitmap(device, width, height);
    SelectObject(memory, keyBitmapOut);
    RECT rect = { 0, 0, in.width, in.height };
    FillRect(memory, &rect, getKeyBrush(color));
}

#pragma endregion

#pragma region getLabels

LPWSTR labelTextOut = NULL;
LPWSTR getLabelText() {
    if (labelTextOut) { return labelTextOut; }
    HRSRC resource = FindResource(NULL, L"LABELS", L"CSV");
    DWORD utf8Size = SizeofResource(NULL, resource);
    HGLOBAL utf8Handle = LoadResource(NULL, resource);
    LPCCH utf8 = (LPCCH)LockResource(utf8Handle);
    int textLength = MultiByteToWideChar(CP_UTF8, 0, utf8, utf8Size, NULL, 0);
    LPWSTR labelTextOut = (LPWSTR)malloc((textLength + 1) * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, utf8, utf8Size, labelTextOut, textLength);
    FreeResource(utf8Handle);
    labelTextOut[textLength] = L'\0';
    return labelTextOut;
}

typedef struct {
    int columns;
    int rows;
    LPWSTR *cells; // column-first order
} CSV;
CSV labelCSVOut = { .cells = NULL };
CSV getLabelCSV() {
    if (labelCSVOut.cells) { return labelCSVOut; }
    LPWSTR text = getLabelText();
    CSV csv = { .columns = 0, .rows = 0, .cells = NULL };
    for (LPWSTR i = text; *i != L'\0'; csv.rows++) {
        int columns = 0;
        for (WCHAR terminator = L','; terminator == L','; columns++) {
            for (
                int quotes = 0;
                *i != L'\0' && (
                    quotes % 2 || (*i != L',' && *i != L'\r' && *i != L'\n')
                );
                i++
            ) {
                if (*i == L'"') { quotes++; }
            }

            terminator = *i;
            if (terminator != L'\0') { i++; }
            if (terminator == L'\r' && *i == L'\n') { i++; }
        }

        if (columns > csv.columns) { csv.columns = columns; }
    }

    csv.cells = (LPWSTR*)malloc(csv.columns * csv.rows * sizeof(LPWSTR*));
    ZeroMemory(csv.cells, csv.columns * csv.rows * sizeof(LPWSTR*));
    LPCWSTR *rowStart = csv.cells;
    for (LPWSTR i = text; *i != L'\0'; rowStart++) {
        LPCWSTR *cell = rowStart;
        for (WCHAR terminator = L','; terminator == L','; cell += csv.rows) {
            *cell = i;
            int quotes = 0;
            for (
                ;
                *i != L'\0' && (
                    quotes % 2 || (*i != L',' && *i != L'\r' && *i != L'\n')
                );
                i++
            ) {
                i[-((quotes + 1) / 2)] = *i;
                if (*i == L'"') { quotes++; }
            }

            terminator = *i;
            i[-(quotes / 2) - (quotes > 0)] = L'\0';
            if (terminator != L'\0') { i++; }
            if (terminator == L'\r' && *i == L'\n') { i++; }
        }
    }

    labelCSVOut = csv;
    return csv;
}

#pragma endregion
typedef struct { LPCWSTR *value; int count; } StringArray;
StringArray getLabels() {
    StringArray labels = { getLabelCSV().cells, getLabelCSV().rows };
    return labels;
}


#pragma region selectLabelBitmap

typedef struct {
    UINT dpi; double heightPt; WCHAR family[LF_FACESIZE];
} LabelFontIn;
LabelFontIn labelFontIn;
HFONT labelFontOut = NULL;
HFONT getLabelFont(UINT dpi, double heightPt, WCHAR family[LF_FACESIZE]) {
    LabelFontIn in = { .dpi = dpi, .heightPt = heightPt };
    wcsncpy(in.family, family, LF_FACESIZE);
    if (labelFontOut) {
        if (!memcmp(&in, &labelFontIn, sizeof(in))) { return labelFontOut; }
        DeleteObject(labelFontOut);
    }

    labelFontIn = in;
    NONCLIENTMETRICS metrics;
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoForDpi(
        SPI_GETNONCLIENTMETRICS,
        sizeof(metrics),
        &metrics,
        0,
        dpi
    );
    if (heightPt > 0) {
        metrics.lfMessageFont.lfHeight = max(1, ptToIntPx(heightPt, dpi));
    }

    if (wcsncmp(family, L"", LF_FACESIZE)) {
        wcsncpy(metrics.lfMessageFont.lfFaceName, family, LF_FACESIZE);
    }

    return labelFontOut = CreateFontIndirect(&metrics.lfMessageFont);
}

typedef struct { LOGFONT font; int count; } LabelWidthsIn;
LabelWidthsIn labelWidthsIn = { .count = 0 };
int *labelWidthsOut = NULL;
int *getLabelWidths(HDC device, int count) {
    LabelWidthsIn in = { .count = max(count, labelWidthsIn.count) };
    GetObject(GetCurrentObject(device, OBJ_FONT), sizeof(in.font), &in.font);
    if (labelWidthsOut) {
        if (!memcmp(&in, &labelWidthsIn, sizeof(in))) {
            return labelWidthsOut;
        }

        free(labelWidthsOut);
        in.count = count;
    }

    labelWidthsIn = in;
    labelWidthsOut = malloc(count * sizeof(int));
    LPCWSTR *labels = getLabels().value;
    for (int i = 0; i < count; i++) {
        RECT rect = { 0, 0 };
        DrawText(
            device,
            labels[i],
            -1,
            &rect,
            DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX
        );
        labelWidthsOut[i] = rect.right;
    }

    return labelWidthsOut;
}

COLORREF labelBrushIn;
HBRUSH labelBrushOut = NULL;
HBRUSH getLabelBrush(COLORREF color) {
    if (labelBrushOut) {
        if (color == labelBrushIn) { return labelBrushOut; }
        DeleteObject(labelBrushOut);
    }

    labelBrushIn = color;
    return labelBrushOut = CreateSolidBrush(color);
}

const int DESCENDER = 1;
const int ASCENDER = 2;
const int ACCENT = 4;
// e = 0, g = 1, E = 2, j = 3
const int ASCII_FEATURES[] = { // https://www.ascii-code.com/
//  0,1,2,3,4,5,6,7,8,9,a,b,c,d,e,f
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, //                  0-15
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 0123456789abcdef 16-31
    0,2,2,2,3,2,2,2,3,3,2,2,1,0,0,2, //  !"#$%&'()*+,-./ 32-47
    2,2,2,2,2,2,2,2,2,2,2,3,2,2,2,2, // 0123456789:;<=>? 48-63
    3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // @ABCDEFGHIJKLMNO 64-79
    2,3,2,2,2,2,2,2,2,2,2,3,3,3,2,1, // PQRSTUVWXYZ[\]^_ 80-95
    2,0,2,0,2,0,2,1,2,2,3,2,2,0,0,0, // `abcdefghijklmno 96-111
    1,1,0,0,2,0,0,0,0,1,0,3,3,3,2    // pqrstuvwxyz{|}~  112-126
};
const int TOP_BAR = 8;
const int BOTTOM_BAR = 16;
const int MID_BAR = 32;
// I = 0, T = 8, L = 16, E = 24, r = 32, z = 48
const int ASCII_FEATURES_2[] = { // https://www.ascii-code.com/
//  0,1,2,3,4,5,6,7,8,9,a,b,c,d,e,f
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, //                  0-15
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 0123456789abcdef 16-31
    0, 0, 8, 8,24,24,24, 8, 8, 8, 8, 0, 0, 0,16, 8, //  !"#$%&'()*+,-./ 32-47
   24, 0,24,24, 8,24,24, 8,24,24, 0, 0, 0, 0, 0,24, // 0123456789:;<=>? 48-63
    8, 0,24, 0,24,24, 8,16, 0, 0, 0, 0,16, 0, 0, 0, // @ABCDEFGHIJKLMNO 64-79
    8, 0, 8,24, 8, 0, 0,16, 0, 0,24, 8, 8, 8, 8, 0, // PQRSTUVWXYZ[\]^_ 80-95
    8,48,16,48,16,48, 8,32, 0, 0, 0, 0, 0,48,32, 0, // `abcdefghijklmno 96-111
   32,32,32,48,16,16, 0,16, 0, 0,48, 8, 8, 8, 0     // pqrstuvwxyz{|}~ 112-126
};
int getStringFeatures(LPCWSTR string) {
    int result = 0;
    for (; *string != L'\0'; string++) {
        if (*string * sizeof(int) < sizeof(ASCII_FEATURES)) {
            result |= ASCII_FEATURES[*string] | ASCII_FEATURES_2[*string];
        } else {
            return DESCENDER | ASCENDER | ACCENT;
        }
    }

    return result;
}

int getTopCrop(int features, TEXTMETRIC metrics) {
    int crop = 0;
    if (!(features & ASCENDER)) {
        crop += metrics.tmDescent;
    }

    if (!(features & ACCENT)) {
        crop += metrics.tmInternalLeading;
        if (
            ((features & ASCENDER) && (features & TOP_BAR))
                || (!(features & ASCENDER) && (features & MID_BAR))
        ) {
            crop -= max(1, metrics.tmDescent / 2);
        }
    }

    return max(0, crop);
}

int getBottomCrop(int features, TEXTMETRIC metrics) {
    int crop = 0;
    if (!(features & DESCENDER)) {
        crop += metrics.tmDescent;
        if (features & BOTTOM_BAR) {
            crop -= max(1, metrics.tmDescent / 2);
        }
    }

    return max(0, crop);
}

typedef struct {
    int count;
    RECT paddingPx;
    LOGFONT font;
    COLORREF foreground;
    COLORREF background;
} LabelBitmapIn;
typedef struct { HBITMAP bitmap; RECT *rects; } LabelBitmapOut;
RECT *selectLabelBitmapHelp(
    HDC device, HDC memory, int count, RECT paddingPx,
    LabelBitmapIn *labelBitmapIn, LabelBitmapOut *labelBitmapOut
) {
    LabelBitmapIn in = {
        .count = max(count, labelBitmapIn->count),
        .paddingPx = paddingPx,
        .foreground = GetTextColor(memory),
        .background = GetBkColor(memory)
    };
    GetObject(GetCurrentObject(memory, OBJ_FONT), sizeof(in.font), &in.font);
    if (labelBitmapOut->bitmap && labelBitmapOut->rects) {
        if (!memcmp(&in, labelBitmapIn, sizeof(in))) {
            SelectObject(memory, labelBitmapOut->bitmap);
            return labelBitmapOut->rects;
        }

        DeleteObject(labelBitmapOut->bitmap);
        free(labelBitmapOut->rects);
        in.count = count;
    }

    *labelBitmapIn = in;
    if (count == 0) {
        labelBitmapOut->bitmap = NULL;
        return labelBitmapOut->rects = NULL;
    }

    int xPadding = max(paddingPx.left, paddingPx.right);
    int yPadding = max(paddingPx.top, paddingPx.bottom);

    int width = 500;
    int *labelWidths = getLabelWidths(memory, count);
    for (int i = 0; i < count; i++) {
        width = max(width, labelWidths[i] + 2 * xPadding);
    }

    TEXTMETRIC metrics;
    GetTextMetrics(memory, &metrics);
    int x = xPadding;
    int height = metrics.tmHeight + 2 * yPadding;
    for (int i = 0; i < count; i++) {
        x += labelWidths[i] + xPadding;
        if (x > width) {
            x = xPadding;
            height += metrics.tmHeight + yPadding;
            i--;
        }
    }

    labelBitmapOut->bitmap = CreateCompatibleBitmap(device, width, height);
    SelectObject(memory, labelBitmapOut->bitmap);

    if (yPadding > 0) {
        HBRUSH brush = getLabelBrush(GetBkColor(memory));
        for (int y = 0; y < height; y += metrics.tmHeight + yPadding) {
            RECT separator = { 0, y, width, y + yPadding };
            FillRect(memory, &separator, brush);
        }
    }

    if (xPadding > 0) {
        HBRUSH brush = getLabelBrush(GetBkColor(memory));
        int x = 0;
        int y = yPadding;
        RECT separator = { x, y, x + xPadding, y + metrics.tmHeight };
        FillRect(memory, &separator, brush);
        for (int i = 0; i < count; i++) {
            x += xPadding + labelWidths[i];
            if (x > width - xPadding) {
                x = 0;
                y += metrics.tmHeight + yPadding;
                i--;
            }

            RECT separator = { x, y, x + xPadding, y + metrics.tmHeight };
            FillRect(memory, &separator, brush);
        }
    }

    labelBitmapOut->rects = malloc(count * sizeof(RECT));
    LPCWSTR *labels = getLabels().value;
    x = xPadding;
    int y = yPadding;
    for (int i = 0; i < count; i++) {
        if (x + labelWidths[i] + xPadding > width) {
            x = xPadding;
            y += metrics.tmHeight + yPadding;
        }

        RECT rect = { x, y, x + labelWidths[i], y + metrics.tmHeight };
        DrawText(
            memory, labels[i], -1, &rect,
            DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP
        );

        int features = getStringFeatures(labels[i]);
        rect.left -= paddingPx.left;
        rect.top -= paddingPx.top - getTopCrop(features, metrics);;
        rect.right += paddingPx.right;
        rect.bottom += paddingPx.bottom - getBottomCrop(features, metrics);
        labelBitmapOut->rects[i] = rect;

        x += labelWidths[i] + xPadding;
    }

    return labelBitmapOut->rects;
}

#pragma endregion
LabelBitmapIn labelBitmapIn = { .count = 0 };
LabelBitmapOut labelBitmapOut = { .bitmap = NULL, .rects = NULL};
RECT *selectLabelBitmap(HDC device, HDC memory, int count, RECT paddingPx) {
    return selectLabelBitmapHelp(
        device, memory, count, paddingPx, &labelBitmapIn, &labelBitmapOut
    );
}

LabelBitmapIn selectionBitmapIn = { .count = 0 };
LabelBitmapOut selectionBitmapOut = { .bitmap = NULL, .rects = NULL};
RECT *selectSelectionBitmap(HDC device, HDC memory, int count) {
    RECT paddingPx = { 0, 0, 0, 0 };
    return selectLabelBitmapHelp(
        device, memory, count, paddingPx,
        &selectionBitmapIn, &selectionBitmapOut
    );
}

#pragma region dialogHelpers

HACCEL acceleratorTableOut = NULL;
HACCEL getAcceleratorTable() {
    if (acceleratorTableOut) { return acceleratorTableOut; }
    return acceleratorTableOut = LoadAccelerators(
        GetModuleHandle(NULL),
        L"IDR_ACCEL1"
    );
}

typedef struct { ACCEL *value; int count; } AccelArray;
AccelArray acceleratorsOut = { .value = NULL };
AccelArray getAccelerators() {
    if (acceleratorsOut.value) { return acceleratorsOut; }
    acceleratorsOut.count = CopyAcceleratorTable(
        getAcceleratorTable(), NULL, 0
    );
    acceleratorsOut.value = malloc(acceleratorsOut.count * sizeof(ACCEL));
    acceleratorsOut.count = CopyAcceleratorTable(
        getAcceleratorTable(),
        acceleratorsOut.value,
        acceleratorsOut.count
    );
    return acceleratorsOut;
}

typedef struct { int left; int up; int right; int down; } ArrowKeyMap;
ArrowKeyMap getArrowKeyMap(int left, int up, int right, int down) {
    ArrowKeyMap keys = { 0, 0, 0, 0 };
    AccelArray accelerators = getAccelerators();
    for (int i = 0; i < accelerators.count; i++) {
        ACCEL accelerator = accelerators.value[i];
        if (accelerator.cmd == left) { keys.left = accelerator.key; }
        else if (accelerator.cmd == up) { keys.up = accelerator.key; }
        else if (accelerator.cmd == right) { keys.right = accelerator.key; }
        else if (accelerator.cmd == down) { keys.down = accelerator.key; }
    }

    return keys;
}

ArrowKeyMap normalArrowKeyMapOut = { 0, 0, 0, 0 };
ArrowKeyMap getNormalArrowKeyMap() {
    if (normalArrowKeyMapOut.left) { return normalArrowKeyMapOut; }
    return normalArrowKeyMapOut = getArrowKeyMap(
        IDM_LEFT, IDM_UP, IDM_RIGHT, IDM_DOWN
    );
}

ArrowKeyMap slowArrowKeyMapOut = { 0, 0, 0, 0 };
ArrowKeyMap getSlowArrowKeyMap() {
    if (slowArrowKeyMapOut.left) { return slowArrowKeyMapOut; }
    return slowArrowKeyMapOut = getArrowKeyMap(
        IDM_SLIGHTLY_LEFT, IDM_SLIGHTLY_UP,
        IDM_SLIGHTLY_RIGHT, IDM_SLIGHTLY_DOWN
    );
}

HBITMAP dropdownBitmapOut = NULL;
HBITMAP getDropdownBitmap() {
    // predefined bitmaps are scaled based on the program's initial DPI.
    // unfortunately, reloading them when the DPI changes does not change
    // their scale.
    if (dropdownBitmapOut) { return dropdownBitmapOut; }
    // http://www.bcbj.org/articles/vol1/9711/Bitmaps_on_demand.htm
    return dropdownBitmapOut = LoadBitmap(NULL, MAKEINTRESOURCE(OBM_COMBO));
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

int nextPowerOf2(int n) {
    int result = 1;
    while (result < n) { result <<= 1; }
    return result;
}

struct { LPWSTR text; int capacity; } textBoxTextOut = { .text = NULL };
LPWSTR getTextBoxText(HWND textBox) {
    int oldCapacity = textBoxTextOut.text ? textBoxTextOut.capacity : 0;\
    textBoxTextOut.capacity = nextPowerOf2(GetWindowTextLength(textBox) + 1);
    if (textBoxTextOut.capacity > oldCapacity) {
        if (textBoxTextOut.text) { free(textBoxTextOut.text); }
        textBoxTextOut.text = (LPWSTR)malloc(
            textBoxTextOut.capacity * sizeof(WCHAR)
        );
    }

    GetWindowText(textBox, textBoxTextOut.text, textBoxTextOut.capacity);
    return textBoxTextOut.text;
}

#pragma endregion

void destroyCache() {
    if (keyBrushOut) { DeleteObject(keyBrushOut); }
    if (keyBitmapOut) { DeleteObject(keyBitmapOut); }
    if (labelTextOut) { free(labelTextOut); }
    if (labelCSVOut.cells) { free(labelCSVOut.cells); }
    if (labelFontOut) { DeleteObject(labelFontOut); }
    if (labelWidthsOut) { free(labelWidthsOut); }
    if (labelBrushOut) { DeleteObject(labelBrushOut); }
    if (labelBitmapOut.bitmap) { DeleteObject(labelBitmapOut.bitmap); }
    if (labelBitmapOut.rects) { free(labelBitmapOut.rects); }
    if (selectionBitmapOut.bitmap) {
        DeleteObject(selectionBitmapOut.bitmap);
    }

    if (selectionBitmapOut.rects) { free(selectionBitmapOut.rects); }
    if (acceleratorsOut.value) { free(acceleratorsOut.value); }
    if (acceleratorTableOut) { DestroyAcceleratorTable(acceleratorTableOut); }
    if (dropdownBitmapOut) { DeleteObject(dropdownBitmapOut); }
    if (dropdownMenuOut) { DestroyMenu(dropdownMenuOut); }
    if (textBoxTextOut.text) { free(textBoxTextOut.text); }
}

typedef struct {
    HWND window;
    HWND dialog;
    COLORREF colorKey;
    double offsetXPt;
    double offsetYPt;
    double deltaPx;
    double smallDeltaPx;
    int bubbleCount;
    double marginLeftPt;
    double marginTopPt;
    double marginRightPt;
    double marginBottomPt;
    double aspect;
    double angle1;
    double angle2;
    double fontHeightPt;
    WCHAR fontFamily[LF_FACESIZE];
    double paddingLeftPt;
    double paddingTopPt;
    double paddingRightPt;
    double paddingBottomPt;
    COLORREF labelBackground;
    COLORREF selectionBackground;
    double textBoxWidthPt;
    double dropdownWidthPt;
    double clientHeightPt;
    BOOL showCaption;
    SIZE minTextBoxSize;
    BOOL inMenuLoop;
    LPWSTR text;
} Model;

typedef struct {
    double x;
    double y;
} Point;

int getBubbleCount(
    Model *model, double widthPt, double heightPt, int maxCount
) {
    return maxCount;
}

Point getBubblePositionPt(
    Model *model, double widthPt, double heightPt, int count, int index
) {
    Point result = {
        .x = 50 * (index % 10) + model->offsetXPt,
        .y = 30 * (index / 10) + model->offsetYPt,
    };
    return result;
}

double pxToPt(double px, UINT dpi) { return px * 72 / (double)dpi; }
double intPxToPt(int px, UINT dpi) {
    return (double)px * 72 / (double)dpi;
}

double ptToPx(double pt, UINT dpi) { return pt * (double)dpi / 72; }
int ptToIntPx(double pt, UINT dpi) {
    return (int)round(pt * (double)dpi / 72);
}
int ptToThinPx(double pt, UINT dpi) {
    return max(1, (int)(pt * (double)dpi / 72));
}

void clampToRect(RECT rect, POINT *point) {
    if (point->x < rect.left) { point->x = rect.left; }
    if (point->y < rect.top) { point->y = rect.top; }
    if (point->x >= rect.right) { point->x = rect.right - 1; }
    if (point->y >= rect.bottom) { point->y = rect.bottom - 1; }
}

COLORREF getTextColor(COLORREF background) {
    // https://stackoverflow.com/questions/3942878/how-to-decide-font-color-in-white-or-black-depending-on-background-color/3943023
    if (
        0.299 * GetRValue(background) + 0.587 * GetGValue(background)
            + 0.114 * GetBValue(background) > 149
    ) {
        return RGB(0, 0, 0);
    } else {
        return RGB(255, 255, 255);
    }
}

void redraw(HWND window) {
    Model *model = (Model*)GetWindowLongPtr(window, GWLP_USERDATA);
    RECT frame;
    GetWindowRect(window, &frame);
    RECT client = {
        0, 0, frame.right - frame.left, frame.bottom - frame.top
    };
    int widthPx = client.right;
    int heightPx = client.bottom;
    HDC device = GetDC(window);
    HDC memory = CreateCompatibleDC(device);
    HBRUSH keyBrush = getKeyBrush(model->colorKey);
    selectKeyBitmap(device, memory, widthPx, heightPx, model->colorKey);
    UINT dpi = GetDpiForWindow(window);
    double widthPt = intPxToPt(widthPx, dpi);
    double heightPt = intPxToPt(heightPx, dpi);
    int bubbleCount = getBubbleCount(
        model, widthPt, heightPt, getLabels().count
    );
    HDC labelMemory = CreateCompatibleDC(device);
    SelectObject(
        labelMemory,
        getLabelFont(dpi, model->fontHeightPt, model->fontFamily)
    );
    SetBkColor(labelMemory, model->labelBackground);
    SetTextColor(labelMemory, getTextColor(model->labelBackground));
    RECT paddingPx = {
        ptToThinPx(model->paddingLeftPt, dpi),
        ptToThinPx(model->paddingTopPt, dpi),
        ptToThinPx(model->paddingRightPt, dpi),
        ptToThinPx(model->paddingBottomPt, dpi),
    };
    RECT *labelRects = selectLabelBitmap(
        device, labelMemory, bubbleCount, paddingPx
    );
    HDC selectionMemory = CreateCompatibleDC(device);
    SelectObject(
        selectionMemory,
        getLabelFont(dpi, model->fontHeightPt, model->fontFamily)
    );
    SetBkColor(selectionMemory, model->selectionBackground);
    SetTextColor(selectionMemory, getTextColor(model->selectionBackground));
    RECT *selectionRects = selectSelectionBitmap(
        device, selectionMemory, bubbleCount
    );
    // RECT labelBitmapRect = {
    //     ptToIntPx(model->offsetXPt, dpi) + 100,
    //     ptToIntPx(model->offsetYPt, dpi) + 400,
    //     ptToIntPx(model->offsetXPt, dpi) + 100 + 600,
    //     ptToIntPx(model->offsetYPt, dpi) + 400 + 400,
    // };
    // BitBlt(
    //     memory, labelBitmapRect.left, labelBitmapRect.top,
    //     labelBitmapRect.right - labelBitmapRect.left,
    //     labelBitmapRect.bottom - labelBitmapRect.top,
    //     labelMemory, 0, 0, SRCCOPY
    // );
    for (int i = 0; i < bubbleCount; i++) {
        Point positionPt = getBubblePositionPt(
            model, widthPt, heightPt, bubbleCount, i
        );
        POINT positionPx = {
            .x = ptToIntPx(positionPt.x, dpi),
            .y = ptToIntPx(positionPt.y, dpi),
        };
        clampToRect(client, &positionPx);
        BitBlt(
            memory,
            positionPx.x,
            positionPx.y,
            labelRects[i].right - labelRects[i].left,
            labelRects[i].bottom - labelRects[i].top,
            labelMemory,
            labelRects[i].left,
            labelRects[i].top,
            SRCCOPY
        );
        BitBlt(
            memory,
            positionPx.x + paddingPx.left,
            positionPx.y + paddingPx.top,
            min(selectionRects[i].right - selectionRects[i].left, 0),
            selectionRects[i].bottom - selectionRects[i].top,
            selectionMemory,
            selectionRects[i].left,
            selectionRects[i].top,
            SRCCOPY
        );
    }
    DeleteDC(selectionMemory);
    DeleteDC(labelMemory);
    ReleaseDC(window, device);
    POINT origin = {0, 0};
    SIZE frameSize = { widthPx, heightPx };
    UpdateLayeredWindow(
        window,
        device,
        NULL,
        &frameSize,
        memory,
        &origin,
        model->colorKey,
        NULL,
        ULW_COLORKEY
    );
    for (int i = 0; i < bubbleCount; i++) {
        Point positionPt = getBubblePositionPt(
            model, widthPt, heightPt, bubbleCount, i
        );
        POINT positionPx = {
            .x = ptToIntPx(positionPt.x, dpi),
            .y = ptToIntPx(positionPt.y, dpi),
        };
        clampToRect(client, &positionPx);
        RECT dstRect = labelRects[i];
        OffsetRect(
            &dstRect, positionPx.x - dstRect.left, positionPx.y - dstRect.top
        );
        FillRect(memory, &dstRect, keyBrush);
    }

    // FillRect(memory, &labelBitmapRect, keyBrush);

    DeleteDC(memory);
}

LRESULT CALLBACK WndProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    if (message == WM_PAINT) {
        redraw(window);
    } else if (message == WM_ACTIVATE) {
        if (LOWORD(wParam) == WA_ACTIVE || LOWORD(wParam) == WA_CLICKACTIVE) {
            HWND dialog = (HWND)GetWindowLongPtr(window, GWLP_USERDATA);
            // The main window initially receives WM_ACTIVATE before the
            // dialog is created.
            if (dialog) {
                SetActiveWindow(dialog);
            }
            return 0;
        }
    } else if (message == WM_DPICHANGED) {
        // https://docs.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged
        LPRECT bounds = (LPRECT)lParam;
        SetWindowPos(
            window,
            NULL,
            bounds->left,
            bounds->top,
            bounds->right - bounds->left,
            bounds->bottom - bounds->top,
            SWP_NOZORDER | SWP_NOACTIVATE
        );
        redraw(window);
        return 0;
    } else if (message == WM_SETTINGCHANGE) {
        if (wParam == SPI_SETNONCLIENTMETRICS && labelFontOut) {
            DeleteObject(labelFontOut);
            labelFontOut = NULL;
            redraw(window);
            return 0;
        }
    } else if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(window, message, wParam, lParam);
}

BOOL CALLBACK SetFontRedraw(HWND child, LPARAM font){
    SendMessage(child, WM_SETFONT, font, TRUE);
    return TRUE;
}

BOOL CALLBACK SetFontNoRedraw(HWND child, LPARAM font){
    SendMessage(child, WM_SETFONT, font, FALSE);
    return TRUE;
}

Model *getModel(HWND window) {
    Model *model = (Model*)GetWindowLongPtr(window, GWLP_USERDATA);
    if (model) { return model; }
    HWND owner = GetWindowOwner(window);
    if (owner) {
        model = (Model*)GetWindowLongPtr(owner, GWLP_USERDATA);
        SetWindowLongPtr(window, GWLP_USERDATA, (LONG)model);
    }

    return model;
}

BOOL shouldShowDropdown(HWND dialog, Model *model) {
    return model->showCaption || model->inMenuLoop
        || GetFocus() != GetDlgItem(dialog, IDC_TEXTBOX);
}

SIZE getMinDialogClientSize(HWND dialog) {
    Model *model = getModel(dialog);
    SIZE clientSize = model->minTextBoxSize;
    if (clientSize.cy == 0) { return clientSize; }
    UINT dpi = GetDpiForWindow(dialog);
    int textBoxWidth = ptToIntPx(model->textBoxWidthPt, dpi);
    clientSize.cx = max(clientSize.cx, textBoxWidth);
    if (shouldShowDropdown(dialog, model)) {
        clientSize.cx += ptToIntPx(model->dropdownWidthPt, dpi);
    }

    int clientHeight = ptToIntPx(model->clientHeightPt, dpi);
    clientSize.cy = max(clientSize.cy, clientHeight);
    return clientSize;
}

typedef struct {
    SIZE clientSize;
    BOOL showCaption;
    BOOL showMenuBar;
} MinDialogSizeIn;
MinDialogSizeIn minDialogSizeIn = { .clientSize = { 0, 0 } };
SIZE minDialogSizeOut;
SIZE getMinDialogSize(HWND dialog) {
    Model *model = getModel(dialog);
    MinDialogSizeIn in = {
        .clientSize = getMinDialogClientSize(dialog),
        .showCaption = model->showCaption,
        .showMenuBar = GetMenu(dialog) != NULL,
    };
    if (in.clientSize.cy == 0) { return in.clientSize; }
    if (!memcmp(&in, &minDialogSizeIn, sizeof(in))) {
        return minDialogSizeOut;
    }
    debugIncrement(model->window);

    minDialogSizeIn = in;
    RECT frame = { 0, 0, in.clientSize.cx, in.clientSize.cy };
    AdjustWindowRectEx(
        &frame,
        GetWindowLongPtr(dialog, GWL_STYLE),
        in.showMenuBar,
        GetWindowLongPtr(dialog, GWL_EXSTYLE)
    );
    minDialogSizeOut.cx = frame.right - frame.left;
    minDialogSizeOut.cy = frame.bottom - frame.top;
    return minDialogSizeOut;
}

void applyMinDialogSize(HWND dialog) {
    RECT newFrame = {0, 0, 0, 0};
    *(LPSIZE)&newFrame.right = getMinDialogSize(dialog);
    if (newFrame.bottom == 0) { return; }
    RECT frame;
    GetWindowRect(dialog, &frame);
    OffsetRect(&frame, -frame.left, -frame.top);
    if (getModel(dialog)->showCaption) {
        newFrame.right = max(newFrame.right, frame.right);
    }

    if (!memcmp(&frame, &newFrame, sizeof(frame))) { return; }
    SetWindowPos(
        dialog,
        NULL,
        0, 0,
        newFrame.right, newFrame.bottom,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
    );
}

const int PRESSED = 0x8000;
const UINT WM_APP_FITTOTEXT = WM_APP + 0;
const int ENABLE_DROPDOWN_TIMER = 1;
LRESULT CALLBACK DlgProc(
    HWND dialog,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    if (message == WM_INITDIALOG) {
        Model *model = getModel(dialog);
        model->inMenuLoop = FALSE;
        MENUITEMINFO menuItemInfo = {
            .cbSize = sizeof(MENUITEMINFO),
            .fMask = MIIM_STATE,
            .fState =
                model->showCaption ? MFS_UNCHECKED : MFS_CHECKED,
        };
        SetMenuItemInfo(
            getDropdownMenu(),
            IDM_HIDE_INTERFACE,
            FALSE,
            &menuItemInfo
        );
        LONG_PTR style = GetWindowLongPtr(dialog, GWL_STYLE);
        if (model->showCaption) {
            style |= WS_THICKFRAME | WS_CAPTION;
        } else {
            style &= ~WS_THICKFRAME & ~WS_CAPTION;
        }

        SetWindowLongPtr(dialog, GWL_STYLE, style);
        SendMessage(
            dialog,
            WM_SETFONT,
            (WPARAM)getSystemFont(GetDpiForWindow(dialog)),
            FALSE
        );
        // https://docs.microsoft.com/en-us/windows/win32/controls/bm-setimage
        SendMessage(
            GetDlgItem(dialog, IDC_DROPDOWN),
            BM_SETIMAGE,
            (WPARAM)IMAGE_BITMAP,
            (LPARAM)getDropdownBitmap()
        );
        return TRUE;
    } else if (message == WM_SETFONT) {
        // https://stackoverflow.com/a/17075471
        if (LOWORD(lParam)) {
            EnumChildWindows(dialog, SetFontRedraw, wParam);
        } else {
            EnumChildWindows(dialog, SetFontNoRedraw, wParam);
        }

        // for some reason Windows adds margins when the font changes
        SendMessage(
            GetDlgItem(dialog, IDC_TEXTBOX),
            EM_SETMARGINS,
            EC_LEFTMARGIN | EC_RIGHTMARGIN,
            MAKELPARAM(0, 0)
        );

        PostMessage(dialog, WM_APP_FITTOTEXT, 0, 0);
        return TRUE;
    } else if (message == WM_APP_FITTOTEXT) {
        HWND textBox = GetDlgItem(dialog, IDC_TEXTBOX);
        HDC device = GetDC(textBox);
        HFONT font = (HFONT)SendMessage(textBox, WM_GETFONT, 0, 0);
        HFONT oldFont = SelectObject(device, font);
        RECT rect = { 0, 0, 0, 0 };
        Model *model = getModel(dialog);
        DrawText(
            device, model->text, -1, &rect,
            DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX
        );
        SelectObject(device, oldFont);
        ReleaseDC(textBox, device);

        // 1px cursor, 1px padding left, 2px padding right, left and right margin
        LRESULT margins = SendMessage(textBox, EM_GETMARGINS, 0, 0);
        rect.right += 4 + LOWORD(margins) + HIWORD(margins);
        rect.bottom += 2; // 1px padding top, 1px padding bottom
        AdjustWindowRectEx(
            &rect,
            GetWindowLongPtr(textBox, GWL_STYLE),
            FALSE, // bMenu
            GetWindowLongPtr(textBox, GWL_EXSTYLE)
        );
        model->minTextBoxSize.cx = rect.right - rect.left;
        model->minTextBoxSize.cy = rect.bottom - rect.top;
        applyMinDialogSize(dialog);
        return TRUE;
    } else if (message == WM_SIZE) {
        RECT client;
        GetClientRect(dialog, &client);
        Model *model = getModel(dialog);
        UINT dpi = GetDpiForWindow(dialog);
        int dropdownWidth = ptToIntPx(model->dropdownWidthPt, dpi);
        if (shouldShowDropdown(dialog, model)) {
            client.right -= dropdownWidth;
        }

        SetWindowPos(
            GetDlgItem(dialog, IDC_TEXTBOX),
            NULL, 0, 0, client.right, client.bottom, 0
        );
        SetWindowPos(
            GetDlgItem(dialog, IDC_DROPDOWN),
            NULL,
            client.right, 0,
            dropdownWidth, client.bottom,
            0
        );
        return TRUE;
    } else if (message == WM_GETMINMAXINFO) {
        if (getModel(dialog)->showCaption) {
            LPMINMAXINFO minMaxInfo = (LPMINMAXINFO)lParam;
            SIZE minSize = getMinDialogSize(dialog);
            minMaxInfo->ptMinTrackSize.x = minSize.cx;
            minMaxInfo->ptMinTrackSize.y = minSize.cy;
            minMaxInfo->ptMaxTrackSize.y = minSize.cy;
        }

        return 0;
    } else if (message == WM_DPICHANGED) {
        SendMessage(
            dialog,
            WM_SETFONT,
            (WPARAM)getSystemFont(GetDpiForWindow(dialog)),
            FALSE
        );
        return TRUE;
    } else if (message == WM_SETTINGCHANGE) {
        if (wParam == SPI_SETNONCLIENTMETRICS && systemFontOut) {
            DeleteObject(systemFontOut);
            systemFontOut = NULL;
            SendMessage(
                dialog,
                WM_SETFONT,
                (WPARAM)getSystemFont(GetDpiForWindow(dialog)),
                FALSE
            );
            return TRUE;
        }
    } else if (message == WM_CONTEXTMENU) {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        // https://devblogs.microsoft.com/oldnewthing/20040921-00/?p=37813
        if (point.x == -1 && point.y == -1) {
            RECT buttonRect;
            GetWindowRect(GetFocus(), &buttonRect);
            point.x = (buttonRect.left + buttonRect.right) / 2;
            point.y = (buttonRect.top + buttonRect.bottom) / 2;
        }

        // http://winapi.freetechsecrets.com/win32/WIN32Processing_the_WMCONTEXTMENU_Mes.htm
        RECT client;
        GetClientRect(dialog, &client);
        POINT clientPoint = point;
        ScreenToClient(dialog, &clientPoint);
        if (PtInRect(&client, clientPoint)) {
            TrackPopupMenu(
                GetSubMenu(getDropdownMenu(), 0),
                TPM_RIGHTBUTTON,
                point.x, point.y,
                0,
                dialog,
                NULL
            );
        }

        return TRUE;
    } else if (message == WM_SYSCOMMAND) {
        if ((wParam & 0xFFF0) == SC_KEYMENU) {
            SetMenu(dialog, getDropdownMenu());
            applyMinDialogSize(dialog);
            return 0;
        }
    } else if (message == WM_ENTERMENULOOP) {
        getModel(dialog)->inMenuLoop = TRUE;
        applyMinDialogSize(dialog);
        return TRUE;
    } else if (message == WM_EXITMENULOOP) {
        HWND button = GetDlgItem(dialog, IDC_DROPDOWN);
        if (Button_GetCheck(button)) {
            Button_SetCheck(button, BST_UNCHECKED);
            SetWindowRedraw(button, FALSE);
            Button_Enable(button, FALSE);
        }

        getModel(dialog)->inMenuLoop = FALSE;
        SetTimer(dialog, ENABLE_DROPDOWN_TIMER, 0, NULL);
        return TRUE;
    } else if (message == WM_TIMER) {
        if (wParam == ENABLE_DROPDOWN_TIMER) {
            HWND button = GetDlgItem(dialog, IDC_DROPDOWN);
            Button_Enable(button, TRUE);
            if (GetFocus() == NULL) { SetFocus(button); }
            SetWindowRedraw(button, TRUE);
            KillTimer(dialog, wParam);
            SetMenu(dialog, NULL);
            applyMinDialogSize(dialog);
            return TRUE;
        }
    } else if (message == WM_COMMAND) {
        if (HIWORD(wParam) == 0 || HIWORD(wParam) == 1) {
            if (LOWORD(wParam) == 2) {
                PostQuitMessage(0);
                return TRUE;
            } else if (LOWORD(wParam) == IDM_EXIT) {
                PostQuitMessage(0);
                return TRUE;
            } else if (LOWORD(wParam) == IDM_SELECT_ALL) {
                HWND textBox = GetDlgItem(dialog, IDC_TEXTBOX);
                SendMessage(textBox, EM_SETSEL, 0, -1);
                SetFocus(textBox);
                return TRUE;
            } else if (LOWORD(wParam) == IDC_DROPDOWN) {
                // https://docs.microsoft.com/en-us/windows/win32/controls/handle-drop-down-buttons
                HWND button = GetDlgItem(dialog, IDC_DROPDOWN);
                Button_SetCheck(button, BST_CHECKED);
                SetWindowRedraw(button, TRUE);
                RECT buttonRect;
                GetWindowRect(button, &buttonRect);
                TPMPARAMS popupParams;
                popupParams.cbSize = sizeof(popupParams);
                popupParams.rcExclude = buttonRect;
                TrackPopupMenuEx(
                    GetSubMenu(getDropdownMenu(), 0),
                    TPM_VERTICAL | TPM_LEFTBUTTON,
                    buttonRect.left,
                    buttonRect.bottom,
                    dialog,
                    &popupParams
                );

                return TRUE;
            } else if (
                LOWORD(wParam) == IDM_LEFT
                    || LOWORD(wParam) == IDM_UP
                    || LOWORD(wParam) == IDM_RIGHT
                    || LOWORD(wParam) == IDM_DOWN
            ) {
                ArrowKeyMap keys = getNormalArrowKeyMap();
                int xDirection = (
                    LOWORD(wParam) == IDM_RIGHT
                        || (GetKeyState(keys.right) & PRESSED)
                ) - (
                    LOWORD(wParam) == IDM_LEFT
                        || (GetKeyState(keys.left) & PRESSED)
                );
                int yDirection = (
                    LOWORD(wParam) == IDM_DOWN
                        || (GetKeyState(keys.down) & PRESSED)
                ) - (
                    LOWORD(wParam) == IDM_UP
                        || (GetKeyState(keys.up) & PRESSED)
                );
                Model *model = getModel(dialog);
                UINT dpi = GetDpiForWindow(model->window);
                model->offsetXPt += xDirection * pxToPt(model->deltaPx, dpi);
                model->offsetYPt += yDirection * pxToPt(model->deltaPx, dpi);
                RedrawWindow(model->window, NULL, NULL, RDW_INTERNALPAINT);
                return TRUE;
            } else if (
                LOWORD(wParam) == IDM_SLIGHTLY_LEFT
                    || LOWORD(wParam) == IDM_SLIGHTLY_UP
                    || LOWORD(wParam) == IDM_SLIGHTLY_RIGHT
                    || LOWORD(wParam) == IDM_SLIGHTLY_DOWN
            ) {
                ArrowKeyMap keys = getSlowArrowKeyMap();
                int xDirection = (
                    LOWORD(wParam) == IDM_SLIGHTLY_RIGHT
                        || (GetKeyState(keys.right) & PRESSED)
                ) - (
                    LOWORD(wParam) == IDM_SLIGHTLY_LEFT
                        || (GetKeyState(keys.left) & PRESSED)
                );
                int yDirection = (
                    LOWORD(wParam) == IDM_SLIGHTLY_DOWN
                        || (GetKeyState(keys.down) & PRESSED)
                ) - (
                    LOWORD(wParam) == IDM_SLIGHTLY_UP
                        || (GetKeyState(keys.up) & PRESSED)
                );
                Model *model = getModel(dialog);
                UINT dpi = GetDpiForWindow(model->window);
                model->offsetXPt += xDirection * pxToPt(
                    model->smallDeltaPx, dpi
                );
                model->offsetYPt += yDirection * pxToPt(
                    model->smallDeltaPx, dpi
                );
                RedrawWindow(model->window, NULL, NULL, RDW_INTERNALPAINT);
                return TRUE;
            } else if (LOWORD(wParam) == IDM_HIDE_INTERFACE) {
                Model *model = getModel(dialog);
                model->showCaption = !model->showCaption;
                DWORD selectionStart, selectionStop;
                SendMessage(
                    GetDlgItem(dialog, IDC_TEXTBOX),
                    EM_GETSEL,
                    (WPARAM)&selectionStart,
                    (LPARAM)&selectionStop
                );
                SetMenu(dialog, NULL);
                DestroyWindow(dialog);
                model->minTextBoxSize.cx = model->minTextBoxSize.cy = 0;
                model->dialog = CreateDialog(
                    GetModuleHandle(NULL), L"TOOL", model->window, DlgProc
                );
                SetDlgItemText(model->dialog, IDC_TEXTBOX, model->text);
                SendMessage(
                    GetDlgItem(model->dialog, IDC_TEXTBOX),
                    EM_SETSEL,
                    selectionStart,
                    selectionStop
                );
            }
        } else if (LOWORD(wParam) == IDC_TEXTBOX) {
            if (
                HIWORD(wParam) == EN_SETFOCUS
                    || HIWORD(wParam) == EN_KILLFOCUS
            ) {
                applyMinDialogSize(dialog);
                return TRUE;
            } else if (HIWORD(wParam) == EN_UPDATE) {
                HWND textBox = GetDlgItem(dialog, IDC_TEXTBOX);
                getModel(dialog)->text = getTextBoxText(textBox);
                SendMessage(dialog, WM_APP_FITTOTEXT, 0, 0);
                return TRUE;
            }
        }
    } else if (message == WM_CLOSE) {
        PostQuitMessage(0);
        return TRUE;
    } else if (message == WM_ACTIVATE || message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == 70 || message == WM_SYSKEYDOWN || message == 71 || message == 28 || message == 134 || message == 799 || message == 1024 || message == 307 || message == 309 || message == 32 || message == 131 || message == 133 || message == 20 || message == 310 || message == 15) {

    } else {
        return FALSE;
    }

    return FALSE;
}

const int SHIFT = 1, CONTROL = 2, ALT = 4, WIN = 8;
int getModifiers() {
    int modifiers = 0;
    modifiers |= GetKeyState(VK_SHIFT) & PRESSED ? SHIFT : 0;
    modifiers |= GetKeyState(VK_CONTROL) & PRESSED ? CONTROL : 0;
    modifiers |= GetKeyState(VK_MENU) & PRESSED ? ALT : 0;
    modifiers |=
        (GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & PRESSED ? WIN : 0;
    return modifiers;
}

int TranslateAcceleratorCustom(HWND dialog, MSG *message) {
    if (message->message == WM_KEYDOWN) {
        if (message->wParam == VK_SPACE && !getModifiers()) {
            if (GetFocus() != GetDlgItem(dialog, IDC_TEXTBOX)) {
                return 0;
            }
        } else if (
            message->wParam == VK_LEFT || message->wParam == VK_RIGHT
        ) {
            int modifiers = getModifiers();
            if (
                modifiers == 0 || modifiers == SHIFT || modifiers == CONTROL
                    || modifiers == (CONTROL | SHIFT)
            ) {
                HWND textBox = GetDlgItem(dialog, IDC_TEXTBOX);
                if (GetFocus() == textBox) {
                    DWORD selectionStart, selectionStop;
                    SendMessage(
                        GetDlgItem(dialog, IDC_TEXTBOX),
                        EM_GETSEL,
                        (WPARAM)&selectionStart,
                        (LPARAM)&selectionStop
                    );
                    if (selectionStart < GetWindowTextLength(textBox)) {
                        return 0;
                    }
                }
            }
        }
    }
    return TranslateAccelerator(dialog, getAcceleratorTable(), message);
}

int CALLBACK WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow)
{
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

    double PI = 3.1415926535897932384626433832795;
    Model model = {
        .colorKey = RGB(255, 0, 255),
        .offsetXPt = 0,
        .offsetYPt = 0,
        .deltaPx = 12,
        .smallDeltaPx = 1,
        .bubbleCount = 50,
        .marginLeftPt = 0,
        .marginTopPt = 0,
        .aspect = 4 / 3,
        .angle1 = 15 * PI / 180,
        .angle2 = 75 * PI / 180,
        .fontHeightPt = 0,
        .fontFamily = L"",
        .paddingLeftPt = 0.75,
        .paddingTopPt = 0.75,
        .paddingRightPt = 0.75,
        .paddingBottomPt = 0.75,
        .labelBackground = RGB(255, 255, 255),
        .selectionBackground = GetSysColor(COLOR_HIGHLIGHT),
        .textBoxWidthPt = 15,
        .dropdownWidthPt = 15,
        .clientHeightPt = 21,
        .showCaption = TRUE,
        .minTextBoxSize = { 0, 0 },
        .inMenuLoop = FALSE,
        .text = L"",
    };

    WNDCLASS windowClass = {
        .lpfnWndProc   = WndProc,
        .hInstance     = hInstance,
        .hIcon         = LoadIcon(hInstance, (LPCWSTR)101),
        .hCursor       = LoadCursor(0, IDC_ARROW),
        // https://www.guidgenerator.com/
        .lpszClassName = L"MouseJump,b354100c-e6a7-4d32-a0bb-643e35a82fb0",
    };
    RegisterClass(&windowClass);

    HWND oldWindow = NULL;
    do {
        oldWindow = FindWindowEx(
            NULL, // parent
            oldWindow,
            windowClass.lpszClassName,
            NULL // title
        );
        if (oldWindow) {
            PostMessage(oldWindow, WM_CLOSE, 0, 0);
        }
    } while (oldWindow);

    model.window = CreateWindowEx(
        WS_EX_LAYERED,
        windowClass.lpszClassName,
        L"MouseJump",
        WS_POPUP | WS_VISIBLE | WS_MAXIMIZE,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, // parent
        NULL, // menu
        hInstance,
        NULL // lParam
    );
    SetWindowLongPtr(model.window, GWLP_USERDATA, (LONG)&model);
    model.dialog = CreateDialog(hInstance, L"TOOL", model.window, DlgProc);
    redraw(model.window);
    MSG message;
    while (GetMessage(&message, NULL, 0, 0)) {
        // https://devblogs.microsoft.com/oldnewthing/20120416-00/?p=7853
        if (
            !TranslateAcceleratorCustom(model.dialog, &message)
                && !IsDialogMessage(model.dialog, &message)
        ) {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
    }

    destroyCache();
    return 0;
}

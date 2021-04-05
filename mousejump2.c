// rc mousejump2.rc && cl mousejump2.c /link mousejump2.res && mousejump2.exe

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

#define PI 3.1415926535897932384626433832795

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

typedef struct { LPWSTR *value; int count; } StringArray;
StringArray labelsOut = { .value = NULL };
StringArray getLabels() {
    if (labelsOut.value) { return labelsOut; }
    labelsOut.count = 0;
    LPWSTR labelText = getLabelText();
    for (LPWSTR i = labelText; ; i++) {
        if (*i == L'\r' && *(i + 1) == L'\n') { i++; }
        if ((*i == L'\r' || *i == L'\n') && *(i + 1) == L'\0') { i++; }
        if (*i == L'\r' || *i == L'\n' || *i == L'\0') {
            labelsOut.count++;
            if (*i == L'\0') { break; }
        }
    }

    if (labelText[0] == L'\0') { labelsOut.count = 0; }
    labelsOut.value = malloc(labelsOut.count * sizeof(LPCWSTR));
    LPCWSTR *labelOut = labelsOut.value;
    if (labelsOut.count) {
        *labelOut = labelText;
        labelOut++;
    }

    for (LPWSTR i = labelText; ; i++) {
        if (*i == L'\r' && *(i + 1) == L'\n') {
            *i = L'\0';
            i++;
        }

        if ((*i == L'\r' || *i == L'\n') && *(i + 1) == L'\0') {
            *i = L'\0';
            i++;
        }

        if (*i == L'\r' || *i == L'\n' || *i == L'\0') {
            if (*i == L'\0') { break; }
            *labelOut = i + 1;
            labelOut++;
        }
    }

    return labelsOut;
}

int compareStringPointersIgnoreCase(const wchar_t **a, const wchar_t **b) {
    return _wcsicmp(*a, *b);
}

int sortedLabelsIn = 0;
StringArray sortedLabelsOut = { .count = 0, .value = NULL };
StringArray getSortedLabels(int count) {
    if (count == sortedLabelsIn) { return sortedLabelsOut; }
    sortedLabelsIn = count;
    StringArray labels = getLabels();
    sortedLabelsOut.count = min(count, labels.count);
    sortedLabelsOut.value = realloc(
        sortedLabelsOut.value,
        sortedLabelsOut.count * sizeof(LPWSTR)
    );
    memcpy_s(
        sortedLabelsOut.value,
        sortedLabelsOut.count * sizeof(LPWSTR),
        labels.value,
        sortedLabelsOut.count * sizeof(LPWSTR)
    );
    qsort(
        (void*)sortedLabelsOut.value,
        sortedLabelsOut.count,
        sizeof(LPWSTR),
        compareStringPointersIgnoreCase
    );
    return sortedLabelsOut;
}

int labelBinarySearch(
    LPCWSTR *labels, WCHAR target, int lo, int hi, int offset, BOOL right
) {
    // returns an index in the range [lo, hi]
    // right=0: index of the first label greater than or equal to target
    // right=1: index of the first label greater than target
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (_wcsnicmp(labels[mid] + offset, &target, 1) < right) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

typedef struct {
    int start;
    int stop;
    int matchLength;
    int count;
} LabelRange;
LabelRange getLabelRange(LPCWSTR text, StringArray labels) {
    int start = 0;
    int stop = labels.count;
    int textLength = wcslen(text);
    int matchLength = 0;
    for (int i = 0; i < textLength; i++) {
        int newStart = labelBinarySearch(
            labels.value, text[i], start, stop, matchLength, 0
        );
        int newStop = labelBinarySearch(
            labels.value, text[i], newStart, stop, matchLength, 1
        );
        if (newStart < newStop) {
            start = newStart;
            stop = newStop;
            matchLength++;
        }
    }

    LabelRange result = { start, stop, matchLength, labels.count };
    return result;
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
    LabelWidthsIn in = { .count = count };
    GetObject(GetCurrentObject(device, OBJ_FONT), sizeof(in.font), &in.font);
    if (labelWidthsOut && !memcmp(&in, &labelWidthsIn, sizeof(in))) {
        return labelWidthsOut;
    }

    labelWidthsIn = in;
    StringArray labels = getSortedLabels(count);
    labelWidthsOut = realloc(labelWidthsOut, labels.count * sizeof(int));
    for (int i = 0; i < labels.count; i++) {
        RECT rect = { 0, 0 };
        DrawText(
            device,
            labels.value[i],
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
        .count = count,
        .paddingPx = paddingPx,
        .foreground = GetTextColor(memory),
        .background = GetBkColor(memory)
    };
    GetObject(GetCurrentObject(memory, OBJ_FONT), sizeof(in.font), &in.font);
    if (labelBitmapOut->bitmap) {
        if (!memcmp(&in, labelBitmapIn, sizeof(in))) {
            SelectObject(memory, labelBitmapOut->bitmap);
            return labelBitmapOut->rects;
        }

        DeleteObject(labelBitmapOut->bitmap);
    }

    *labelBitmapIn = in;
    int xPadding = max(paddingPx.left, paddingPx.right);
    int yPadding = max(paddingPx.top, paddingPx.bottom);
    StringArray labels = getSortedLabels(count);
    int *labelWidths = getLabelWidths(memory, count);
    int width = 500;
    for (int i = 0; i < labels.count; i++) {
        width = max(width, labelWidths[i] + 2 * xPadding);
    }

    TEXTMETRIC metrics;
    GetTextMetrics(memory, &metrics);
    int x = xPadding;
    int height = metrics.tmHeight + 2 * yPadding;
    for (int i = 0; i < labels.count; i++) {
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
        for (int i = 0; i < labels.count; i++) {
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

    labelBitmapOut->rects = realloc(
        labelBitmapOut->rects, labels.count * sizeof(RECT)
    );
    x = xPadding;
    int y = yPadding;
    for (int i = 0; i < labels.count; i++) {
        if (x + labelWidths[i] + xPadding > width) {
            x = xPadding;
            y += metrics.tmHeight + yPadding;
        }

        RECT rect = { x, y, x + labelWidths[i], y + metrics.tmHeight };
        DrawText(
            memory, labels.value[i], -1, &rect,
            DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP
        );

        int features = getStringFeatures(labels.value[i]);
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

typedef struct { COLORREF color; int width; } BorderPenIn;
BorderPenIn borderPenIn;
HPEN borderPenOut;
HPEN getBorderPen(COLORREF color, int width) {
    BorderPenIn in = { .color = color, .width = width };
    if (borderPenOut) {
        if (!memcmp(&in, &borderPenIn, sizeof(in))) { return borderPenOut; }
        DeletePen(borderPenOut);
    }

    borderPenIn = in;
    LOGBRUSH brush = { .lbStyle = BS_SOLID, .lbColor = color };
    return borderPenOut = ExtCreatePen(
        PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER,
        width, &brush, 0, NULL
    );
}

typedef struct {
    int borderPx;
    int offsetPx;
    int heightPx;
    COLORREF keyColor;
    COLORREF borderColor;
    COLORREF backgroundColor;
} EarBitmapIn;
EarBitmapIn earBitmapIn;
HBITMAP earBitmapOut = NULL;
const double sqrt2 = 1.4142135623730950488016887242096;
SIZE selectEarBitmap(
    HDC device, HDC memory,
    int borderPx, int offsetPx, int heightPx,
    COLORREF keyColor, COLORREF borderColor, COLORREF backgroundColor
) {
    SIZE size = { offsetPx, heightPx };
    EarBitmapIn in = {
        .borderPx = borderPx,
        .offsetPx = offsetPx,
        .heightPx = heightPx,
        .keyColor = keyColor,
        .borderColor = borderColor,
        .backgroundColor = backgroundColor,
    };
    if (earBitmapOut) {
        if (!memcmp(&in, &earBitmapIn, sizeof(in))) {
            SelectObject(memory, earBitmapOut);
            return size;
        }

        DeleteObject(earBitmapOut);
    }

    earBitmapIn = in;
    earBitmapOut = CreateCompatibleBitmap(device, offsetPx, heightPx);
    SelectObject(memory, earBitmapOut);
    RECT rect = { 0, 0, offsetPx, heightPx };
    FillRect(memory, &rect, getKeyBrush(keyColor));
    SelectObject(memory, getBorderPen(borderColor, borderPx));
    SelectObject(memory, getLabelBrush(backgroundColor));
    int borderDiagonalPx = (int)round(borderPx * sqrt2);
    int xyOffset = borderPx / 2;
    int yOffset = (borderDiagonalPx - 1) / 2;
    POINT points[3] = {
        { xyOffset, heightPx + xyOffset },
        { xyOffset, xyOffset + yOffset },
        { heightPx + xyOffset - yOffset, heightPx + xyOffset },
    };
    Polygon(memory, points, 3);
    SelectObject(memory, getBorderPen(RGB(0, 0, 0), 1));
    rect.left = borderPx;
    rect.top = offsetPx;
    FillRect(memory, &rect, getKeyBrush(keyColor));
    return size;
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

int nextPowerOf2(int n, int start) {
    int result = start;
    while (result < n) { result <<= 1; }
    return result;
}

struct { LPWSTR text; int capacity; } textBoxTextOut = { .text = NULL };
LPWSTR getTextBoxText(HWND textBox) {
    int oldCapacity = textBoxTextOut.text ? textBoxTextOut.capacity : 0;\
    textBoxTextOut.capacity = nextPowerOf2(
        GetWindowTextLength(textBox) + 1,
        64
    );
    if (textBoxTextOut.capacity > oldCapacity) {
        textBoxTextOut.text = (LPWSTR)realloc(
            textBoxTextOut.text, textBoxTextOut.capacity * sizeof(WCHAR)
        );
    }

    GetWindowText(textBox, textBoxTextOut.text, textBoxTextOut.capacity);
    return textBoxTextOut.text;
}

#pragma endregion

typedef struct { double x; double y; } Point;
Point makePoint(double x, double y) { Point point = { x, y }; return point; }
Point scale(Point v, double a) { return makePoint(a * v.x, a * v.y); }
Point add(Point v1, Point v2) { return makePoint(v1.x + v2.x, v1.y + v2.y); }
double dot(Point v1, Point v2) { return v1.x * v2.x + v1.y * v2.y; }
Point leftTurn(Point v) { return makePoint(-v.y, v.x); }
Point getNormal(Point v) { return scale(leftTurn(v), 1 / sqrt(dot(v, v))); }
double determinant(Point v1, Point v2) { return dot(leftTurn(v1), v2); }
Point matrixDot(Point column1, Point column2,  Point v) {
    return add(scale(column1, v.x), scale(column2, v.y));
}

Point intersect(Point point1, Point normal1, Point point2, Point normal2) {
    double denominator = determinant(normal1, normal2);
    return leftTurn(
        add(
            scale(normal1, dot(point2, normal2) / denominator),
            scale(normal2, -dot(point1, normal1) / denominator)
        )
    );
}

typedef struct { double angle1; double angle2; double aspect; } CellShapeIn;
CellShapeIn cellShapeIn = { 0, PI, 1 };
typedef struct { Point shape1; Point shape2; } CellShapeOut;
CellShapeOut cellShapeOut = { { 1, 0 }, { 0, 1 } };
void getCellShape(
    double angle1, double angle2, double aspect,
    Point *shape1, Point *shape2
) {
    CellShapeIn in = { angle1, angle2, aspect };
    if (memcmp(&in, &cellShapeIn, sizeof(in))) {
        cellShapeIn = in;
        cellShapeOut.shape1 = makePoint(cos(angle1) * aspect, sin(angle1));
        cellShapeOut.shape2 = makePoint(cos(angle2) * aspect, sin(angle2));
        double shapeScale = 1 / sqrt(
            determinant(cellShapeOut.shape1, cellShapeOut.shape2)
        );
        cellShapeOut.shape1 = scale(cellShapeOut.shape1, shapeScale);
        cellShapeOut.shape2 = scale(cellShapeOut.shape2, shapeScale);
    }

    *shape1 = cellShapeOut.shape1;
    *shape2 = cellShapeOut.shape2;
}

int minN(int *candidates, int count) {
    int result = candidates[0];
    for (int i = 1; i < count; i++) { result = min(result, candidates[i]); }
    return result;
}

int maxN(int *candidates, int count) {
    int result = candidates[0];
    for (int i = 1; i < count; i++) { result = max(result, candidates[i]); }
    return result;
}

typedef struct { int i; BOOL right; double overlap; } EdgeCell;

int compareEdgeCells(const EdgeCell *a, const EdgeCell *b) {
    if (a->overlap > b->overlap) { return 1; }
    if (a->overlap < b->overlap) { return -1; }
    if (a->i > b->i) { return 1; }
    if (a->i < b->i) { return -1; }
    if (a->right > b->right) { return 1; }
    if (a->right < b->right) { return -1; }
    return 0;
}

struct { int capacity; EdgeCell *edgeCells; } edgeCellsOut = { 0, NULL };
EdgeCell *getEdgeCells(int count) {
    if (count > edgeCellsOut.capacity) {
        edgeCellsOut.capacity = nextPowerOf2(count, 64);
        edgeCellsOut.edgeCells = realloc(
            edgeCellsOut.edgeCells,
            edgeCellsOut.capacity * sizeof(EdgeCell)
        );
    }

    return edgeCellsOut.edgeCells;
}

typedef struct {
    int spineStart;
    int spineStop;
    int *ribStarts;
    int *ribStops;
} Spine;
struct {
    Spine oldSpine;
    Spine newSpine;
    int oldCapacity;
    int newCapacity;
} spinesOut = {
    .oldSpine = { 0, 0, NULL, NULL },
    .newSpine = { 0, 0, NULL, NULL },
    .oldCapacity = 0,
    .newCapacity = 0,
};
Spine getSpines(
    double angle1, double angle2, double aspect, Point offsetPt,
    double widthPt, double heightPt, int count, HWND dialog,
    Spine *oldSpine, Spine *newSpine
) {
    // get the shape of all screen parallelograms (cells) without worrying
    // about scale yet. shape1 and shape2 represent the edges that
    // approximately correspond to the x and y axes respectively.
    // in the windows API in general and in this function specifically, the
    // positive y direction is downward.
    Point shape1, shape2;
    getCellShape(angle1, angle2, aspect, &shape1, &shape2);
    double inverseScale = sqrt(count / (widthPt * heightPt));
    // inverse1 and inverse2 are edges of a 1pt x 1pt square, projected into
    // grid space (in other words, they are the columns of the inverse of the
    // matrix whose columns are the edges of a screen parallelogram).
    Point inverse1 = scale(makePoint(shape2.y, -shape1.y), inverseScale);
    Point inverse2 = scale(makePoint(-shape2.x, shape1.x), inverseScale);
    Point gridOffset = matrixDot(inverse1, inverse2, scale(offsetPt, -1));
    // the grid parallelogram is the entire screen area projected into grid
    // space
    Point gridEdge1 = scale(inverse1, widthPt);
    Point gridEdge2 = scale(inverse2, heightPt);
    // normals radiate out from the grid parallelogram
    Point gridNormal1 = scale(getNormal(inverse1), -1);
    Point gridNormal2 = getNormal(inverse2);
    Point gridNormals[5] = {
        gridNormal1,
        gridNormal2,
        scale(gridNormal1, -1),
        scale(gridNormal2, -1),
        gridNormal1, // for convenience
    };
    // the inflated grid parallelogram is created by shifting each edge of the
    // grid parallelogram by borderRadius grid units in the direction of its
    // normal, to encompass grid cells on the border (edge cells).
    double borderRadius = 0.5;
    // pick an arbitrary point on each edge of the inflated grid parallelogram
    Point gridPoints[5] = {
        add(gridOffset, scale(gridNormal1, borderRadius)),
        add(gridOffset, scale(gridNormal2, borderRadius)),
        add(add(gridOffset, gridEdge2), scale(gridNormal1, -borderRadius)),
        add(add(gridOffset, gridEdge1), scale(gridNormal2, -borderRadius)),
        add(gridOffset, scale(gridNormal1, borderRadius)), // for convenience
    };
    int spineCandidates[4];
    for (int i = 0; i < 4; i++) {
        spineCandidates[i] = (int)ceil(
            intersect(
                gridPoints[i], gridNormals[i],
                gridPoints[i + 1], gridNormals[i + 1]
            ).y
        );
    }

    int spineStart = minN(spineCandidates, 4);
    int spineStop = maxN(spineCandidates, 4);
    int *ribStarts = spinesOut.oldSpine.ribStarts;
    int spineCapacity = max(
        spinesOut.oldCapacity,
        nextPowerOf2(2 * (spineStop - spineStart), 64)
    );
    if (spineCapacity > spinesOut.oldCapacity) {
        ribStarts = realloc(ribStarts, spineCapacity * sizeof(int));
    }

    int *ribStops = ribStarts + (spineStop - spineStart);
    Point rowNormal = { 0, 1 };
    Point cell = { 0, spineStart };
    int actualCount = 0;
    int edgeCellCount = 0;
    for (int i = 0; i < spineStop - spineStart; i++) {
        cell.y = spineStart + i;
        double xEntry = -INFINITY;
        double xExit = INFINITY;
        for (int j = 0; j < 4; j++) {
            if (gridNormals[j].x == 0) { continue; }
            double x = intersect(
                gridPoints[j], gridNormals[j], cell, rowNormal
            ).x;
            if (gridNormals[j].x < 0) { xEntry = max(xEntry, x); }
            if (gridNormals[j].x > 0) { xExit = min(xExit, x); }
        }

        double xCenter = (int)ceil(0.5 * (xEntry + xExit));
        ribStarts[i] = (int)ceil(xEntry);
        ribStops[i] = (int)ceil(xExit);
        actualCount += ribStops[i] - ribStarts[i];
        int direction = 1;
        for (
            cell.x = ribStarts[i];
            direction > 0 ? cell.x < xCenter : cell.x >= xCenter;
            cell.x += direction
        ) {
            // overlap: approximate fraction of the cell that overlaps the
            // grid parallelogram. if the center of the cell is on the outer
            // edge of the border, overlap is zero. If it's on the inner edge,
            // overlap is one.
            double overlap = INFINITY;
            for (int j = 0; j < 4; j++) {
                overlap = min(
                    overlap,
                    dot(add(gridPoints[j], scale(cell, -1)), gridNormals[j])
                        / (2 * borderRadius)
                );
            }

            if (overlap >= 1) {
                if (direction == 1) {
                    cell.x = ribStops[i];
                    direction = -1;
                    continue;
                } else { break; }
            }

            EdgeCell *edgeCells = getEdgeCells(edgeCellCount + 1);
            edgeCells[edgeCellCount].i = i;
            edgeCells[edgeCellCount].right = direction == -1;
            edgeCells[edgeCellCount].overlap = overlap;
            edgeCellCount++;
        }
    }

    EdgeCell *edgeCells = getEdgeCells(edgeCellCount);
    qsort(edgeCells, edgeCellCount, sizeof(EdgeCell), compareEdgeCells);
    if (actualCount - count > edgeCellCount) {
        // my theory is that a borderRadius of 0.5 should be enough to prevent
        // this error from ever occuring
        MessageBox(
            dialog,
            L"Not enough edge cells. "
            L"A developer should increase borderRadius.",
            L"MouseJump error",
            MB_ICONERROR
        );
        exit(1);
    }

    for (int j = 0; j < actualCount - count; j++) {
        if (edgeCells[j].right) {
            ribStops[edgeCells[j].i] -= 1;
        } else {
            ribStarts[edgeCells[j].i] += 1;
        }
    }

    spinesOut.oldSpine = spinesOut.newSpine;
    spinesOut.oldCapacity = spinesOut.newCapacity;
    *oldSpine = spinesOut.oldSpine;

    spinesOut.newSpine.spineStart = spineStart;
    spinesOut.newSpine.spineStop = spineStop;
    spinesOut.newSpine.ribStarts = ribStarts;
    spinesOut.newSpine.ribStops = ribStops;
    spinesOut.newCapacity = spineCapacity;
    *newSpine = spinesOut.newSpine;
}

void destroyCache() {
    DeleteObject(keyBrushOut);
    DeleteObject(keyBitmapOut);
    free(labelTextOut);
    free(labelsOut.value);
    free(sortedLabelsOut.value);
    DeleteObject(labelFontOut);
    free(labelWidthsOut);
    DeleteObject(labelBrushOut);
    DeleteObject(labelBitmapOut.bitmap);
    free(labelBitmapOut.rects);
    DeleteObject(selectionBitmapOut.bitmap);
    free(selectionBitmapOut.rects);
    if (borderPenOut) { DeletePen(borderPenOut); }
    DeleteObject(earBitmapOut);
    free(acceleratorsOut.value);
    if (acceleratorTableOut) { DestroyAcceleratorTable(acceleratorTableOut); }
    DeleteObject(dropdownBitmapOut);
    if (dropdownMenuOut) { DestroyMenu(dropdownMenuOut); }
    free(textBoxTextOut.text);
    free(edgeCellsOut.edgeCells);
    free(spinesOut.oldSpine.ribStarts);
    free(spinesOut.newSpine.ribStarts);
}

typedef struct {
    HWND window;
    HWND dialog;
    COLORREF colorKey;
    Point offsetPt;
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
    double borderPt;
    double earHeightPt;
    double earElevationPt;
    COLORREF borderColor;
    double textBoxWidthPt;
    double dropdownWidthPt;
    double clientHeightPt;
    BOOL showCaption;
    SIZE minTextBoxSize;
    BOOL inMenuLoop;
    LPWSTR text;
} Model;

int getBubbleCount(Model *model, double widthPt, double heightPt) {
    return model->bubbleCount;
}

Point getBubblePositionPt(
    Model *model, double widthPt, double heightPt, int count, int index
) {
    // get the shape of all screen parallelograms (cells) without worrying
    // about scale yet. shape1 and shape2 represent the edges that
    // approximately correspond to the x and y axes respectively.
    // in the windows API in general and in this function specifically, the
    // positive y direction is downward.
    Point shape1, shape2;
    getCellShape(
        model->angle1, model->angle2, model->aspect, &shape1, &shape2
    );
    Spine oldSpine, newSpine;
    getSpines(
        model->angle1, model->angle2, model->aspect, model->offsetPt,
        widthPt, heightPt, count, model->dialog, &oldSpine, &newSpine
    );
    int cellsSeen = 0;
    Point grid;
    for (int i = 0; i < newSpine.spineStop - newSpine.spineStart; i++) {
        grid.y = newSpine.spineStart + i;
        int width = newSpine.ribStops[i] - newSpine.ribStarts[i];
        if (cellsSeen + width > index) {
            grid.x = newSpine.ribStarts[i] + index - cellsSeen;
            break;
        }

        cellsSeen += width;
    }

    // how much to scale each edge so that the number of cells on screen
    // equals count
    double shapeScale = sqrt(widthPt * heightPt / count);
    Point result = add(
        scale(matrixDot(shape1, shape2, grid), shapeScale),
        model->offsetPt
    );
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

typedef struct {
    Point offsetPt;
    UINT dpi;
    int widthPx;
    int heightPx;
    LabelRange labelRange;
    POINT cursorPos;
} Graphics;

Graphics getGraphics(HWND window) {
    Model *model = getModel(window);
    RECT frame;
    GetWindowRect(window, &frame);
    int widthPx = frame.right - frame.left;
    int heightPx = frame.bottom - frame.top;
    UINT dpi = GetDpiForWindow(window);
    double widthPt = intPxToPt(widthPx, dpi);
    double heightPt = intPxToPt(heightPx, dpi);
    StringArray labels = getSortedLabels(
        getBubbleCount(model, widthPt, heightPt)
    );
    LabelRange labelRange = getLabelRange(model->text, labels);
    Point cursorPosPt = getBubblePositionPt(
        model,
        widthPt,
        heightPt,
        labels.count,
        labelRange.start
    );
    POINT cursorPos = {
        .x = ptToIntPx(cursorPosPt.x, dpi),
        .y = ptToIntPx(cursorPosPt.y, dpi),
    };
    ClientToScreen(model->window, &cursorPos);
    Graphics graphics = {
        .offsetPt = model->offsetPt,
        .dpi = dpi,
        .widthPx = widthPx,
        .heightPx = heightPx,
        .labelRange = getLabelRange(model->text, labels),
        .cursorPos = cursorPos,
    };
    return graphics;
}

Graphics lastGraphics;
POINT naturalCursorPos = { .x = 0, .y = 0 };;
void redraw(HWND window) {
    Graphics graphics = getGraphics(window);
    if (!memcmp(&graphics, &lastGraphics, sizeof(graphics))) {
        return;
    }

    Model *model = getModel(window);
    double widthPt = intPxToPt(graphics.widthPx, graphics.dpi);
    double heightPt = intPxToPt(graphics.heightPx, graphics.dpi);
    int bubbleCount = getBubbleCount(model, widthPt, heightPt);
    StringArray labels = getSortedLabels(bubbleCount);
    if (
        memcmp(
            &graphics.labelRange, &lastGraphics.labelRange, sizeof(LabelRange)
        ) || memcmp(
            &graphics.cursorPos, &lastGraphics.cursorPos, sizeof(POINT)
        )
    ) {
        POINT cursorPos;
        GetCursorPos(&cursorPos);
        BOOL isNatural = lastGraphics.labelRange.matchLength <= 0 || memcmp(
            &cursorPos, &lastGraphics.cursorPos, sizeof(cursorPos)
        );
        if (graphics.labelRange.matchLength > 0) {
            if (isNatural) { naturalCursorPos = cursorPos; }
            SetCursorPos(graphics.cursorPos.x, graphics.cursorPos.y);
        } else if (!isNatural) {
            SetCursorPos(naturalCursorPos.x, naturalCursorPos.y);
        }
    }

    lastGraphics = graphics;
    HDC device = GetDC(window);
    HDC memory = CreateCompatibleDC(device);
    HBRUSH keyBrush = getKeyBrush(model->colorKey);
    selectKeyBitmap(
        device, memory, graphics.widthPx, graphics.heightPx, model->colorKey
    );
    HDC labelMemory = CreateCompatibleDC(device);
    SelectObject(
        labelMemory,
        getLabelFont(graphics.dpi, model->fontHeightPt, model->fontFamily)
    );
    SetBkColor(labelMemory, model->labelBackground);
    SetTextColor(labelMemory, getTextColor(model->labelBackground));
    RECT paddingPx = {
        ptToThinPx(model->paddingLeftPt, graphics.dpi),
        ptToThinPx(model->paddingTopPt, graphics.dpi),
        ptToThinPx(model->paddingRightPt, graphics.dpi),
        ptToThinPx(model->paddingBottomPt, graphics.dpi),
    };
    RECT *labelRects = selectLabelBitmap(
        device, labelMemory, bubbleCount, paddingPx
    );
    HDC selectionMemory = CreateCompatibleDC(device);
    SelectObject(
        selectionMemory,
        getLabelFont(graphics.dpi, model->fontHeightPt, model->fontFamily)
    );
    SetBkColor(selectionMemory, model->selectionBackground);
    SetTextColor(selectionMemory, getTextColor(model->selectionBackground));
    RECT *selectionRects = selectSelectionBitmap(
        device, selectionMemory, bubbleCount
    );
    HDC earMemory = CreateCompatibleDC(device);
    int borderPx = ptToThinPx(model->borderPt, graphics.dpi);
    int earElevationPx = ptToIntPx(model->earElevationPt, graphics.dpi);
    SIZE earSize = selectEarBitmap(
        device,
        earMemory,
        borderPx,
        earElevationPx,
        ptToIntPx(model->earHeightPt, graphics.dpi),
        model->colorKey,
        model->borderColor,
        model->labelBackground
    );
    // RECT labelBitmapRect = {
    //     ptToIntPx(model->offsetPt.x, graphics.dpi) + 100,
    //     ptToIntPx(model->offsetPt.y, graphics.dpi) + 400,
    //     ptToIntPx(model->offsetPt.x, graphics.dpi) + 100 + 600,
    //     ptToIntPx(model->offsetPt.y, graphics.dpi) + 400 + 400,
    // };
    // BitBlt(
    //     memory, labelBitmapRect.left, labelBitmapRect.top,
    //     labelBitmapRect.right - labelBitmapRect.left,
    //     labelBitmapRect.bottom - labelBitmapRect.top,
    //     labelMemory, 0, 0, SRCCOPY
    // );
    RECT client = { 0, 0, graphics.widthPx, graphics.heightPx };
    for (
        int i = graphics.labelRange.start; i < graphics.labelRange.stop; i++
    ) {
        Point positionPt = getBubblePositionPt(
            model, widthPt, heightPt, labels.count, i
        );
        POINT positionPx = {
            .x = ptToIntPx(positionPt.x, graphics.dpi),
            .y = ptToIntPx(positionPt.y, graphics.dpi),
        };
        clampToRect(client, &positionPx);
        BitBlt(
            memory,
            positionPx.x,
            positionPx.y,
            earSize.cx,
            earSize.cy,
            earMemory,
            0,
            0,
            SRCCOPY
        );
        BitBlt(
            memory,
            positionPx.x + borderPx,
            positionPx.y + earElevationPx,
            labelRects[i].right - labelRects[i].left,
            labelRects[i].bottom - labelRects[i].top,
            labelMemory,
            labelRects[i].left,
            labelRects[i].top,
            SRCCOPY
        );
        if (graphics.labelRange.matchLength) {
            RECT textRect = { 0, 0, 0, 0 };
            DrawText(
                labelMemory,
                labels.value[i],
                graphics.labelRange.matchLength,
                &textRect,
                DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX
            );
            BitBlt(
                memory,
                positionPx.x + borderPx + paddingPx.left,
                positionPx.y + earElevationPx + paddingPx.top,
                min(
                    selectionRects[i].right - selectionRects[i].left,
                    textRect.right
                ),
                selectionRects[i].bottom - selectionRects[i].top,
                selectionMemory,
                selectionRects[i].left,
                selectionRects[i].top,
                SRCCOPY
            );
        }
    }
    DeleteDC(selectionMemory);
    DeleteDC(labelMemory);
    DeleteDC(earMemory);
    ReleaseDC(window, device);
    POINT origin = {0, 0};
    SIZE frameSize = { graphics.widthPx, graphics.heightPx };
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
    for (
        int i = graphics.labelRange.start; i < graphics.labelRange.stop; i++
    ) {
        Point positionPt = getBubblePositionPt(
            model, widthPt, heightPt, labels.count, i
        );
        POINT positionPx = {
            .x = ptToIntPx(positionPt.x, graphics.dpi),
            .y = ptToIntPx(positionPt.y, graphics.dpi),
        };
        clampToRect(client, &positionPx);
        RECT dstRect = labelRects[i];
        OffsetRect(
            &dstRect,
            positionPx.x + borderPx - dstRect.left,
            positionPx.y + earElevationPx - dstRect.top
        );
        FillRect(memory, &dstRect, keyBrush);
        RECT earRect = {
            .left = positionPx.x,
            .top = positionPx.y
        };
        earRect.right = earRect.left + earSize.cx;
        earRect.bottom = earRect.top + earSize.cy;
        FillRect(memory, &earRect, keyBrush);
    }

    // FillRect(memory, &labelBitmapRect, keyBrush);

    DeleteDC(memory);
}

BOOL skipHitTest = FALSE;

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
            Model *model = getModel(window);
            // The main window initially receives WM_ACTIVATE before its user
            // data is populated
            if (model) { SetActiveWindow(model->dialog); }
            return 0;
        }
    } else if (message == WM_NCHITTEST && skipHitTest) {
        return HTTRANSPARENT;
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

DWORD getFinalStyle(HWND dialog) {
    DWORD style = GetWindowLongPtr(dialog, GWL_STYLE);
    Model *model = getModel(dialog);
    if (getModel(dialog)->showCaption) {
        return style | WS_THICKFRAME | WS_CAPTION;
    } else {
        return style & ~WS_THICKFRAME & ~WS_CAPTION;
    }
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

    minDialogSizeIn = in;
    RECT frame = { 0, 0, in.clientSize.cx, in.clientSize.cy };
    AdjustWindowRectEx(
        &frame,
        getFinalStyle(dialog),
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
const int ACTIVATE_WINDOW_TIMER = 2;
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
        SetWindowLongPtr(dialog, GWL_STYLE, getFinalStyle(dialog));
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
        } else if (wParam == ACTIVATE_WINDOW_TIMER) {
            KillTimer(dialog, wParam);
            SetForegroundWindow(dialog);
            return TRUE;
        }
    } else if (message == WM_NCHITTEST && skipHitTest) {
        return HTTRANSPARENT;
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
                model->offsetPt.x += xDirection * pxToPt(model->deltaPx, dpi);
                model->offsetPt.y += yDirection * pxToPt(model->deltaPx, dpi);
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
                model->offsetPt.x += xDirection * pxToPt(
                    model->smallDeltaPx, dpi
                );
                model->offsetPt.y += yDirection * pxToPt(
                    model->smallDeltaPx, dpi
                );
                RedrawWindow(model->window, NULL, NULL, RDW_INTERNALPAINT);
                return TRUE;
            } else if (LOWORD(wParam) == IDM_CLICK) {
                POINT cursor;
                GetCursorPos(&cursor);
                skipHitTest = TRUE;
                HWND target = WindowFromPoint(cursor);
                skipHitTest = FALSE;
                if (target) { SetForegroundWindow(target); }

                INPUT click[2] = {
                    {
                        .type = INPUT_MOUSE,
                        .mi = {
                            .dx = 0,
                            .dy = 0,
                            .mouseData = 0,
                            .dwFlags = MOUSEEVENTF_LEFTDOWN,
                            .time = 0,
                            .dwExtraInfo = 0,
                        },
                    },
                    {
                        .type = INPUT_MOUSE,
                        .mi = {
                            .dx = 0,
                            .dy = 0,
                            .mouseData = 0,
                            .dwFlags = MOUSEEVENTF_LEFTUP,
                            .time = 0,
                            .dwExtraInfo = 0,
                        },
                    },
                };
                SendInput(2, click, sizeof(INPUT));
                SetDlgItemText(dialog, IDC_TEXTBOX, L"");
                SetTimer(dialog, ACTIVATE_WINDOW_TIMER, 100, NULL);
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
            } else if (HIWORD(wParam) == EN_CHANGE) {
                redraw(getModel(dialog)->window);
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

    Model model = {
        .colorKey = RGB(255, 0, 255),
        .offsetPt = { 0, 0 },
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
        .borderPt = .75,
        .earHeightPt = 6,
        .earElevationPt = 4,
        .borderColor = RGB(0, 0, 0),
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

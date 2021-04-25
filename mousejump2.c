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

#pragma region brushesAndPens

const int KEY_BRUSH_SLOT = 0;
const int LABEL_BRUSH_SLOT = 1;
#define BRUSH_SLOT_COUNT 2
COLORREF brushesIn[BRUSH_SLOT_COUNT];
HBRUSH brushesOut[BRUSH_SLOT_COUNT] = { NULL };
HBRUSH getBrush(COLORREF color, int slot) {
    if (brushesOut[slot]) {
        if (color == brushesIn[slot]) { return brushesOut[slot]; }
        DeleteObject(brushesOut[slot]);
    }

    return brushesOut[slot] = CreateSolidBrush(brushesIn[slot] = color);
}

const int BORDER_PEN_SLOT = 0;
const int DRAG_PEN_SLOT = 1;
const int ALT_DRAG_PEN_SLOT = 2;
const int ERASE_DRAG_PEN_SLOT = 3;
#define PEN_SLOT_COUNT 4

const int SOLID_PEN_STYLE = 0;
const int DASH_PEN_STYLE = 1;
const int ALT_DASH_PEN_STYLE = 2;
const int BOTH_DASH_PEN_STYLE = 3;
typedef struct { COLORREF color; int width, style, dashLength; } PenIn;
PenIn pensIn[PEN_SLOT_COUNT];
HPEN pensOut[PEN_SLOT_COUNT] = { NULL };
HPEN getPen(COLORREF color, int width, int style, int dashLength, int slot) {
    PenIn in;
    ZeroMemory(&in, sizeof(in));
    in.color = color;
    in.width = width;
    in.style = style;
    in.dashLength = dashLength;
    if (pensOut[slot]) {
        if (!memcmp(&in, &pensOut[slot], sizeof(in))) {
            return pensOut[slot];
        }

        DeletePen(pensOut[slot]);
    }

    ZeroMemory(&pensIn[slot], sizeof(in));
    pensIn[slot] = in;
    LOGBRUSH brush = { .lbStyle = BS_SOLID, .lbColor = color };
    DWORD dashCount = 0;
    DWORD dashLengths[3] = { dashLength, 0, 0 };
    if (style == DASH_PEN_STYLE) { dashCount = 1; }
    else if (style == ALT_DASH_PEN_STYLE) {
        dashCount = 3;
        dashLengths[0] = 0;
        dashLengths[1] = dashLength;
    } else if (style == BOTH_DASH_PEN_STYLE) { dashCount = 2; }
    return pensOut[slot] = ExtCreatePen(
        PS_GEOMETRIC | PS_ENDCAP_FLAT | PS_JOIN_MITER | (
            style == SOLID_PEN_STYLE ? PS_SOLID : PS_USERSTYLE
        ),
        width,
        &brush,
        dashCount,
        style == SOLID_PEN_STYLE ? NULL : dashLengths
    );
}

#pragma endregion

typedef struct { int width; int height; COLORREF color; } KeyBitmapIn;
KeyBitmapIn keyBitmapIn = { .width = 0, .height = 0 };
HBITMAP keyBitmapOut = NULL;
void selectKeyBitmap(
    HDC device, HDC memory, int width, int height, COLORREF color
) {
    // please clear the bitmap when you are done with it!
    KeyBitmapIn in;
    ZeroMemory(&in, sizeof(in));
    in.width = max(width, keyBitmapIn.width);
    in.height = max(height, keyBitmapIn.height);
    in.color = color;
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
    FillRect(memory, &rect, getBrush(color, KEY_BRUSH_SLOT));
}

#pragma region labels

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

    LabelRange result;
    ZeroMemory(&result, sizeof(result));
    result.start = start;
    result.stop = stop;
    result.matchLength = matchLength;
    return result;
}

#pragma endregion

#pragma region selectLabelBitmap

typedef struct {
    UINT dpi;
    double heightPt;
    WCHAR family[LF_FACESIZE];
    int systemFontChanges;
} LabelFontIn;
LabelFontIn labelFontIn;
HFONT labelFontOut = NULL;
HFONT getLabelFont(
    UINT dpi,
    double heightPt,
    WCHAR family[LF_FACESIZE],
    int systemFontChanges
) {
    LabelFontIn in;
    ZeroMemory(&in, sizeof(in));
    in.dpi = dpi;
    in.heightPt = heightPt;
    wcsncpy(in.family, family, LF_FACESIZE);
    in.systemFontChanges = systemFontChanges;
    if (labelFontOut) {
        if (!memcmp(&in, &labelFontIn, sizeof(in))) { return labelFontOut; }
        DeleteObject(labelFontOut);
    }

    ZeroMemory(&labelFontIn, sizeof(in));
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
    LabelWidthsIn in;
    ZeroMemory(&in, sizeof(in));
    in.count = count;
    GetObject(GetCurrentObject(device, OBJ_FONT), sizeof(in.font), &in.font);
    if (labelWidthsOut && !memcmp(&in, &labelWidthsIn, sizeof(in))) {
        return labelWidthsOut;
    }

    ZeroMemory(&labelWidthsIn, sizeof(in));
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
    LabelBitmapIn in;
    ZeroMemory(&labelFontIn, sizeof(in));
    in.count = count;
    in.paddingPx = paddingPx;
    in.foreground = GetTextColor(memory);
    in.background = GetBkColor(memory);
    GetObject(GetCurrentObject(memory, OBJ_FONT), sizeof(in.font), &in.font);
    if (labelBitmapOut->bitmap) {
        if (!memcmp(&in, labelBitmapIn, sizeof(in))) {
            SelectObject(memory, labelBitmapOut->bitmap);
            return labelBitmapOut->rects;
        }

        DeleteObject(labelBitmapOut->bitmap);
    }

    ZeroMemory(labelBitmapIn, sizeof(in));
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
        HBRUSH brush = getBrush(GetBkColor(memory), LABEL_BRUSH_SLOT);
        for (int y = 0; y < height; y += metrics.tmHeight + yPadding) {
            RECT separator = { 0, y, width, y + yPadding };
            FillRect(memory, &separator, brush);
        }
    }

    if (xPadding > 0) {
        HBRUSH brush = getBrush(GetBkColor(memory), LABEL_BRUSH_SLOT);
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
    EarBitmapIn in;
    ZeroMemory(&in, sizeof(in));
    in.borderPx = borderPx;
    in.offsetPx = offsetPx;
    in.heightPx = heightPx;
    in.keyColor = keyColor;
    in.borderColor = borderColor;
    in.backgroundColor = backgroundColor;
    if (earBitmapOut) {
        if (!memcmp(&in, &earBitmapIn, sizeof(in))) {
            SelectObject(memory, earBitmapOut);
            return size;
        }

        DeleteObject(earBitmapOut);
    }

    ZeroMemory(&earBitmapIn, sizeof(in));
    earBitmapIn = in;
    earBitmapOut = CreateCompatibleBitmap(device, 2 * offsetPx, 2 * heightPx);
    SelectObject(memory, earBitmapOut);
    RECT rect = { 0, 0, 2 * offsetPx, 2 * heightPx };
    FillRect(memory, &rect, getBrush(keyColor, KEY_BRUSH_SLOT));
    SelectObject(
        memory,
        getPen(borderColor, borderPx, SOLID_PEN_STYLE, 0, BORDER_PEN_SLOT)
    );
    SelectObject(memory, getBrush(backgroundColor, LABEL_BRUSH_SLOT));
    int borderDiagonalPx = (int)round(borderPx * sqrt2);
    int xyOffset = borderPx / 2;
    int yOffset = (borderDiagonalPx - 1) / 2;
    for (int i = 0; i < 4; i++) {
        POINT points[3] = {
            { xyOffset, heightPx + xyOffset },
            { xyOffset, xyOffset + yOffset },
            { heightPx + xyOffset - yOffset, heightPx + xyOffset },
        };
        if (i % 2) {
            for (int j = 0; j < 3; j++) {
                points[j].x = rect.right - 1 - points[j].x;
            }
        } if (i >= 2) {
            for (int j = 0; j < 3; j++) {
                points[j].y = rect.bottom - 1 - points[j].y;
            }
        }

        Polygon(memory, points, 3);
    }

    rect.left = borderPx;
    rect.top = offsetPx;
    rect.right = 2 * offsetPx - borderPx;
    rect.bottom = 2 * heightPx - offsetPx;
    FillRect(memory, &rect, getBrush(keyColor, KEY_BRUSH_SLOT));
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
ArrowKeyMap getArrowKeyMapHelp(int left, int up, int right, int down) {
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

ArrowKeyMap arrowKeyMapOut[2] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
ArrowKeyMap getArrowKeyMap(BOOL slow) {
    if (arrowKeyMapOut[slow].left) { return arrowKeyMapOut[slow]; }
    return arrowKeyMapOut[slow] = slow ? getArrowKeyMapHelp(
        IDM_SLIGHTLY_LEFT, IDM_SLIGHTLY_UP,
        IDM_SLIGHTLY_RIGHT, IDM_SLIGHTLY_DOWN
    ) : getArrowKeyMapHelp(
        IDM_LEFT, IDM_UP, IDM_RIGHT, IDM_DOWN
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

#pragma region getBubblePositionPt

typedef struct { double x, y; } Point;
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

typedef struct { double angle1, angle2, aspect, area; } CellEdgesIn;
CellEdgesIn cellEdgesIn = { 0, PI, 1, 1 };
typedef struct { Point shape1; Point shape2; } CellEdgesOut;
CellEdgesOut cellEdgesOut = { { 1, 0 }, { 0, 1 } };
void getCellEdges(
    double angle1, double angle2, double aspect, double area,
    Point *shape1, Point *shape2
) {
    CellEdgesIn in;
    ZeroMemory(&in, sizeof(in));
    in.angle1 = angle1;
    in.angle2 = angle2;
    in.aspect = aspect;
    in.area = area;
    if (memcmp(&in, &cellEdgesIn, sizeof(in))) {
        ZeroMemory(&cellEdgesIn, sizeof(in));
        cellEdgesIn = in;
        cellEdgesOut.shape1 = makePoint(cos(angle1) * aspect, sin(angle1));
        cellEdgesOut.shape2 = makePoint(cos(angle2) * aspect, sin(angle2));
        double shapeScale = sqrt(
            area / determinant(cellEdgesOut.shape1, cellEdgesOut.shape2)
        );
        cellEdgesOut.shape1 = scale(cellEdgesOut.shape1, shapeScale);
        cellEdgesOut.shape2 = scale(cellEdgesOut.shape2, shapeScale);
    }

    *shape1 = cellEdgesOut.shape1;
    *shape2 = cellEdgesOut.shape2;
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

double getParallelogramOverlap(
    Point cell,
    Point *edgePoints,
    Point *normals,
    double borderRadius,
    int iStart,
    int iStop
) {
    // returns the approximate fraction of the grid cell that overlaps the
    // given parallelogram.
    // cell: center of the grid cell
    // edgePoints: one point on each of the 4 sides
    // normals: normals for each of the 4 sides
    // borderRadius: half the width of the border. this is an inner border,
    //               meaning it does not extend outside the given edges.
    // if the center of the cell is on the outer edge of the border, overlap
    // is zero. If it's on the inner edge, overlap is one.
    double overlap = INFINITY;
    for (int i = iStart; i < iStop; i++) {
        overlap = min(
            overlap,
            dot(add(edgePoints[i], scale(cell, -1)), normals[i])
                / (2 * borderRadius)
        );
    }

    return overlap;
}

typedef struct { int start, stop; int *ribStarts, *ribStops; } Spine;
BOOL spineContains(Spine spine, Point point) {
    return point.y >= spine.start
        && point.y < spine.stop
        && point.x >= spine.ribStarts[(int)point.y - spine.start]
        && point.x < spine.ribStops[(int)point.y - spine.start];
}

struct {
    Spine oldSpine, newSpine;
    int oldCapacity, newCapacity;
} spineOut = {
    .oldSpine = { 0, 0, NULL, NULL },
    .newSpine = { 0, 0, NULL, NULL },
    .oldCapacity = 0,
    .newCapacity = 0,
};
Spine getSpine(
    Point edge1, Point edge2, Point offset, double width, double height,
    int count, Spine oldSpine, int cliffStart, int cliffStop, HWND dialog
) {
    double inverseScale = 1 / determinant(edge1, edge2);
    // inverse1 and inverse2 are edges of a 1pt x 1pt square, projected into
    // grid space (in other words, they are the columns of the inverse of the
    // matrix whose columns are the edges of a screen parallelogram).
    Point inverse1 = scale(makePoint(edge2.y, -edge1.y), inverseScale);
    Point inverse2 = scale(makePoint(-edge2.x, edge1.x), inverseScale);
    Point gridOffset = matrixDot(inverse1, inverse2, scale(offset, -1));
    // the grid parallelogram is the entire screen area projected into grid
    // space
    Point gridEdge1 = scale(inverse1, width);
    Point gridEdge2 = scale(inverse2, height);
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

    int start = minN(spineCandidates, 4);
    int stop = maxN(spineCandidates, 4);
    int *ribStarts = spineOut.oldSpine.ribStarts;
    int spineCapacity = max(
        spineOut.oldCapacity,
        nextPowerOf2(2 * (stop - start), 64)
    );
    if (spineCapacity > spineOut.oldCapacity) {
        ribStarts = realloc(ribStarts, spineCapacity * sizeof(int));
    }

    int *ribStops = ribStarts + (stop - start);
    Point rowNormal = { 0, 1 };
    Point cell = { 0, start };
    int actualCount = 0;
    int edgeCellCount = 0;
    for (int i = 0; i < stop - start; i++) {
        cell.y = start + i;
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
        ribStarts[i] = min(xCenter, (int)ceil(xEntry));
        ribStops[i] = max(xCenter, (int)ceil(xExit));
        actualCount += ribStops[i] - ribStarts[i];
        for (cell.x = ribStarts[i]; cell.x < xCenter; cell.x++) {
            int iStart = 0, iStop = 4;
            if (spineContains(oldSpine, cell)) {
                // only remove cells from the previous spine if they fell off
                // the cliff. this prevents churn near the screen edges
                // parallel to the direction of motion.
                iStart = cliffStart; iStop = cliffStop;
            }

            double overlap = getParallelogramOverlap(
                cell, gridPoints, gridNormals, borderRadius, iStart, iStop
            );
            if (overlap >= 1) { break; }
            EdgeCell edgeCell = { i, FALSE, overlap };
            getEdgeCells(edgeCellCount + 1)[edgeCellCount] = edgeCell;
            edgeCellCount++;
        }

        for (cell.x = ribStops[i] - 1; cell.x >= xCenter; cell.x--) {
            int iStart = 0, iStop = 4;
            if (spineContains(oldSpine, cell)) {
                iStart = cliffStart; iStop = cliffStop;
            }

            double overlap = getParallelogramOverlap(
                cell, gridPoints, gridNormals, borderRadius, iStart, iStop
            );
            if (overlap >= 1) { break; }
            EdgeCell edgeCell = { i, TRUE, overlap };
            getEdgeCells(edgeCellCount + 1)[edgeCellCount] = edgeCell;
            edgeCellCount++;
        }
    }

    EdgeCell *edgeCells = getEdgeCells(edgeCellCount);
    qsort(edgeCells, edgeCellCount, sizeof(EdgeCell), compareEdgeCells);
    if (actualCount - count > edgeCellCount) {
        // I convinced myself that a borderRadius of 0.5 should be enough to
        // prevent this error from ever occuring, but that was before the
        // cliffStart and cliffStop parameters were added to reduce label
        // churn.
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

    spineOut.oldSpine = spineOut.newSpine;
    spineOut.oldCapacity = spineOut.newCapacity;

    spineOut.newSpine.start = start;
    spineOut.newSpine.stop = stop;
    spineOut.newSpine.ribStarts = ribStarts;
    spineOut.newSpine.ribStops = ribStops;
    spineOut.newCapacity = spineCapacity;

    return spineOut.newSpine;
}

typedef struct { int index; double score; } ScoredIndex;
int compareScoredIndices(const ScoredIndex *a, const ScoredIndex *b) {
    if (a->score > b->score) { return 1; }
    if (a->score < b->score) { return -1; }
    if (a->index > b->index) { return 1; }
    if (a->index < b->index) { return -1; }
    return 0;
}

typedef struct { Point point; double score; } ScoredPoint;
int compareScoredPoints(const ScoredPoint *a, const ScoredPoint *b) {
    if (a->score > b->score) { return 1; }
    if (a->score < b->score) { return -1; }
    if (a->point.y > b->point.y) { return 1; }
    if (a->point.y < b->point.y) { return -1; }
    if (a->point.x > b->point.x) { return 1; }
    if (a->point.x < b->point.x) { return -1; }
    return 0;
}

typedef struct {
    Point edge1, edge2, offset;
    double width, height;
    int count;
} BubblesIn;
BubblesIn bubblesIn = { { 0, 0 }, { 0, 0 }, 0, 0, 0 };
struct {
    Point negativeOffset;
    Spine spine;
    Point *bubbles;
    ScoredIndex *removed;
    ScoredPoint *added;
    int capacity;
} bubblesOut = {
    .negativeOffset = { 0, 0 },
    .spine = { 0, 0, NULL, NULL },
    .bubbles = NULL,
    .removed = NULL,
    .added = NULL,
    .capacity = 0,
};
Point *getBubbles(
    Point edge1, Point edge2, Point offset, double width, double height,
    int count, HWND dialog
) {
    BubblesIn in;
    ZeroMemory(&in, sizeof(in));
    in.edge1 = edge1;
    in.edge2 = edge2;
    in.offset = offset;
    in.width = width;
    in.height = height;
    in.count = count;
    if (!memcmp(&in, &bubblesIn, sizeof(in))) {
        return bubblesOut.bubbles;
    }

    Point negativeOffset = bubblesOut.negativeOffset;
    bubblesOut.negativeOffset = scale(offset, -1);
    Point delta = add(offset, negativeOffset);

    // the cliff is the range of screen edges, indexed counterclockwise
    // with top = 0, that labels are moving towards and "falling off of".
    int cliffStart = 0;
    int cliffStop = 4;
    // specify a cliff only if nothing but offset changed.
    bubblesIn.offset = in.offset;
    if (!memcmp(&in, &bubblesIn, sizeof(in))) {
        int cliffIndices[3][3] = {
            {1, 0, 7},
            {2, 8, 6},
            {3, 4, 5},
        };
        int cliffIndex = cliffIndices[
            1 + (delta.y > 0) - (delta.y < 0)
        ][
            1 + (delta.x > 0) - (delta.x < 0)
        ];
        cliffStart = cliffIndex / 2;
        cliffStop = (cliffIndex + 3) / 2; // values up to 5 are allowed
        if (cliffIndex == 8) { cliffStart = cliffStop = 0; }
    }

    ZeroMemory(&bubblesIn, sizeof(in));
    bubblesIn = in;

    Spine oldSpine = bubblesOut.spine;
    int oldCount = 0;
    for (int i = 0; i < oldSpine.stop - oldSpine.start; i++) {
        oldCount += oldSpine.ribStops[i] - oldSpine.ribStarts[i];
    }

    Spine spine = bubblesOut.spine = getSpine(
        edge1, edge2, offset, width, height, count,
        oldSpine, cliffStart, cliffStop, dialog
    );

    Point *bubbles = bubblesOut.bubbles;
    ScoredIndex *removed = bubblesOut.removed;
    ScoredPoint *added = bubblesOut.added;
    int capacity = max(bubblesOut.capacity, nextPowerOf2(count, 64));
    if (capacity > bubblesOut.capacity) {
        bubblesOut.capacity = capacity;
        bubbles = bubblesOut.bubbles = realloc(
            bubblesOut.bubbles, capacity * sizeof(Point)
        );
        removed = bubblesOut.removed = realloc(
            bubblesOut.removed, capacity * sizeof(ScoredIndex)
        );
        added = bubblesOut.added = realloc(
            bubblesOut.added, capacity * sizeof(ScoredPoint)
        );
    }

    int removedCount = 0;
    for (int i = 0; i < min(oldCount, count); i++) {
        if (!spineContains(spine, bubbles[i])) {
            Point screen = add(
                matrixDot(edge1, edge2, bubbles[i]), negativeOffset
            );
            double time = INFINITY;
            if (delta.x != 0) {
                time = min(
                    time, (width * (delta.x >= 0) - screen.x) / delta.x
                );
            } if (delta.y != 0) {
                time = min(
                    time, (height * (delta.y >= 0) - screen.y) / delta.y
                );
            }

            removed[removedCount].index = i;
            removed[removedCount].score = time;
            removedCount++;
        }
    }

    qsort(removed, removedCount, sizeof(ScoredIndex), compareScoredIndices);

    int addedCount = 0;
    for (int y = spine.start; y < spine.stop; y++) {
        int ribStart = spine.ribStarts[y - spine.start];
        int ribStop = spine.ribStops[y - spine.start];
        int holeStart = 0;
        int holeStop = 0;
        if (y >= oldSpine.start && y < oldSpine.stop) {
            holeStart = oldSpine.ribStarts[y - oldSpine.start];
            holeStop = oldSpine.ribStops[y - oldSpine.start];
        }

        for (int x = ribStart; x < ribStop; x++) {
            if (x >= holeStart && x < holeStop) {
                x = holeStop - 1;
                continue;
            }

            Point bubble = { x, y };
            Point screen = add(
                matrixDot(edge1, edge2, bubble), negativeOffset
            );
            double time = -INFINITY;
            if (delta.x != 0) {
                time = max(
                    time, (width * (delta.x < 0) - screen.x) / delta.x
                );
            } if (delta.y != 0) {
                time = max(
                    time, (height * (delta.y < 0) - screen.y) / delta.y
                );
            }

            added[addedCount].point = bubble;
            added[addedCount].score = time;
            addedCount++;
        }
    }

    qsort(added, addedCount, sizeof(ScoredPoint), compareScoredPoints);

    for (int i = count; i < oldCount; i++) {
        if (spineContains(spine, bubbles[i])) {
            added[addedCount].point = bubbles[i];
            addedCount++;
        }
    }

    // if the number of labels has increased, shuffle the new ones
    for (int i = removedCount; i < addedCount; i++) {
        removed[i].index = oldCount + i - removedCount;
    }

    for (int i = removedCount; i < addedCount - 1; i++) {
        int j = i + rand() % (addedCount - i);
        int tmp = removed[i].index;
        removed[i].index = removed[j].index;
        removed[j].index = tmp;
    }

    for (int i = 0; i < addedCount; i++) {
        bubbles[removed[i].index] = added[i].point;
    }

    return bubbles;
}

#pragma endregion
Point getBubblePositionPt(
    Point marginSize, Point edge1, Point edge2, Point offsetPt, HWND dialog,
    double width, double height, int count, int index
) {
    width += 2 * marginSize.x;
    height += 2 * marginSize.y;
    Point *bubbles = getBubbles(
        edge1, edge2, offsetPt, width, height, count, dialog
    );
    Point result = add(
        matrixDot(edge1, edge2, bubbles[index]),
        add(offsetPt, scale(marginSize, -1))
    );
    return result;
}

int getBubbleCount(
    int maxCount, double minCellArea, double gridMargin, double aspect,
    double width, double height
) {
    return min(
        maxCount,
        width * height / minCellArea + 2 * gridMargin * (
            2 * gridMargin + (width + height * aspect) / sqrt(
                aspect * minCellArea
            )
        )
    );
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

void destroyCache() {
    for (int i = 0; i < BRUSH_SLOT_COUNT; i++) {
        DeleteObject(brushesOut[i]);
    }

    for (int i = 0; i < PEN_SLOT_COUNT; i++) {
        if (pensOut[i]) { DeletePen(pensOut[i]); }
    }

    DeleteObject(keyBitmapOut);
    free(labelTextOut);
    free(labelsOut.value);
    free(sortedLabelsOut.value);
    DeleteObject(labelFontOut);
    free(labelWidthsOut);
    DeleteObject(labelBitmapOut.bitmap);
    free(labelBitmapOut.rects);
    DeleteObject(selectionBitmapOut.bitmap);
    free(selectionBitmapOut.rects);
    DeleteObject(earBitmapOut);
    free(acceleratorsOut.value);
    if (acceleratorTableOut) { DestroyAcceleratorTable(acceleratorTableOut); }
    DeleteObject(dropdownBitmapOut);
    if (dropdownMenuOut) { DestroyMenu(dropdownMenuOut); }
    free(textBoxTextOut.text);
    free(edgeCellsOut.edgeCells);
    free(spineOut.oldSpine.ribStarts);
    free(spineOut.newSpine.ribStarts);
    free(bubblesOut.bubbles);
    free(bubblesOut.added);
    free(bubblesOut.removed);
}

typedef struct {
    HWND window;
    HWND dialog;
    HMONITOR monitor;
    COLORREF colorKey;
    Point offsetPt;
    double deltaPx;
    double smallDeltaPx;
    int bubbleCount;
    double minCellArea;
    double gridMargin;
    double aspect;
    double angle1;
    double angle2;
    double fontHeightPt;
    WCHAR fontFamily[LF_FACESIZE];
    int systemFontChanges;
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
    double dashPt;
    double mirrorWidthPt;
    double mirrorHeightPt;
    double textBoxWidthPt;
    double dropdownWidthPt;
    double clientHeightPt;
    BOOL showCaption;
    SIZE minTextBoxSize;
    BOOL inMenuLoop;
    LPWSTR text;
    POINT naturalPoint;
    POINT matchPoint;
    BOOL hasMatch;
    int dragCount;
    POINT drag[3];
} Model;

double sqrtAspectIn = 0;
double sqrtAspectOut = 0;
double getSqrtAspect(double aspect) {
    if (aspect == sqrtAspectIn) { return sqrtAspectOut; }
    return sqrtAspectOut = sqrt(aspect);
}

typedef struct { double width, height, margin; int count; } SqrtCellAreaIn;
SqrtCellAreaIn sqrtCellAreaIn = { 0, 0, 0, 0 };
double sqrtCellAreaOut = 0;
double getSqrtCellArea(
    double aspectWidth, double aspectHeight, double margin, int count
) {
    SqrtCellAreaIn in;
    ZeroMemory(&in, sizeof(in));
    in.width = aspectWidth;
    in.height = aspectHeight;
    in.margin = margin;
    in.count = count;
    if (!memcmp(&in, &sqrtCellAreaIn, sizeof(in))) { return sqrtCellAreaOut; }
    ZeroMemory(&sqrtCellAreaIn, sizeof(in));
    sqrtCellAreaIn = in;
    double temp = margin * (aspectWidth - aspectHeight);
    sqrtCellAreaOut = (
        margin * (aspectWidth + aspectHeight) + sqrt(
            temp * temp + count * aspectWidth * aspectHeight
        )
    ) / (count - 4 * margin * margin);
    return sqrtCellAreaOut;
}

typedef struct {
    HWND window, dialog;
    Point offsetPt;
    UINT dpi;
    double fontHeightPt;
    WCHAR fontFamily[LF_FACESIZE];
    int systemFontChanges;
    int leftPx, topPx, widthPx, heightPx;
    int topCrop, bottomCrop;
    int bubbleCount;
    LabelRange labelRange;
    Point marginSize, edge1, edge2;
    COLORREF colorKey, labelBackground, selectionBackground, borderColor;
    RECT paddingPx;
    double borderPx, earHeightPx, earElevationPx, dashPx;
    POINT mirrorStart;
    POINT matchPoint;
    int dragCount;
    POINT drag[3];
} Graphics;
Graphics graphicsOut;
Graphics *getGraphics(Model *model) {
    MONITORINFO monitorInfo;
    ZeroMemory(&monitorInfo, sizeof(monitorInfo));
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfo(model->monitor, &monitorInfo)) {
        POINT origin = { 0, 0 };
        model->monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
        GetMonitorInfo(model->monitor, &monitorInfo);
    }

    int topCrop = 1;
    int bottomCrop = 0;
    if (
        monitorInfo.rcWork.top - monitorInfo.rcMonitor.top
            > monitorInfo.rcMonitor.bottom - monitorInfo.rcWork.bottom
    ) {
        // taskbar is on top
        topCrop = 0;
        bottomCrop = 1;
    }

    int widthPx = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
    int heightPx = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
    UINT dpi;
    GetDpiForMonitor(model->monitor, MDT_EFFECTIVE_DPI, &dpi, &dpi);
    double widthPt = intPxToPt(widthPx, dpi);
    double heightPt = intPxToPt(heightPx, dpi);
    int bubbleCount = getBubbleCount(
        model->bubbleCount, model->minCellArea, model->gridMargin,
        model->aspect, widthPt, heightPt
    );
    StringArray labels = getSortedLabels(bubbleCount);
    LabelRange labelRange;
    ZeroMemory(&labelRange, sizeof(labelRange));
    labelRange = getLabelRange(model->text, labels);
    double sqrtAspect = getSqrtAspect(model->aspect);
    double sqrtCellArea = getSqrtCellArea(
        widthPt / sqrtAspect, heightPt * sqrtAspect, model->gridMargin,
        labels.count
    );
    Point marginSize = {
        .x = model->gridMargin * sqrtCellArea * sqrtAspect,
        .y = model->gridMargin * sqrtCellArea / sqrtAspect,
    };
    // get the shape of all screen parallelograms (cells) without worrying
    // about scale yet. shape1 and shape2 represent the edges that
    // approximately correspond to the x and y axes respectively.
    // in the windows API in general and in this function specifically, the
    // positive y direction is downward.
    Point edge1, edge2;
    getCellEdges(
        model->angle1, model->angle2, model->aspect,
        sqrtCellArea * sqrtCellArea, &edge1, &edge2
    );
    ZeroMemory(&graphicsOut, sizeof(graphicsOut));
    graphicsOut.window = model->window;
    graphicsOut.dialog = model->dialog;
    graphicsOut.offsetPt = model->offsetPt;
    graphicsOut.dpi = dpi;
    graphicsOut.fontHeightPt = model->fontHeightPt;
    wcsncpy(graphicsOut.fontFamily, model->fontFamily, LF_FACESIZE);
    graphicsOut.systemFontChanges = model->systemFontChanges;
    graphicsOut.leftPx = monitorInfo.rcMonitor.left;
    graphicsOut.topPx = monitorInfo.rcMonitor.top;
    graphicsOut.widthPx = widthPx;
    graphicsOut.heightPx = heightPx;
    graphicsOut.topCrop = topCrop;
    graphicsOut.bottomCrop = bottomCrop;
    graphicsOut.bubbleCount = bubbleCount;
    graphicsOut.labelRange = getLabelRange(model->text, labels);
    graphicsOut.marginSize = marginSize;
    graphicsOut.edge1 = edge1;
    graphicsOut.edge2 = edge2;
    graphicsOut.colorKey = model->colorKey;
    graphicsOut.labelBackground = model->labelBackground;
    graphicsOut.selectionBackground = model->selectionBackground;
    graphicsOut.borderColor = model->borderColor;
    graphicsOut.paddingPx.left = ptToThinPx(model->paddingLeftPt, dpi),
    graphicsOut.paddingPx.top = ptToThinPx(model->paddingTopPt, dpi),
    graphicsOut.paddingPx.right = ptToThinPx(model->paddingRightPt, dpi),
    graphicsOut.paddingPx.bottom = ptToThinPx(model->paddingBottomPt, dpi),
    graphicsOut.borderPx = ptToThinPx(model->borderPt, dpi);
    graphicsOut.earHeightPx = ptToIntPx(model->earHeightPt, dpi);
    graphicsOut.earElevationPx = ptToIntPx(model->earElevationPt, dpi);
    graphicsOut.dashPx = ptToIntPx(model->dashPt, dpi);
    graphicsOut.mirrorStart.x = widthPx
        - ptToIntPx(model->mirrorWidthPt, dpi);
    graphicsOut.mirrorStart.y = heightPx
        - ptToIntPx(model->mirrorHeightPt, dpi);
    graphicsOut.matchPoint = model->matchPoint;
    graphicsOut.dragCount = model->dragCount;
    for (int i = 0; i < model->dragCount; i++) {
        graphicsOut.drag[i] = model->drag[i];
    }

    return &graphicsOut;
}

POINT getBubblePositionPx(Graphics *graphics, int index) {
    Point positionPt = getBubblePositionPt(
        graphics->marginSize,
        graphics->edge1, graphics->edge2,
        graphics->offsetPt,
        graphics->dialog,
        intPxToPt(graphics->widthPx, graphics->dpi),
        intPxToPt(graphics->heightPx, graphics->dpi),
        getSortedLabels(graphics->bubbleCount).count,
        index
    );
    POINT positionPx = {
        .x = min(
            graphics->widthPx - 1,
            max(0, ptToIntPx(positionPt.x, graphics->dpi))
        ),
        .y = min(
            graphics->heightPx - 1,
            max(0, ptToIntPx(positionPt.y, graphics->dpi))
        ),
    };
    return positionPx;
}

Graphics lastGraphics = {
    .window = NULL, .dialog = NULL,
    .offsetPt = { 0, 0 },
    .dpi = 0,
    .fontHeightPt = 0,
    .fontFamily = L"",
    .systemFontChanges = 0,
    .leftPx = 0, .topPx = 0, .widthPx = 0, .heightPx = 0,
    .topCrop = 0, .bottomCrop = 0,
    .bubbleCount = 0,
    .labelRange = { 0, 0, 0 },
    .marginSize = { 0, 0 }, .edge1 = { 0, 0 }, .edge2 = { 0, 0 },
    .colorKey = 0,
    .labelBackground = 0, .selectionBackground = 0, .borderColor = 0,
    .paddingPx = { 0, 0, 0, 0 },
    .borderPx = 0, .earHeightPx = 0, .earElevationPx = 0, .dashPx = 0,
    .mirrorStart = { 0, 0 },
    .matchPoint = { 0, 0 },
    .dragCount = 0,
    .drag = { { 0, 0 }, { 0, 0 }, { 0, 0 } },
};
void redraw(Graphics *graphics) {
    if (graphics->dragCount <= 0 || graphics->dragCount > 3) {
        ZeroMemory(&graphics->matchPoint, sizeof(POINT));
    }

    if (!memcmp(graphics, &lastGraphics, sizeof(Graphics))) {
        return;
    }

    ZeroMemory(&lastGraphics, sizeof(graphics));
    lastGraphics = *graphics;

    double widthPt = intPxToPt(graphics->widthPx, graphics->dpi);
    double heightPt = intPxToPt(graphics->heightPx, graphics->dpi);
    StringArray labels = getSortedLabels(graphics->bubbleCount);
    HDC device = GetDC(graphics->window);
    HDC memory = CreateCompatibleDC(device);
    HBRUSH keyBrush = getBrush(graphics->colorKey, KEY_BRUSH_SLOT);
    selectKeyBitmap(
        device, memory, graphics->widthPx, graphics->heightPx, graphics->colorKey
    );
    HDC labelMemory = CreateCompatibleDC(device);
    SelectObject(
        labelMemory,
        getLabelFont(
            graphics->dpi,
            graphics->fontHeightPt,
            graphics->fontFamily,
            graphics->systemFontChanges
        )
    );
    SetBkColor(labelMemory, graphics->labelBackground);
    SetTextColor(labelMemory, getTextColor(graphics->labelBackground));
    RECT *labelRects = selectLabelBitmap(
        device, labelMemory, graphics->bubbleCount, graphics->paddingPx
    );
    HDC selectionMemory = CreateCompatibleDC(device);
    SelectObject(
        selectionMemory,
        getLabelFont(
            graphics->dpi,
            graphics->fontHeightPt,
            graphics->fontFamily,
            graphics->systemFontChanges
        )
    );
    SetBkColor(selectionMemory, graphics->selectionBackground);
    SetTextColor(selectionMemory, getTextColor(graphics->selectionBackground));
    RECT *selectionRects = selectSelectionBitmap(
        device, selectionMemory, graphics->bubbleCount
    );
    HDC earMemory = CreateCompatibleDC(device);
    SIZE earSize = selectEarBitmap(
        device,
        earMemory,
        graphics->borderPx,
        graphics->earElevationPx,
        graphics->earHeightPx,
        graphics->colorKey,
        graphics->borderColor,
        graphics->labelBackground
    );
    // RECT labelBitmapRect = {
    //     ptToIntPx(graphics->offsetPt.x, graphics->dpi) + 100,
    //     ptToIntPx(graphics->offsetPt.y, graphics->dpi) + 400,
    //     ptToIntPx(graphics->offsetPt.x, graphics->dpi) + 100 + 600,
    //     ptToIntPx(graphics->offsetPt.y, graphics->dpi) + 400 + 400,
    // };
    // BitBlt(
    //     memory, labelBitmapRect.left, labelBitmapRect.top,
    //     labelBitmapRect.right - labelBitmapRect.left,
    //     labelBitmapRect.bottom - labelBitmapRect.top,
    //     labelMemory, 0, 0, SRCCOPY
    // );
    RECT client = { 0, 0, graphics->widthPx, graphics->heightPx };
    for (
        int i = graphics->labelRange.start; i < graphics->labelRange.stop; i++
    ) {
        POINT positionPx = getBubblePositionPx(graphics, i);
        BOOL xFlip = positionPx.x >= graphics->mirrorStart.x;
        BOOL yFlip = positionPx.y >= graphics->mirrorStart.y;
        BitBlt(
            memory,
            positionPx.x + xFlip * (1 - earSize.cx),
            positionPx.y + yFlip * (1 - earSize.cy),
            earSize.cx,
            earSize.cy,
            earMemory,
            xFlip * earSize.cx,
            yFlip * earSize.cy,
            SRCCOPY
        );
        BitBlt(
            memory,
            positionPx.x + graphics->borderPx + xFlip * (
                1 - 2 * graphics->borderPx
                    + labelRects[i].left - labelRects[i].right
            ),
            positionPx.y + graphics->earElevationPx + yFlip * (
                1 - 2 * graphics->earElevationPx
                    + labelRects[i].top - labelRects[i].bottom
            ),
            labelRects[i].right - labelRects[i].left,
            labelRects[i].bottom - labelRects[i].top,
            labelMemory,
            labelRects[i].left,
            labelRects[i].top,
            SRCCOPY
        );
        if (graphics->labelRange.matchLength) {
            RECT textRect = { 0, 0, 0, 0 };
            DrawText(
                labelMemory,
                labels.value[i],
                graphics->labelRange.matchLength,
                &textRect,
                DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX
            );
            BitBlt(
                memory,
                positionPx.x + graphics->borderPx
                    + graphics->paddingPx.left + xFlip * (
                        1 - 2 * graphics->borderPx
                            + labelRects[i].left - labelRects[i].right
                    ),
                positionPx.y + graphics->earElevationPx
                    + graphics->paddingPx.top + yFlip * (
                        1 - 2 * graphics->earElevationPx
                            + labelRects[i].top - labelRects[i].bottom
                    ),
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

    if (graphics->dragCount > 0) {
        HPEN pens[2] = {
            getPen(
                graphics->labelBackground,
                graphics->borderPx,
                DASH_PEN_STYLE,
                graphics->dashPx,
                DRAG_PEN_SLOT
            ),
            getPen(
                graphics->borderColor,
                graphics->borderPx,
                ALT_DASH_PEN_STYLE,
                graphics->dashPx,
                ALT_DRAG_PEN_SLOT
            ),
        };
        for (int i = 0; i < 2; i++) {
            SelectObject(memory, pens[i]);
            MoveToEx(
                memory,
                graphics->drag[0].x - graphics->leftPx,
                graphics->drag[0].y - graphics->topPx,
                NULL
            );
            for (int j = 1; j < graphics->dragCount; j++) {
                LineTo(
                    memory,
                    graphics->drag[j].x - graphics->leftPx,
                    graphics->drag[j].y - graphics->topPx
                );
            }

            if (graphics->dragCount < 3) {
                LineTo(
                    memory,
                    graphics->matchPoint.x - graphics->leftPx,
                    graphics->matchPoint.y - graphics->topPx
                );
            }
        }
    }

    DeleteDC(selectionMemory);
    DeleteDC(labelMemory);
    DeleteDC(earMemory);
    ReleaseDC(graphics->window, device);
    POINT screenPosition = {
        graphics->leftPx, graphics->topPx + graphics->topCrop
    };
    SIZE screenSize = {
        graphics->widthPx,
        graphics->heightPx - graphics->bottomCrop - graphics->topCrop
    };
    POINT memoryPosition = { 0, graphics->topCrop };
    UpdateLayeredWindow(
        graphics->window,
        device,
        &screenPosition,
        &screenSize,
        memory,
        &memoryPosition,
        graphics->colorKey,
        NULL,
        ULW_COLORKEY
    );
    for (
        int i = graphics->labelRange.start; i < graphics->labelRange.stop; i++
    ) {
        POINT positionPx = getBubblePositionPx(graphics, i);
        BOOL xFlip = positionPx.x >= graphics->mirrorStart.x;
        BOOL yFlip = positionPx.y >= graphics->mirrorStart.y;
        RECT dstRect = labelRects[i];
        OffsetRect(
            &dstRect,
            positionPx.x + (
                xFlip ? 1 - graphics->borderPx - dstRect.right
                    : graphics->borderPx - dstRect.left
            ),
            positionPx.y + (
                yFlip ? 1 - graphics->earElevationPx - dstRect.bottom
                    : graphics->earElevationPx - dstRect.top
            )
        );
        FillRect(memory, &dstRect, keyBrush);
        RECT earRect = {
            .left = positionPx.x + xFlip * (1 - earSize.cx),
            .top = positionPx.y + yFlip * (1 - earSize.cy)
        };
        earRect.right = earRect.left + earSize.cx;
        earRect.bottom = earRect.top + earSize.cy;
        FillRect(memory, &earRect, keyBrush);
    }

    if (graphics->dragCount > 0) {
        SelectObject(
            memory,
            getPen(
                graphics->colorKey,
                graphics->borderPx,
                BOTH_DASH_PEN_STYLE,
                graphics->dashPx,
                ERASE_DRAG_PEN_SLOT
            )
        );
        MoveToEx(
            memory,
            graphics->drag[0].x - graphics->leftPx,
            graphics->drag[0].y - graphics->topPx,
            NULL
        );
        for (int j = 1; j < graphics->dragCount; j++) {
            LineTo(
                memory,
                graphics->drag[j].x - graphics->leftPx,
                graphics->drag[j].y - graphics->topPx
            );
        }

        if (graphics->dragCount < 3) {
            LineTo(
                memory,
                graphics->matchPoint.x - graphics->leftPx,
                graphics->matchPoint.y - graphics->topPx
            );
        }
    }

    // FillRect(memory, &labelBitmapRect, keyBrush);

    DeleteDC(memory);
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

BOOL skipHitTest = FALSE;

LRESULT CALLBACK WndProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    if (message == WM_ACTIVATE) {
        if (LOWORD(wParam) == WA_ACTIVE || LOWORD(wParam) == WA_CLICKACTIVE) {
            Model *model = getModel(window);
            // The main window initially receives WM_ACTIVATE before its user
            // data is populated
            if (model && IsWindowVisible(model->dialog)) {
                SetActiveWindow(model->dialog);
            return 0;
        }
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
        redraw(getGraphics(getModel(window)));
        return 0;
    } else if (message == WM_SETTINGCHANGE) {
        if (wParam == SPI_SETNONCLIENTMETRICS) {
            Model *model = getModel(window);
            model->systemFontChanges++;
            redraw(getGraphics(model));
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
    MinDialogSizeIn in;
    ZeroMemory(&in, sizeof(in));
    in.clientSize = getMinDialogClientSize(dialog);
    in.showCaption = model->showCaption;
    in.showMenuBar = GetMenu(dialog) != NULL;
    if (in.clientSize.cy == 0) { return in.clientSize; }
    if (!memcmp(&in, &minDialogSizeIn, sizeof(in))) {
        return minDialogSizeOut;
    }

    ZeroMemory(&minDialogSizeIn, sizeof(in));
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
    RECT newFrame;
    ZeroMemory(&newFrame, sizeof(newFrame));
    *(LPSIZE)&newFrame.right = getMinDialogSize(dialog);
    if (newFrame.bottom == 0) { return; }
    RECT frame;
    ZeroMemory(&frame, sizeof(frame));
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

void setMatchPoint(Model *model, POINT matchPoint) {
    if (model->dragCount <= 0) {
        POINT cursorPos;
        GetCursorPos(&cursorPos);
        if (
            !model->hasMatch
                || cursorPos.x != model->matchPoint.x
                || cursorPos.y != model->matchPoint.y
        ) {
            model->naturalPoint = cursorPos;
        }
    }

    model->hasMatch = TRUE;
    model->matchPoint = matchPoint;
    SetCursorPos(matchPoint.x, matchPoint.y);
}

void unsetMatchPoint(Model *model) {
    if (model->dragCount > 0) {
        POINT dragStop = model->drag[model->dragCount - 1];
        SetCursorPos(dragStop.x, dragStop.y);
        GetCursorPos(&model->matchPoint);
        model->drag[model->dragCount - 1] = model->matchPoint;
    } else if (model->hasMatch) {
        POINT cursorPos;
        GetCursorPos(&cursorPos);
        if (
            cursorPos.x == model->matchPoint.x
                && cursorPos.y == model->matchPoint.y
        ) {
            SetCursorPos(
                model->naturalPoint.x, model->naturalPoint.y
            );
        }
    }

    model->hasMatch = FALSE;
}

const int PRESSED = 0x8000;
const UINT WM_APP_FITTOTEXT = WM_APP + 0;
const int ENABLE_DROPDOWN_TIMER = 1;
const int RESTORE_WINDOW_TIMER = 2;
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
    } else if (message == WM_ACTIVATE) {
        // make MouseJump topmost only when the dialog is active
        if (LOWORD(wParam) == WA_ACTIVE || LOWORD(wParam) == WA_CLICKACTIVE) {
            SetWindowPos(
                GetWindowOwner(dialog), HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
            );
            return 0;
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

            return 0;
        }
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
        } else if (wParam == RESTORE_WINDOW_TIMER) {
            KillTimer(dialog, wParam);
            Model *model = getModel(dialog);
            ShowWindow(model->window, SW_SHOWNORMAL);
            if (!model->text[0]) {
                unsetMatchPoint(model);
                redraw(getGraphics(model));
            }

            SetDlgItemText(dialog, IDC_TEXTBOX, L"");
            return TRUE;
        }
    } else if (message == WM_NCHITTEST && skipHitTest) {
        return HTTRANSPARENT;
    } else if (message == WM_COMMAND) {
        if (HIWORD(wParam) == 0 || HIWORD(wParam) == 1) {
            WORD command = LOWORD(wParam);
            if (command == 2) {
                PostQuitMessage(0);
                return TRUE;
            } else if (command == IDM_EXIT) {
                PostQuitMessage(0);
                return TRUE;
            } else if (command == IDM_SELECT_ALL) {
                HWND textBox = GetDlgItem(dialog, IDC_TEXTBOX);
                SendMessage(textBox, EM_SETSEL, 0, -1);
                SetFocus(textBox);
                return TRUE;
            } else if (command == IDC_DROPDOWN) {
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
                command >= IDM_LEFT && command <= IDM_DOWN || (
                    command >= IDM_SLIGHTLY_LEFT
                        && command <= IDM_SLIGHTLY_DOWN
                )
            ) {
                BOOL slow = !(command >= IDM_LEFT && command <= IDM_DOWN);
                if (slow) {
                    command += IDM_LEFT; command -= IDM_SLIGHTLY_LEFT;
                }

                ArrowKeyMap keys = getArrowKeyMap(slow);
                int xDirection = (
                    command == IDM_RIGHT
                        || (GetKeyState(keys.right) & PRESSED)
                ) - (
                    command == IDM_LEFT
                        || (GetKeyState(keys.left) & PRESSED)
                );
                int yDirection = (
                    command == IDM_DOWN
                        || (GetKeyState(keys.down) & PRESSED)
                ) - (
                    command == IDM_UP
                        || (GetKeyState(keys.up) & PRESSED)
                );
                Model *model = getModel(dialog);
                Graphics *graphics = getGraphics(model);
                Point deltaPt;
                if (slow) {
                    model->offsetPt.x += xDirection
                        * pxToPt(model->smallDeltaPx, graphics->dpi);
                    model->offsetPt.y += yDirection
                        * pxToPt(model->smallDeltaPx, graphics->dpi);
                } else {
                    model->offsetPt.x += xDirection
                        * pxToPt(model->deltaPx, graphics->dpi);
                    model->offsetPt.y += yDirection
                        * pxToPt(model->deltaPx, graphics->dpi);
                }

                if (graphics->labelRange.matchLength > 0) {
                    graphics->offsetPt = model->offsetPt;
                    POINT cursorPos = getBubblePositionPx(
                        graphics, graphics->labelRange.start
                    );
                    cursorPos.x += graphics->leftPx;
                    cursorPos.y += graphics->topPx;
                    setMatchPoint(model, cursorPos);
                } else {
                    if (model->hasMatch) { unsetMatchPoint(model); }
                    POINT cursorPos;
                    GetCursorPos(&cursorPos);
                    cursorPos.x += ptToIntPx(model->offsetPt.x, graphics->dpi)
                        - ptToIntPx(graphics->offsetPt.x, graphics->dpi);
                    cursorPos.y += ptToIntPx(model->offsetPt.y, graphics->dpi)
                        - ptToIntPx(graphics->offsetPt.y, graphics->dpi);
                    graphics->offsetPt = model->offsetPt;
                    SetCursorPos(cursorPos.x, cursorPos.y);
                    if (model->dragCount > 0 && model->dragCount < 3) {
                        GetCursorPos(&model->matchPoint);
                    }
                }

                graphics->matchPoint = model->matchPoint;
                redraw(graphics);
                return TRUE;
            } else if (command == IDM_CLICK) {
                POINT cursor;
                GetCursorPos(&cursor);
                Model *model = getModel(dialog);
                ShowWindow(model->window, SW_MINIMIZE);
                if (model->dragCount > 0) {
                    model->dragCount = 0;
                    POINT dragStart = model->drag[0];
                    SetCursorPos(dragStart.x, dragStart.y);
                    INPUT mouseDown = {
                        .type = INPUT_MOUSE,
                        .mi = { 0, 0, 0, MOUSEEVENTF_LEFTDOWN, 0, 0 },
                    };
                    SendInput(1, &mouseDown, sizeof(INPUT));
                    Point vector = {
                        cursor.x - dragStart.x,
                        cursor.y - dragStart.y,
                    };
                    if (vector.x == 0 && vector.y == 0) { vector.x = 1; }
                    vector = scale(vector, 8 / sqrt(dot(vector, vector)));
                    SetCursorPos(
                        dragStart.x + (int)round(vector.x),
                        dragStart.y + (int)round(vector.y)
                    );
                    Sleep(100);
                    SetCursorPos(cursor.x, cursor.y);
                    Sleep(100);
                    INPUT mouseUp = {
                        .type = INPUT_MOUSE,
                        .mi = { 0, 0, 0, MOUSEEVENTF_LEFTUP, 0, 0 },
                    };
                    SendInput(1, &mouseUp, sizeof(INPUT));
                } else {
                    INPUT click[2] = {
                        {
                            .type = INPUT_MOUSE,
                            .mi = { 0, 0, 0, MOUSEEVENTF_LEFTDOWN, 0, 0 },
                        },
                        {
                            .type = INPUT_MOUSE,
                            .mi = { 0, 0, 0, MOUSEEVENTF_LEFTUP, 0, 0 },
                        },
                    };
                    SendInput(2, click, sizeof(INPUT));
                }

                SetTimer(dialog, RESTORE_WINDOW_TIMER, 100, NULL);
                return TRUE;
            } else if (command == IDM_START_DRAGGING) {
                Model *model = getModel(dialog);
                BOOL erase = FALSE;
                if (model->dragCount > 0) {
                    POINT start = model->drag[model->dragCount - 1];
                    POINT end;
                    GetCursorPos(&end);
                    erase = start.x == end.x && start.y == end.y;
                }

                BOOL click = FALSE;
                if (erase) {
                    model->dragCount--;
                } else if (model->dragCount < 3) {
                    GetCursorPos(&model->drag[model->dragCount]);
                    model->dragCount++;
                    click = model->dragCount == 2;
                } else {
                    click = TRUE;
                }

                if (click) {
                    ShowWindow(model->window, SW_MINIMIZE);
                    INPUT click[2] = {
                        {
                            .type = INPUT_MOUSE,
                            .mi = { 0, 0, 0, MOUSEEVENTF_LEFTDOWN, 0, 0 },
                        },
                        {
                            .type = INPUT_MOUSE,
                            .mi = { 0, 0, 0, MOUSEEVENTF_LEFTUP, 0, 0 },
                        },
                    };
                    SendInput(2, click, sizeof(INPUT));
                    SetTimer(dialog, RESTORE_WINDOW_TIMER, 100, NULL);
                } if (model->text[0]) {
                    SetDlgItemText(dialog, IDC_TEXTBOX, L"");
                } else {
                    unsetMatchPoint(model);
                    redraw(getGraphics(model));
                }

                return TRUE;
            } else if (command == IDM_PREVIOUS_DRAG) {
                Model *model = getModel(dialog);
                unsetMatchPoint(model);
                redraw(getGraphics(model));
                return TRUE;
            } else if (command == IDM_HIDE_INTERFACE) {
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
                Model *model = getModel(dialog);
                Graphics *graphics = getGraphics(model);
                if (graphics->labelRange.matchLength > 0) {
                    POINT cursorPos = getBubblePositionPx(
                        graphics, graphics->labelRange.start
                    );
                    cursorPos.x += graphics->leftPx;
                    cursorPos.y += graphics->topPx;
                    setMatchPoint(model, cursorPos);
                } else {
                    unsetMatchPoint(model);
                }

                graphics->matchPoint = model->matchPoint;
                redraw(graphics);
                return TRUE;
            }
        }
    } else if (message == WM_CLOSE) {
        PostQuitMessage(0);
        return TRUE;
    } else if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == 70 || message == WM_SYSKEYDOWN || message == 71 || message == 28 || message == 134 || message == 799 || message == 1024 || message == 307 || message == 309 || message == 32 || message == 131 || message == 133 || message == 20 || message == 310 || message == 15) {

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
        } else if (message->wParam == VK_BACK && !getModifiers()) {
            HWND textBox = GetDlgItem(dialog, IDC_TEXTBOX);
            if (GetFocus() != textBox || GetWindowTextLength(textBox) > 0) {
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
    srand(478956);

    Model model = {
        .monitor = MonitorFromWindow(
            GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY
        ),
        .colorKey = RGB(255, 0, 255),
        .offsetPt = { 0, 0 },
        .deltaPx = 12,
        .smallDeltaPx = 1,
        .bubbleCount = 1200,
        .minCellArea = 30 * 30,
        .gridMargin = 0.5,
        .aspect = 4 / 3.0,
        .angle1 = 15 * PI / 180,
        .angle2 = 75 * PI / 180,
        .fontHeightPt = 0,
        .fontFamily = L"",
        .systemFontChanges = 0,
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
        .dashPt = 3,
        .mirrorWidthPt = 100,
        .mirrorHeightPt = 100,
        .textBoxWidthPt = 15,
        .dropdownWidthPt = 15,
        .clientHeightPt = 21,
        .showCaption = TRUE,
        .minTextBoxSize = { 0, 0 },
        .inMenuLoop = FALSE,
        .text = L"",
        .hasMatch = FALSE,
        .dragCount = 0,
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
        WS_POPUP | WS_VISIBLE | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, // parent
        NULL, // menu
        hInstance,
        NULL // lParam
    );
    SetWindowLongPtr(model.window, GWLP_USERDATA, (LONG)&model);
    model.dialog = CreateDialog(hInstance, L"TOOL", model.window, DlgProc);
    redraw(getGraphics(&model));

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

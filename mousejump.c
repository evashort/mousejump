// rc mousejump.rc && cl mousejump.c /link mousejump.res && mousejump.exe

// https://docs.microsoft.com/en-us/windows/win32/controls/common-control-versions
#define _WIN32_IE 0x0600

#define UNICODE
#include <math.h>
#include "mousejump.h"
#include <stdlib.h>
#include <ShellScalingApi.h>
#include <commctrl.h>
#include <windows.h>
#include <combaseapi.h>
#include <shobjidl_core.h>
#include "./file_watcher.h"
#include "./json_parser.h"

// GET_X_LPARAM, GET_Y_LPARAM
#include <windowsx.h>

#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "SHCore")
#pragma comment(lib, "comctl32")
#pragma comment(lib, "Ole32")
// https://docs.microsoft.com/en-us/windows/win32/controls/cookbook-overview
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

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

typedef enum {
    KEY_BRUSH_SLOT = 0, LABEL_BRUSH_SLOT, BRUSH_SLOT_COUNT,
} BrushSlot;
COLORREF brushesIn[BRUSH_SLOT_COUNT];
HBRUSH brushesOut[BRUSH_SLOT_COUNT] = { NULL };
HBRUSH getBrush(COLORREF color, BrushSlot slot) {
    if (brushesOut[slot]) {
        if (color == brushesIn[slot]) { return brushesOut[slot]; }
        DeleteObject(brushesOut[slot]);
    }

    return brushesOut[slot] = CreateSolidBrush(brushesIn[slot] = color);
}

typedef enum {
    BORDER_PEN_SLOT = 0,
    DRAG_PEN_SLOT,
    ALT_DRAG_PEN_SLOT,
    ERASE_DRAG_PEN_SLOT,
    PEN_SLOT_COUNT,
} PenSlot;

typedef enum {
    SOLID_PEN_STYLE, DASH_PEN_STYLE, ALT_DASH_PEN_STYLE, BOTH_DASH_PEN_STYLE,
} PenStyle;
typedef struct { COLORREF color; int width, style, dashLength; } PenIn;
PenIn pensIn[PEN_SLOT_COUNT];
HPEN pensOut[PEN_SLOT_COUNT] = { NULL };
HPEN getPen(
    COLORREF color, int width, PenStyle style, int dashLength, PenSlot slot
) {
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

int nextPowerOf2(int n, int start) {
    int result = start;
    while (result < n) { result <<= 1; }
    return result;
}

typedef enum {
    MODEL_TEXT_SLOT = 0, TEMP_TEXT_SLOT, PATH_TEXT_SLOT, TITLE_TEXT_SLOT,
    RANGE_TEXT_SLOT, TOOLTIP_TEXT_SLOT, TEXT_SLOT_COUNT
} TextSlot;
int textsIn[TEXT_SLOT_COUNT] = { 0, 0, 0, 0, 0, 0 };
LPWSTR textsOut[TEXT_SLOT_COUNT] = { NULL, NULL, NULL, NULL, NULL, NULL };
LPWSTR getText(int capacity, TextSlot slot) {
    if (textsIn[slot] < capacity) {
        textsIn[slot] = nextPowerOf2(capacity, 64);
        textsOut[slot] = realloc(
            textsOut[slot], textsIn[slot] * sizeof(WCHAR)
        );
    }

    return textsOut[slot];
}

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
LPWSTR *sortedLabelsOut = NULL;
LPWSTR *getSortedLabels(int count) {
    if (count == sortedLabelsIn) { return sortedLabelsOut; }
    sortedLabelsIn = count;
    StringArray labels = getLabels();
    sortedLabelsOut = realloc(sortedLabelsOut, count * sizeof(LPWSTR));
    memcpy_s(
        sortedLabelsOut, count * sizeof(LPWSTR),
        labels.value, count * sizeof(LPWSTR)
    );
    qsort(
        (void*)sortedLabelsOut, count, sizeof(LPWSTR),
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

struct {
    LPWSTR text; int capacity; int count;
} labelRangeIn = { .text = NULL };
typedef struct { int start, stop, matchLength; } LabelRange;
LabelRange labelRangeOut;
LabelRange getLabelRange(LPCWSTR text, int count) {
    if (
        count == labelRangeIn.count && labelRangeIn.text != NULL
            && !wcsncmp(text, labelRangeIn.text, labelRangeIn.capacity)
    ) { return labelRangeOut; }

    labelRangeIn.capacity = wcslen(text) + 1;
    labelRangeIn.text = getText(labelRangeIn.capacity, RANGE_TEXT_SLOT);
    wcsncpy_s(labelRangeIn.text, labelRangeIn.capacity, text, _TRUNCATE);
    labelRangeIn.count = count;

    LPWSTR node = L","; int nodeLength = wcslen(node);
    LPWSTR edge = L"-"; int edgeLength = wcslen(edge);
    if (!wcsncmp(text, node, nodeLength)) {
        text += nodeLength;
        while (TRUE) {
            if (!wcsncmp(text, node, nodeLength)) { text += nodeLength; }
            else if (!wcsncmp(text, edge, edgeLength)) { text += edgeLength; }
            else { break; }
        }
    } else {
        while (!wcsncmp(text, edge, edgeLength)) { text += edgeLength; }
    }

    LPWSTR *labels = getSortedLabels(count);
    int textLength = wcslen(text);
    int start = 0;
    int stop = count;
    int matchLength = 0;
    for (int i = 0; i < textLength; i++) {
        int newStart = labelBinarySearch(
            labels, text[i], start, stop, matchLength, 0
        );
        int newStop = labelBinarySearch(
            labels, text[i], newStart, stop, matchLength, 1
        );
        if (newStart < newStop) {
            start = newStart;
            stop = newStop;
            matchLength++;
        }
    }

    ZeroMemory(&labelRangeOut, sizeof(labelRangeOut));
    labelRangeOut.start = start;
    labelRangeOut.stop = stop;
    labelRangeOut.matchLength = matchLength;
    return labelRangeOut;
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
        metrics.lfMessageFont.lfHeight = -max(1, ptToIntPx(heightPt, dpi));
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
    LPWSTR *labels = getSortedLabels(count);
    labelWidthsOut = realloc(labelWidthsOut, count * sizeof(int));
    for (int i = 0; i < count; i++) {
        RECT rect = { 0, 0 };
        DrawText(
            device, labels[i], -1, &rect,
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
    ZeroMemory(&in, sizeof(in));
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
    LPWSTR *labels = getSortedLabels(count);
    int *labelWidths = getLabelWidths(memory, count);
    int width = 500;
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

    labelBitmapOut->rects = realloc(
        labelBitmapOut->rects, count * sizeof(RECT)
    );
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

LPWSTR getTextBoxText(HWND textBox) {
    int capacity = GetWindowTextLength(textBox) + 1;
    LPWSTR text = getText(capacity, MODEL_TEXT_SLOT);
    GetWindowText(textBox, text, capacity);
    return text;
}

LPWSTR getSettingsPath(LPCWSTR filename) {
    int nameLength = wcslen(filename);
    int capacity = MAX_PATH + nameLength;
    LPWSTR path = getText(capacity, PATH_TEXT_SLOT);
    int pathLength = GetModuleFileName(NULL, path, capacity - nameLength);
    while (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        capacity *= 2;
        path = getText(capacity, PATH_TEXT_SLOT);
        pathLength = GetModuleFileName(NULL, path, capacity - nameLength);
    }

    int folderStop = pathLength;
    while (
        folderStop > 0 && path[folderStop - 1] != L'/'
            && path[folderStop - 1] != L'\\'
    ) { folderStop--; }
    wcsncpy_s(
        path + folderStop, pathLength + 1 - folderStop, filename, nameLength
    );
    return path;
}

LPCWSTR getErrorTitle(LPCWSTR path, int lineNumber) {
    int pathLength = wcslen(path);
    int folderStop = pathLength;
    while (
        folderStop > 0 && path[folderStop - 1] != L'/'
            && path[folderStop - 1] != L'\\'
    ) { folderStop--; }
    path += folderStop;
    pathLength -= folderStop;
    int numberStart = pathLength + sizeof(L" line ") / sizeof(WCHAR) - 1;
    int capacity = numberStart + sizeof(STRINGIFY(INT_MIN)) / sizeof(WCHAR);
    LPWSTR errorTitle = getText(capacity, TITLE_TEXT_SLOT);
    wcsncpy_s(errorTitle, capacity, path, pathLength);
    wcsncpy_s(
        errorTitle + pathLength, capacity - pathLength, L" line ", _TRUNCATE
    );
    _itow_s(lineNumber, errorTitle + numberStart, capacity - numberStart, 10);
    return errorTitle;
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

POINT roundPoint(Point p) {
    POINT pInt = { (int)round(p.x), (int)round(p.y) };
    return pInt;
}

typedef struct { Point p[2]; } PointPair;

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

typedef struct {
    Point edge1, edge2, offset;
    double width, height;
    int count;
} BubblesIn;

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
    BubblesIn in, Spine oldSpine, int cliffStart, int cliffStop, HWND dialog
) {
    double inverseScale = 1 / determinant(in.edge1, in.edge2);
    // inverse1 and inverse2 are edges of a 1pt x 1pt square, projected into
    // grid space (in other words, they are the columns of the inverse of the
    // matrix whose columns are the edges of a screen parallelogram).
    Point inverse1 = scale(makePoint(in.edge2.y, -in.edge1.y), inverseScale);
    Point inverse2 = scale(makePoint(-in.edge2.x, in.edge1.x), inverseScale);
    Point gridOffset = matrixDot(inverse1, inverse2, scale(in.offset, -1));
    // the grid parallelogram is the entire screen area projected into grid
    // space
    Point gridEdge1 = scale(inverse1, in.width);
    Point gridEdge2 = scale(inverse2, in.height);
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
    if (actualCount - in.count > edgeCellCount) {
        // I convinced myself that a borderRadius of 0.5 should be enough to
        // prevent this error from ever occuring, but that was before the
        // cliffStart and cliffStop parameters were added to reduce label
        // churn.
        MessageBox(
            dialog,
            L"Not enough edge cells. "
            L"A developer should increase borderRadius.",
            L"MouseJump Error",
            MB_ICONERROR
        );
        exit(1);
    }

    for (int j = 0; j < actualCount - in.count; j++) {
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
    in.edge1 = edge1; in.edge2 = edge2; in.offset = offset;
    in.width = width; in.height = height;
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
        in, oldSpine, cliffStart, cliffStop, dialog
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

typedef struct {
    Point marginSize, edge1, edge2;
    HWND dialog;
    double width, height;
    int count;
} Layout;

#pragma endregion
Point getBubblePositionPt(Layout layout, Point offset, int index) {
    double width = layout.width + 2 * layout.marginSize.x;
    double height = layout.height + 2 * layout.marginSize.y;
    Point *bubbles = getBubbles(
        layout.edge1,
        layout.edge2,
        offset,
        layout.width + 2 * layout.marginSize.x,
        layout.height + 2 * layout.marginSize.y,
        layout.count,
        layout.dialog
    );
    Point result = add(
        matrixDot(layout.edge1, layout.edge2, bubbles[index]),
        add(offset, scale(layout.marginSize, -1))
    );
    return result;
}

int getBubbleCount(
    int maxCount, double minCellArea, double gridMargin, double aspect,
    double width, double height
) {
    return min(
        min(maxCount, getLabels().count),
        width * height / minCellArea + 2 * gridMargin * (
            2 * gridMargin + (width + height * aspect) / sqrt(
                aspect * minCellArea
            )
        )
    );
}

ITaskbarList3 *taskbarOut = NULL;
ITaskbarList3 *getTaskbar() {
    if (taskbarOut != NULL) { return taskbarOut; }
    CoCreateInstance(
        &CLSID_TaskbarList,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_ITaskbarList3,
        (void**)&taskbarOut
    );
    taskbarOut->lpVtbl->HrInit(taskbarOut);
    return taskbarOut;
}

typedef union {
    int milliseconds;
    POINT point;
    DWORD clickType;
    DWORD wheelDelta;
    DWORD resourceID;
} ActionParam;
ActionParam actionParamNone;
ActionParam actionParamMilliseconds(int milliseconds) {
    actionParamNone.milliseconds = milliseconds; return actionParamNone;
}
ActionParam actionParamPoint(POINT point) {
    actionParamNone.point = point; return actionParamNone;
}
ActionParam actionParamClickType(DWORD clickType) {
    actionParamNone.clickType = clickType; return actionParamNone;
}
ActionParam actionParamWheelDelta(DWORD wheelDelta) {
    actionParamNone.wheelDelta = wheelDelta; return actionParamNone;
}
ActionParam actionParamResourceID(DWORD resourceID) {
    actionParamNone.resourceID = resourceID; return actionParamNone;
}

// https://stackoverflow.com/questions/7259238/how-to-forward-typedefd-struct-in-h
typedef struct Model Model;

typedef struct {
    BOOL (*function)(Model*, ActionParam);
    ActionParam param;
} Action;
struct { Action *actions; int capacity; } actionListOut = { .actions = NULL };
Action *getActionList(int capacity, Action *oldList) {
    int oldCapacity = actionListOut.capacity
        - (oldList - actionListOut.actions);
    if (capacity <= oldCapacity) { return actionListOut.actions; }
    memmove_s(
        actionListOut.actions, actionListOut.capacity * sizeof(Action),
        oldList, oldCapacity * sizeof(Action)
    );
    if (capacity <= actionListOut.capacity) { return actionListOut.actions; }
    actionListOut.capacity = nextPowerOf2(capacity, 64);
    return actionListOut.actions = realloc(
        actionListOut.actions, actionListOut.capacity * sizeof(Action)
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
    free(sortedLabelsOut);
    DeleteObject(labelFontOut);
    free(labelWidthsOut);
    DeleteObject(labelBitmapOut.bitmap);
    free(labelBitmapOut.rects);
    DeleteObject(selectionBitmapOut.bitmap);
    free(selectionBitmapOut.rects);
    DeleteObject(earBitmapOut);
    free(acceleratorsOut.value);
    if (acceleratorTableOut) { DestroyAcceleratorTable(acceleratorTableOut); }
    if (dropdownMenuOut) { DestroyMenu(dropdownMenuOut); }
    for (int i = 0; i < TEXT_SLOT_COUNT; i++) { free(textsOut[i]); }
    free(edgeCellsOut.edgeCells);
    free(spineOut.oldSpine.ribStarts);
    free(spineOut.newSpine.ribStarts);
    free(bubblesOut.bubbles);
    free(bubblesOut.added);
    free(bubblesOut.removed);
    if (taskbarOut != NULL) { taskbarOut->lpVtbl->Release(taskbarOut); }
    free(actionListOut.actions);
}

struct Model {
    WatcherData watcherData;
    LPWSTR settingsPath;
    HWND window;
    HWND dialog;
    HWND tooltip;
    int lineNumber;
    LPWSTR toolText;
    BOOL autoHideTooltip;
    BOOL keepOpen;
    HMONITOR monitor;
    BOOL drawnYet;
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
    double textBoxHeightPt;
    BOOL showCaption;
    SIZE minTextBoxSize;
    // used only for calculating the previous dialog size after WM_DPICHANGED
    UINT dpi;
    LPWSTR text;
    POINT naturalPoint;
    POINT matchPoint;
    BOOL hasMatch;
    int dragCount;
    POINT drag[3];
    Action *actions;
    int actionCount;
    DWORD nextIcon;
};

BOOL shouldShowLabels(Model *model) {
    return GetFocus() == GetDlgItem(model->dialog, IDC_TEXTBOX);
}

typedef struct {
    int top, left, width, height, topCrop, bottomCrop;
    UINT dpi;
} Screen;
Screen getScreen(HMONITOR *monitor) {
    MONITORINFO monitorInfo;
    ZeroMemory(&monitorInfo, sizeof(monitorInfo));
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfo(*monitor, &monitorInfo)) {
        POINT origin = { 0, 0 };
        *monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
        GetMonitorInfo(*monitor, &monitorInfo);
    }

    Screen screen;
    ZeroMemory(&screen, sizeof(screen));
    screen.left = monitorInfo.rcMonitor.left;
    screen.top = monitorInfo.rcMonitor.top;
    screen.width = monitorInfo.rcMonitor.right - screen.left;
    screen.height = monitorInfo.rcMonitor.bottom - screen.top;
    screen.topCrop = 1;
    screen.bottomCrop = 0;
    if (
        monitorInfo.rcWork.top - monitorInfo.rcMonitor.top
            > monitorInfo.rcMonitor.bottom - monitorInfo.rcWork.bottom
    ) {
        // taskbar is on top
        screen.topCrop = 0;
        screen.bottomCrop = 1;
    }

    GetDpiForMonitor(*monitor, MDT_EFFECTIVE_DPI, &screen.dpi, &screen.dpi);
    return screen;
}

typedef struct {
    int width, height;
    UINT dpi;
    int count;
    double minCellArea, gridMargin, aspect, angle1, angle2;
    Point offset;
} LayoutIn;
LayoutIn layoutIn = {
    .width = 0, .height = 0,
    .dpi = 0,
    .count = 0,
    .minCellArea = 0, .gridMargin = 0, .aspect = 0, .angle1 = 0, .angle2 = 0,
};
Layout layoutOut;
Layout getLayout(Model *model, Screen screen) {
    LayoutIn in;
    ZeroMemory(&in, sizeof(in));
    in.width = screen.width; in.height = screen.height;
    in.dpi = screen.dpi;
    in.count = model->bubbleCount;
    in.minCellArea = model->minCellArea; in.gridMargin = model->gridMargin;
    in.aspect = model->aspect;
    in.angle1 = model->angle1; in.angle2 = model->angle2;
    if (!memcmp(&in, &layoutIn, sizeof(in))) { return layoutOut; }
    ZeroMemory(&layoutIn, sizeof(layoutIn));
    layoutIn = in;

    ZeroMemory(&layoutOut, sizeof(layoutOut));
    layoutOut.width = intPxToPt(screen.width, screen.dpi);
    layoutOut.height = intPxToPt(screen.height, screen.dpi);
    layoutOut.count = getBubbleCount(
        model->bubbleCount, model->minCellArea, model->gridMargin,
        model->aspect, layoutOut.width, layoutOut.height
    );

    double sqrtAspect = sqrt(model->aspect);
    double aspectWidth = layoutOut.width / sqrtAspect;
    double aspectHeight = layoutOut.height * sqrtAspect;
    double temp =  model->gridMargin * (aspectWidth - aspectHeight);
    double sqrtCellArea = (
        model->gridMargin * (aspectWidth + aspectHeight) + sqrt(
            temp * temp + layoutOut.count * layoutOut.width * layoutOut.height
        )
    ) / (layoutOut.count - 4 * model->gridMargin * model->gridMargin);
    layoutOut.marginSize.x = model->gridMargin * sqrtCellArea * sqrtAspect;
    layoutOut.marginSize.y = model->gridMargin * sqrtCellArea / sqrtAspect;

    // get the shape of all screen parallelograms (cells) without worrying
    // about scale yet. shape1 and shape2 represent the edges that
    // approximately correspond to the x and y axes respectively.
    // in the windows API in general and in this function specifically, the
    // positive y direction is downward.
    layoutOut.edge1.x = cos(model->angle1) * model->aspect;
    layoutOut.edge1.y = sin(model->angle1);
    layoutOut.edge2.x = cos(model->angle2) * model->aspect;
    layoutOut.edge2.y = sin(model->angle2);
    double shapeScale = sqrtCellArea / sqrt(
        determinant(layoutOut.edge1, layoutOut.edge2)
    );
    layoutOut.edge1 = scale(layoutOut.edge1, shapeScale);
    layoutOut.edge2 = scale(layoutOut.edge2, shapeScale);

    return layoutOut;
}

typedef struct {
    Layout layout;
    Point offsetPt;
    double fontHeightPt;
    WCHAR fontFamily[LF_FACESIZE];
    int systemFontChanges;
    LabelRange labelRange;
    COLORREF labelBackground, selectionBackground, borderColor;
    RECT paddingPx;
    double borderPx, earHeightPx, earElevationPx;
    POINT mirrorStart;
} LabelGraphics;
typedef struct {
    COLORREF labelBackground, borderColor;
    double borderPx, dashPx;
    POINT mirrorStart;
    int dragCount;
    POINT drag[3];
    BOOL arrowHead;
} DragGraphics;
typedef struct {
    BOOL initialized;
    Screen screen;
    COLORREF colorKey;
    BOOL showLabels;
    LabelGraphics lg;
    DragGraphics dg;
} Graphics;
Graphics graphicsOut[2] = {
    { .initialized = FALSE }, { .initialized = FALSE }
};
int graphicsOutIndex = 1;
Graphics *getGraphics(Model *model, Screen screen) {
    Graphics *graphics = &graphicsOut[graphicsOutIndex];
    ZeroMemory(graphics, sizeof(Graphics));
    graphics->initialized = TRUE;
    graphics->screen = screen;
    graphics->colorKey = model->colorKey;
    graphics->showLabels = shouldShowLabels(model);
    if (graphics->showLabels) {
        LabelGraphics *lg = &graphics->lg;
        lg->layout = getLayout(model, screen);
        lg->offsetPt = model->offsetPt;
        lg->fontHeightPt = model->fontHeightPt;
        wcsncpy(lg->fontFamily, model->fontFamily, LF_FACESIZE);
        lg->systemFontChanges = model->systemFontChanges;
        lg->labelRange = getLabelRange(model->text, lg->layout.count);
        lg->selectionBackground = model->selectionBackground;
        lg->labelBackground = model->labelBackground;
        lg->borderColor = model->borderColor;
        lg->paddingPx.left = ptToThinPx(model->paddingLeftPt, screen.dpi),
        lg->paddingPx.top = ptToThinPx(model->paddingTopPt, screen.dpi),
        lg->paddingPx.right = ptToThinPx(model->paddingRightPt, screen.dpi),
        lg->paddingPx.bottom = ptToThinPx(model->paddingBottomPt, screen.dpi),
        lg->borderPx = ptToThinPx(model->borderPt, screen.dpi);
        lg->earHeightPx = ptToIntPx(model->earHeightPt, screen.dpi);
        lg->earElevationPx = ptToIntPx(model->earElevationPt, screen.dpi);
        lg->mirrorStart.x = screen.width
            - ptToIntPx(model->mirrorWidthPt, screen.dpi);
        lg->mirrorStart.y = screen.height
            - ptToIntPx(model->mirrorHeightPt, screen.dpi);
    }

    // hide drag path when window is minimized. this is a strange one. if you
    // reveal the desktop and use MouseJump to drag across it, the drag will
    // intermittently appear to move the MouseJump window instead of selecting
    // desktop icons, as if the drag path is still obstructing the cursor,
    // even though the drag path is no longer visible because the MouseJump
    // window is already minimized by the time the mouse button is pressed.
    // hiding the drag path after minimizing the window appears to fix this
    // problem. there is no need to explicitly hide the labels as well because
    // we hide them anyway when the textbox loses focus. you might wonder, if
    // we already hide everything when dragging, couldn't we get away with not
    // minimizing the window? well I tried that and after I used MouseJump to
    // click in the lower right corner of my screen to reveal the desktop, the
    // MouseJump dialog was no longer visible even though it was focused.
    if (model->dragCount > 0 && !IsIconic(model->window)) {
        DragGraphics *dg = &graphics->dg;
        dg->labelBackground = model->labelBackground;
        dg->borderColor = model->borderColor;
        dg->borderPx = ptToThinPx(model->borderPt, screen.dpi);
        dg->dashPx = ptToIntPx(model->dashPt, screen.dpi);
        dg->dragCount = min(model->dragCount + 1, 3);
        for (int i = 0; i < model->dragCount; i++) {
            dg->drag[i] = model->drag[i];
        }

        if (dg->dragCount > model->dragCount) {
            dg->drag[model->dragCount] = model->hasMatch
                ? model->matchPoint : model->naturalPoint;
        }

        dg->arrowHead = model->dragCount >= 3;
    }

    return graphics;
}

POINT getBubblePositionPx(
    Screen screen, Layout layout, Point offset, int index
) {
    Point positionPt = getBubblePositionPt(layout, offset, index);
    POINT positionPx = {
        .x = min(
            screen.width - 1, max(0, ptToIntPx(positionPt.x, screen.dpi))
        ),
        .y = min(
            screen.height - 1, max(0, ptToIntPx(positionPt.y, screen.dpi))
        ),
    };
    return positionPx;
}

typedef struct { RECT *labelRects; SIZE earSize; } DrawLabelsOut;
DrawLabelsOut drawLabels(
    LabelGraphics *graphics, COLORREF colorKey, Screen screen,
    HDC device, HDC memory
) {
    HDC labelMemory = CreateCompatibleDC(device);
    SelectObject(
        labelMemory,
        getLabelFont(
            screen.dpi, graphics->fontHeightPt, graphics->fontFamily,
            graphics->systemFontChanges
        )
    );
    SetBkColor(labelMemory, graphics->labelBackground);
    SetTextColor(labelMemory, getTextColor(graphics->labelBackground));
    RECT *labelRects = selectLabelBitmap(
        device, labelMemory, graphics->layout.count, graphics->paddingPx
    );
    HDC selectionMemory = CreateCompatibleDC(device);
    SelectObject(
        selectionMemory,
        getLabelFont(
            screen.dpi, graphics->fontHeightPt, graphics->fontFamily,
            graphics->systemFontChanges
        )
    );
    SetBkColor(selectionMemory, graphics->selectionBackground);
    SetTextColor(
        selectionMemory, getTextColor(graphics->selectionBackground)
    );
    RECT *selectionRects = selectSelectionBitmap(
        device, selectionMemory, graphics->layout.count
    );
    HDC earMemory = CreateCompatibleDC(device);
    SIZE earSize = selectEarBitmap(
        device,
        earMemory,
        graphics->borderPx,
        graphics->earElevationPx,
        graphics->earHeightPx,
        colorKey,
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
    LPWSTR *labels = getSortedLabels(graphics->layout.count);
    for (
        int i = graphics->labelRange.start; i < graphics->labelRange.stop; i++
    ) {
        POINT positionPx = getBubblePositionPx(
            screen, graphics->layout, graphics->offsetPt, i
        );
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
                labels[i],
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

    DeleteDC(selectionMemory);
    DeleteDC(labelMemory);
    DeleteDC(earMemory);
    DrawLabelsOut result = { .labelRects = labelRects, .earSize = earSize };
    return result;
}

#define KEYFRAME_COUNT 11
// index of keyframe that corresponds to t=1
#define KEYFRAME_END 9
Point keyframes[2][KEYFRAME_COUNT] = {
    {
        { -250.75351 / 450, 199.317716 / 450 },
        { -236.7338 / 450, 159.511926 / 450 },
        { -210.54139 / 450, 134.923126 / 450 },
        { -178.79139 / 450, 103.173126 / 450 },
        { -152.33306 / 450, 114.512416 / 450 },
        { -93.25481 / 450, 132.136466 / 450 },
        { -41.85005 / 450, 155.570986 / 450 },
        { 12.100406 / 450, 138.984536 / 450 },
        { 48.0081441 / 450, 107.990486 / 450 },
        { 70.134467 / 450, 0 / 450 },
        // cubic bezier with 3 equally spaced segments will be traversed at a
        // constant speed
        { 1.0 / 3, 0 },
    },
    {
        { 217.87244 / 450 - 0/9.0, 223.221796 / 450 },
        { 170.57139 / 450 - 1/9.0, 270.598516 / 450 },
        { 93.88563 / 450 - 2/9.0, 289.307386 / 450 },
        { 16.196495 / 450 - 3/9.0, 283.038356 / 450 },
        { -18.328431 / 450 - 4/9.0, 233.854206 / 450 },
        { 17.432134 / 450 - 5/9.0, 228.053036 / 450 },
        { 61.067624 / 450 - 6/9.0, 189.088076 / 450 },
        { 106.791312 / 450 - 7/9.0, 165.794376 / 450 },
        { 182.90179 / 450 - 8/9.0, 116.840896 / 450 },
        { 143.688378 / 450 - 9/9.0, 0 / 450 },
        { -1.0 / 3, 0 },
    }
};

Point interpolateKeyframes(Point *frames, int count, int end, double t) {
    int i = (int)floor(t * end);
    double x = t * end - i;
    Point a = frames[min(max(i - 1, 0), count - 1)];
    Point b = frames[min(max(i, 0), count - 1)];
    Point c = frames[min(max(i + 1, 0), count - 1)];
    Point d = frames[min(max(i + 2, 0), count - 1)];
    return add(
        add(
            scale(a, ((-0.5 * x + 1) * x - 0.5) * x),
            scale(b, (1.5 * x - 2.5) * x * x + 1)
        ),
        add(
            scale(c, ((-1.5 * x + 2) * x + 0.5) * x),
            scale(d, (0.5 * x - 0.5) * x * x)
        )
    );
}

// idealNormal doesn't have to be normalized
PointPair getControlPoints(Point vector, Point idealNormal, UINT dpi) {
    double ropeLengthPt = 60;
    double ropeLengthPx = ptToPx(ropeLengthPt, dpi);
    double length = sqrt(dot(vector, vector));
    Point tangent = length > 0 ? scale(vector, 1 / length)
        : getNormal(idealNormal);
    Point normal = leftTurn(tangent);
    int normalSign = copysign(1.0, dot(idealNormal, normal));
    PointPair control;
    for (int i = 0; i < 2; i++) {
        control.p[i] = scale(
            interpolateKeyframes(
                keyframes[i], KEYFRAME_COUNT, KEYFRAME_END,
                length / ropeLengthPx
            ),
            // control points don't start scaling with rope until it starts
            // stretching
            max(length, ropeLengthPx)
        );
        control.p[i] = add(
            scale(tangent, control.p[i].x),
            scale(normal, control.p[i].y * normalSign)
        );
    }

    return control;
}

Point getIdealNormal(Screen screen, POINT a) {
    // edgeDistance: how close you have to be to the edge of the screen before
    // the normal tries to point away from the edge
    double edgeDistancePt = 20;
    double edgeDistancePx = ptToPx(edgeDistancePt, screen.dpi);
    Point idealNormal = {
        max(0, screen.left + edgeDistancePx - a.x)
            + min(
                0, screen.left + screen.width - edgeDistancePx - a.x
            ),
        max(0, screen.top + edgeDistancePx - a.y)
            + min(
                0, screen.top + screen.height - edgeDistancePx - a.y
            ),
    };
    return idealNormal; // not normalized. zero if not close to the edge
}

Point getFinalTangent(Point control2) {
    return scale(control2, -1 / sqrt(dot(control2, control2)));
}

Point interpolateBezier(Point a, Point b, Point c, Point d, double t) {
    Point ab = add(scale(a, 1 - t), scale(b, t));
    Point bc = add(scale(b, 1 - t), scale(c, t));
    Point cd = add(scale(c, 1 - t), scale(d, t));
    Point abc = add(scale(ab, 1 - t), scale(bc, t));
    Point bcd = add(scale(bc, 1 - t), scale(cd, t));
    Point abcd = add(scale(abc, 1 - t), scale(bcd, t));
    return abcd;
}

typedef struct { POINT dragPoints[7]; POINT arrowHeadPoints[3]; } DrawDragOut;
DrawDragOut drawDrag(DragGraphics *graphics, Screen screen, HDC memory) {
    double arrowRadiusPt = 14;
    double arrowLengthPt = 14;
    double arrowRadiusPx = ptToPx(arrowRadiusPt, screen.dpi);
    double arrowLengthPx = ptToPx(arrowLengthPt, screen.dpi);
    DrawDragOut result;
    result.dragPoints[0] = graphics->drag[0];
    // as in (tangent, normal) not (sin, cos, tan)
    Point lastTangent = { 0, -1 };
    for (int i = 0; i < graphics->dragCount - 1; i++) {
        POINT aInt = graphics->drag[i];
        POINT dInt = graphics->drag[i + 1];
        Point a = { aInt.x, aInt.y };
        Point d = { dInt.x, dInt.y };
        Point vector = add(d, scale(a, -1));
        Point idealNormal = getIdealNormal(screen, aInt);
        if (idealNormal.x == 0 && idealNormal.y == 0) {
            idealNormal = lastTangent;
        }

        PointPair control = getControlPoints(vector, idealNormal, screen.dpi);
        result.dragPoints[3 * i + 1] = roundPoint(add(a, control.p[0]));
        result.dragPoints[3 * i + 2] = roundPoint(add(d, control.p[1]));
        lastTangent = getFinalTangent(control.p[1]);
        result.dragPoints[3 * i + 3] = dInt;
        if (i == 1 && graphics->arrowHead) {
            Point lastNormal = leftTurn(lastTangent);
            Point e = add(
                d,
                add(
                    scale(lastTangent, -arrowLengthPx),
                    scale(lastNormal, -arrowRadiusPt)
                )
            );
            Point f = add(
                d,
                add(
                    scale(lastTangent, -arrowLengthPx),
                    scale(lastNormal, arrowRadiusPt)
                )
            );
            POINT eInt = { (int)round(e.x), (int)round(e.y) };
            POINT fInt = { (int)round(f.x), (int)round(f.y) };
            result.arrowHeadPoints[0] = eInt;
            result.arrowHeadPoints[1] = dInt;
            result.arrowHeadPoints[2] = fInt;
        }
    }

    for (int i = 0; i < 3 * graphics->dragCount - 2; i++) {
        result.dragPoints[i].x -= screen.left;
        result.dragPoints[i].y -= screen.top;
    }

    HPEN pens[2] = {
        getPen(
            graphics->labelBackground, graphics->borderPx,
            DASH_PEN_STYLE, graphics->dashPx, DRAG_PEN_SLOT
        ),
        getPen(
            graphics->borderColor, graphics->borderPx,
            ALT_DASH_PEN_STYLE, graphics->dashPx, ALT_DRAG_PEN_SLOT
        ),
    };
    for (int i = 0; i < 2; i++) {
        SelectObject(memory, pens[i]);
        PolyBezier(memory, result.dragPoints, 3 * graphics->dragCount - 2);
        if (graphics->arrowHead && graphics->dragCount >= 3) {
            Polyline(memory, result.arrowHeadPoints, 3);
        }
    }

    return result;
}

void eraseLabels(
    LabelGraphics *graphics, HBRUSH keyBrush, DrawLabelsOut drawLabelsOut,
    Screen screen, HDC memory
) {
    for (
        int i = graphics->labelRange.start; i < graphics->labelRange.stop; i++
    ) {
        POINT positionPx = getBubblePositionPx(
            screen, graphics->layout, graphics->offsetPt, i
        );
        BOOL xFlip = positionPx.x >= graphics->mirrorStart.x;
        BOOL yFlip = positionPx.y >= graphics->mirrorStart.y;
        RECT dstRect = drawLabelsOut.labelRects[i];
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
            .left = positionPx.x + xFlip * (1 - drawLabelsOut.earSize.cx),
            .top = positionPx.y + yFlip * (1 - drawLabelsOut.earSize.cy)
        };
        earRect.right = earRect.left + drawLabelsOut.earSize.cx;
        earRect.bottom = earRect.top + drawLabelsOut.earSize.cy;
        FillRect(memory, &earRect, keyBrush);
    }

    // FillRect(memory, &labelBitmapRect, keyBrush);
}

void eraseDrag(
    DragGraphics *graphics, COLORREF colorKey, DrawDragOut drawDragOut,
    HDC memory
) {
    SelectObject(
        memory,
        getPen(
            colorKey, graphics->borderPx,
            BOTH_DASH_PEN_STYLE, graphics->dashPx, ERASE_DRAG_PEN_SLOT
        )
    );
    PolyBezier(memory, drawDragOut.dragPoints, 3 * graphics->dragCount - 2);
    if (graphics->arrowHead && graphics->dragCount >= 3) {
        Polyline(memory, drawDragOut.arrowHeadPoints, 3);
    }
}

typedef enum { DO_ACTIONS_TIMER, REDRAW_TIMER, CHANGE_ICON_TIMER } TimerID;

Graphics *lastGraphics = &graphicsOut[0];
void redraw(Model *model, Screen screen) {
    Graphics *graphics = getGraphics(model, screen);
    if (!memcmp(graphics, lastGraphics, sizeof(Graphics))) { return; }
    lastGraphics = graphics;
    graphicsOutIndex++;
    graphicsOutIndex %= 2;

    HDC device = GetDC(model->window);
    HDC memory = CreateCompatibleDC(device);
    HBRUSH keyBrush = getBrush(graphics->colorKey, KEY_BRUSH_SLOT);
    selectKeyBitmap(
        device, memory, screen.width, screen.height, graphics->colorKey
    );

    DrawLabelsOut drawLabelsOut;
    if (graphics->showLabels) {
        drawLabelsOut = drawLabels(
            &graphics->lg, graphics->colorKey, screen, device, memory
        );
    }

    DrawDragOut drawDragOut;
    if (graphics->dg.dragCount > 0) {
        drawDragOut = drawDrag(&graphics->dg, screen, memory);
    }

    ReleaseDC(model->window, device);

    POINT screenPosition = { screen.left, screen.top + screen.topCrop };
    SIZE screenSize = {
        screen.width,
        screen.height - screen.bottomCrop - screen.topCrop
    };
    POINT memoryPosition = { 0, screen.topCrop };
    UpdateLayeredWindow(
        model->window,
        device,
        &screenPosition,
        &screenSize,
        memory,
        &memoryPosition,
        graphics->colorKey,
        NULL,
        ULW_COLORKEY
    );

    if (graphics->showLabels) {
        eraseLabels(&graphics->lg, keyBrush, drawLabelsOut, screen, memory);
    }

    if (graphics->dg.dragCount > 0) {
        eraseDrag(&graphics->dg, graphics->colorKey, drawDragOut, memory);
    }

    DeleteDC(memory);
    KillTimer(model->dialog, REDRAW_TIMER);
}

LPWSTR parseModel(
    Model *model, LPCBYTE buffer, DWORD bufferSize, BOOL fileExists,
    int *lineNumber
) {
    model->deltaPx = 12;
    model->smallDeltaPx = 1;
    model->bubbleCount = 1200;
    model->minCellArea = 30;
    model->gridMargin = 0.5;
    model->aspect = 4 / 3.0;
    model->angle1 = 15;
    model->angle2 = 60;
    model->fontHeightPt = 0;
    wcsncpy_s(
        model->fontFamily, sizeof(model->fontFamily) / sizeof(WCHAR),
        L"", _TRUNCATE
    );
    model->paddingLeftPt = 0.75;
    model->paddingTopPt = 0.75;
    model->paddingRightPt = 0.75;
    model->paddingBottomPt = 0.75;
    // https://docs.microsoft.com/en-us/windows/win32/gdi/system-palette-and-static-colors
    model->labelBackground = GetSysColor(COLOR_WINDOW);
    model->selectionBackground = GetSysColor(COLOR_HIGHLIGHT);
    model->borderPt = .75;
    model->earHeightPt = 6;
    model->earElevationPt = 4;
    model->borderColor = GetSysColor(COLOR_WINDOWTEXT);
    model->dashPt = 3;
    model->mirrorWidthPt = 100;
    model->mirrorHeightPt = 100;
    model->showCaption = TRUE;
    if (!fileExists) { return NULL; }
    double deltaRange[2] = { 1, 1000 };
    int countRange[2] = { 0, INT_MAX };
    double gridMarginRange[2] = { 0, 100 };
    double aspectRange[2] = { 0.1, 10 };
    double angleRange[2] = { 0, 360 };
    double skewAngleRange[2] = { 1, 90 };
    double nonnegative[2] = { 0, INFINITY };
    double sizeRange[2] = { 1, INFINITY };
    double fontSizeRange[2] = { 1, 96 };
    Hook hooks[] = {
        { .call = expectObject, .frameCount = 0 },
        {
            .call = parseColor,
            .param = NULL,
            .dest = &model->borderColor,
            .frames = { "borderColor" },
            .frameCount = 1,
        },
        {
            .call = parseDouble,
            .param = deltaRange,
            .dest = &model->deltaPx,
            .frames = { "deltaPx" },
            .frameCount = 1,
        },
        {
            .call = parseWideString,
            .param = model->fontFamily + LF_FACESIZE,
            .dest = model->fontFamily,
            .frames = { "font" },
            .frameCount = 1,
        },
        {
            .call = parseDouble,
            .param = fontSizeRange,
            .dest = &model->fontHeightPt,
            .frames = { "fontSize" },
            .frameCount = 1,
        },
        {
            .call = parseDouble,
            .param = aspectRange,
            .dest = &model->aspect,
            .frames = { "grid", "aspectRatio" },
            .frameCount = 2,
        },
        {
            .call = parseDouble,
            .param = sizeRange,
            .dest = &model->minCellArea,
            .frames = { "grid", "cellSize" },
            .frameCount = 2,
        },
        {
            .call = parseDouble,
            .param = gridMarginRange,
            .dest = &model->gridMargin,
            .frames = { "grid", "edgeDensity" },
            .frameCount = 2,
        },
        {
            .call = parseDouble,
            .param = angleRange,
            .dest = &model->angle1,
            .frames = { "grid", "rotation" },
            .frameCount = 2,
        },
        {
            .call = parseDouble,
            .param = skewAngleRange,
            .dest = &model->angle2,
            .frames = { "grid", "skewAngle" },
            .frameCount = 2,
        },
        {
            .call = parseColor,
            .param = NULL,
            .dest = &model->labelBackground,
            .frames = { "labelColor" },
            .frameCount = 1,
        },
        {
            .call = parseInt,
            .param = countRange,
            .dest = &model->bubbleCount,
            .frames = { "labelCount" },
            .frameCount = 1,
        },
        {
            .call = parseBool,
            .param = NULL,
            .dest = &model->showCaption,
            .frames = { "showFrame" },
            .frameCount = 1,
        },
        {
            .call = parseDouble,
            .param = deltaRange,
            .dest = &model->smallDeltaPx,
            .frames = { "smallDeltaPx" },
            .frameCount = 1,
        },
    };
    LPWSTR parseError = parseJSON(
        buffer, buffer + bufferSize, hooks, sizeof(hooks) / sizeof(Hook),
        lineNumber
    );
    model->minCellArea *= model->minCellArea;
    model->angle2 += model->angle1;
    model->angle1 *= PI / 180;
    model->angle2 *= PI / 180;
    return parseError;
}

// TODO: Decide whether we want to keep this
const WCHAR watcherErrorFormat[] = L"File watcher error: Could not %s";
WCHAR watcherErrorString[
    sizeof(watcherErrorFormat) / sizeof(WCHAR) - 2 + WATCHER_VERB_LENGTH - 1
];
const int watcherErrorLength = sizeof(watcherErrorString) / sizeof(WCHAR);

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
BOOL ignoreDestroy = FALSE;
BOOL ignorePop = FALSE;

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
        Model *model = getModel(window);
        redraw(model, getScreen(&model->monitor));
        return 0;
    } else if (message == WM_SETTINGCHANGE) {
        if (wParam == SPI_SETNONCLIENTMETRICS) {
            Model *model = getModel(window);
            model->systemFontChanges++;
            redraw(model, getScreen(&model->monitor));
            return 0;
        }
    } else if (message == WM_DESTROY) {
        if (!ignoreDestroy) { PostQuitMessage(0); }
        return 0;
    }

    return DefWindowProc(window, message, wParam, lParam);
}

TOOLINFO toolInfoOut = {
    .cbSize = sizeof(TOOLINFO),
    .uFlags = 0,
    .hwnd = NULL,
    .uId = (UINT_PTR)NULL,
    .hinst = NULL,
    .lpszText = NULL,
    .lParam = 0,
    .lpReserved = NULL,
};
LPARAM getToolInfo(HWND dialog) {
    toolInfoOut.cbSize = sizeof(TOOLINFO);
    toolInfoOut.hwnd = dialog;
    toolInfoOut.uId = (UINT_PTR)NULL;
    return (LPARAM)&toolInfoOut;
}

BOOL CALLBACK SetFontRedraw(HWND child, LPARAM font){
    SendMessage(child, WM_SETFONT, font, TRUE);
    return TRUE;
}

BOOL CALLBACK SetFontNoRedraw(HWND child, LPARAM font){
    SendMessage(child, WM_SETFONT, font, FALSE);
    return TRUE;
}

BOOL shouldShowDropdown(Model *model) {
    return model->showCaption || GetMenu(model->dialog) != NULL
        || GetFocus() != GetDlgItem(model->dialog, IDC_TEXTBOX);
}

SIZE getMinDialogClientSize(Model *model, UINT dpi) {
    if (model->minTextBoxSize.cy == 0) { return model->minTextBoxSize; }
    int buttonWidth = 0;
    if (shouldShowDropdown(model)) {
        HWND toolbar = GetDlgItem(model->dialog, IDC_TOOLBAR);
        if (toolbar != NULL) {
            RECT buttonRect;
            SendMessage(toolbar, TB_GETRECT, IDC_BUTTON, (LPARAM)&buttonRect);
            buttonWidth = buttonRect.right - buttonRect.left;
        }
    }

    SIZE client = {
        .cx = max(
            model->minTextBoxSize.cx, ptToIntPx(model->textBoxWidthPt, dpi)
        ) + buttonWidth,
        .cy = max(
            model->minTextBoxSize.cy, ptToIntPx(model->textBoxHeightPt, dpi)
        ),
    };
    return client;
}

LPWSTR setNaturalEdge(LPWSTR text, BOOL naturalEdge) {
    LPWSTR edge = L"-"; int edgeLength = wcslen(edge);
    if (!naturalEdge) {
        while (!wcsncmp(text, edge, edgeLength)) { text += edgeLength; }
    } else if (wcsncmp(text, edge, edgeLength)) {
        int length = wcslen(text);
        int capacity = edgeLength + length + 1;
        LPWSTR newText = getText(capacity, TEMP_TEXT_SLOT);
        wcsncpy_s(newText, capacity, edge, edgeLength + 1);
        wcsncpy_s(newText + edgeLength, length + 1, text, length + 1);
        text = newText;
    }

    return text;
}

typedef struct {
    BOOL checked;
    int count; // number of non-empty drag segments
} DragMenuState;

DragMenuState getDragMenuState(Model *model) {
    DragMenuState result = { .checked = FALSE, .count = 0 };
    if (model->dragCount <= 0) { return result; }

    POINT start = model->drag[model->dragCount - 1];
    POINT stop = model->hasMatch ? model->matchPoint : model->naturalPoint;
    result.checked = start.x == stop.x && start.y == stop.y;
    result.count = model->dragCount - result.checked;
    return result;
}

BOOL getDragStarted(DragMenuState state) {
    return state.count > 0 || state.checked;
}

int getClickTextIndex(int count) {
    return (count > 0) + (count >= 3);
}

LPWSTR dragMenuTexts[4] = {
    L"Set &drag start\tComma (,)",
    L"Click, set &drag midpoint\tComma (,)",
    L"Set &drag end\tComma (,)",
    L"Click (without &dragging)\tComma (,)",
};
LPWSTR clickTexts[3] = {
    L"&Click\tSpacebar",
    L"Dra&g\tSpacebar",
    L"Click, then dra&g\tSpacebar",
};
LPWSTR rightClickTexts[3] = {
    L"&Right-click\tPeriod (.)",
    L"Drag with &right mouse button\tPeriod (.)",
    L"Click, then drag with &right mouse button\tPeriod (.)",
};
LPWSTR wheelClickTexts[3] = {
    L"&Click\tSingle-quote (')",
    L"Dra&g\tSingle-quote (')",
    L"Dra&g\tSingle-quote (')",
};
void updateDragMenuState(Model *model, DragMenuState oldState) {
    DragMenuState state = getDragMenuState(model);
    if (state.checked == oldState.checked && state.count == oldState.count) {
        return;
    }

    MENUITEMINFO dragInfo = {
        .cbSize = sizeof(MENUITEMINFO),
        .fMask = MIIM_STATE | MIIM_STRING,
        .fState = state.checked ? MFS_CHECKED : MFS_UNCHECKED,
        .dwTypeData = dragMenuTexts[state.count],
    };
    SetMenuItemInfo(getDropdownMenu(), IDM_DRAG, FALSE, &dragInfo);

    MENUITEMINFO enabledInfo = {
        .cbSize = sizeof(MENUITEMINFO), .fMask = MIIM_STATE,
    };
    BOOL dragStarted = getDragStarted(state);
    if (dragStarted != getDragStarted(oldState)) {
        enabledInfo.fState = dragStarted ? MFS_ENABLED : MFS_DISABLED;
        SetMenuItemInfo(
            getDropdownMenu(), IDM_REMOVE_DRAG, FALSE, &enabledInfo
        );
    }

    if (state.count == oldState.count) { return; }
    int clickTextIndex = getClickTextIndex(state.count);
    MENUITEMINFO wheelInfo = {
        .cbSize = sizeof(MENUITEMINFO),
        .fMask = MIIM_STATE | MIIM_STRING,
        .fState = state.count >= 2 ? MFS_DISABLED : MFS_ENABLED,
        .dwTypeData = wheelClickTexts[clickTextIndex],
    };
    SetMenuItemInfo(getDropdownMenu(), IDM_WHEEL_CLICK, FALSE, &wheelInfo);

    if (clickTextIndex == getClickTextIndex(oldState.count)) { return; }
    MENUITEMINFO textInfo = {
        .cbSize = sizeof(MENUITEMINFO),
        .fMask = MIIM_STRING,
        .dwTypeData = clickTexts[clickTextIndex],
    };
    SetMenuItemInfo(getDropdownMenu(), IDM_CLICK, FALSE, &textInfo);
    textInfo.dwTypeData = rightClickTexts[clickTextIndex];
    SetMenuItemInfo(getDropdownMenu(), IDM_RIGHT_CLICK, FALSE, &textInfo);
    enabledInfo.fState = clickTextIndex > 0 ? MFS_DISABLED : MFS_ENABLED;
    SetMenuItemInfo(getDropdownMenu(), IDM_DOUBLE_CLICK, FALSE, &enabledInfo);
}

void updateShowLabelsChecked(BOOL focused) {
    MENUITEMINFO info = {
        .cbSize = sizeof(MENUITEMINFO),
        .fMask = MIIM_STATE,
        .fState = focused ? MFS_CHECKED : MFS_UNCHECKED,
    };
    SetMenuItemInfo(getDropdownMenu(), IDM_SHOW_LABELS, FALSE, &info);
}

void setMatchPoint(Model *model, POINT matchPoint) {
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    if (
        !model->hasMatch
            || cursorPos.x != model->matchPoint.x
            || cursorPos.y != model->matchPoint.y
    ) {
        model->naturalPoint = cursorPos;
    }

    model->hasMatch = TRUE;
    model->matchPoint = matchPoint;
    SetCursorPos(matchPoint.x, matchPoint.y);
}

void unsetMatchPoint(Model *model) {
    if (model->dragCount > 0 && wcsncmp(model->text, L"-", 1)) {
        POINT dragEnd = model->drag[model->dragCount - 1];
        SetCursorPos(dragEnd.x, dragEnd.y);
        GetCursorPos(&model->naturalPoint);
        model->drag[model->dragCount - 1] = model->naturalPoint;
    } else if (model->hasMatch) {
        POINT cursorPos;
        GetCursorPos(&cursorPos);
        if (
            cursorPos.x == model->matchPoint.x
                && cursorPos.y == model->matchPoint.y
        ) {
            SetCursorPos(model->naturalPoint.x, model->naturalPoint.y);
        }
    }

    model->hasMatch = FALSE;
}

typedef struct {
    HMONITOR oldMonitor, newMonitor, firstMonitor;
    BOOL chooseNext;
} MonitorCallbackVars;
BOOL __stdcall SwitchMonitorCallback(
    HMONITOR monitor, HDC device, LPRECT bounds, LPARAM varsParam
) {
    MonitorCallbackVars *vars = (MonitorCallbackVars*)varsParam;
    if (vars->firstMonitor == NULL) { vars->firstMonitor = monitor; }
    if (vars->chooseNext) { vars->newMonitor = monitor; return FALSE; }
    if (monitor == vars->oldMonitor) { vars->chooseNext = TRUE; }
    return TRUE;
}

void setTooltip(
    Model *model, LPWSTR text, LPCWSTR title, DWORD icon, BOOL autoHide
) {
    if (text == NULL) {
        if (model->toolText != NULL && model->tooltip != NULL) {
            SendMessage(
                model->tooltip, TTM_TRACKACTIVATE, FALSE,
                getToolInfo(model->dialog)
            );
        }

        model->toolText = NULL;
        return;
    }

    LPCWSTR suffix = L"\n(Spacebar to dismiss)";
    int capacity = wcslen(text) + wcslen(suffix) + 1;
    LPWSTR actualText = getText(capacity, TOOLTIP_TEXT_SLOT);
    swprintf_s(actualText, capacity, L"%s%s", text, suffix);

    // https://docs.microsoft.com/en-us/windows/win32/controls/create-a-tooltip-for-a-control
    // https://docs.microsoft.com/en-us/windows/win32/controls/implement-balloon-tooltips
    // Recreate the tooltip window every time, otherwise once you
    // click the close button no future tooltips will display.
    // Incidentally, creating the tooltip window before
    // WM_INITDIALOG causes the tooltip to never display.
    if (model->tooltip != NULL) { DestroyWindow(model->tooltip); }
    model->tooltip = CreateWindow(
        TOOLTIPS_CLASS,
        NULL,
        WS_POPUP | TTS_NOPREFIX | TTS_BALLOON | TTS_CLOSE,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT,
        model->dialog,
        NULL,
        GetWindowInstance(model->dialog),
        NULL
    );
    HWND test = GetDlgItem(model->dialog, 0);
    TOOLINFO info = {
        .cbSize = sizeof(TOOLINFO),
        .uFlags = TTF_IDISHWND | TTF_CENTERTIP | TTF_TRACK | TTF_ABSOLUTE,
        .hwnd = model->dialog,
        .uId = (UINT_PTR)NULL,
        .hinst = NULL,
        .lpszText = actualText,
        .lParam = 0,
        .lpReserved = NULL,
    };
    model->toolText = actualText;
    SendMessage(model->tooltip, TTM_ADDTOOL, 0, (LPARAM)&info);
    SendMessage(model->tooltip, TTM_SETTITLE, icon, (LPARAM)title);
    // https://docs.microsoft.com/en-us/windows/win32/controls/implement-tracking-tooltips
    SendMessage(model->tooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&info);
    RECT frame; GetWindowRect(GetDlgItem(model->dialog, IDC_TEXTBOX), &frame);
    SendMessage(
        model->tooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(
            (frame.left + frame.right) / 2, frame.bottom
        )
    );
    model->autoHideTooltip = autoHide;
}

const int ICON_CHANGE_DELAY_MS = 200;
void changeIcon(Model *model, DWORD resourceID, int delayMs) {
    if (resourceID == 0) {
        KillTimer(model->dialog, CHANGE_ICON_TIMER);
        model->nextIcon = 0;
    } else if (delayMs > 0) {
        model->nextIcon = resourceID;
        SetTimer(model->dialog, CHANGE_ICON_TIMER, delayMs, NULL);
    } else {
        KillTimer(model->dialog, CHANGE_ICON_TIMER);
        model->nextIcon = 0;
        HICON icon = LoadIcon(
            GetModuleHandle(NULL), MAKEINTRESOURCE(resourceID)
        );
        SendMessage(model->window, WM_SETICON, ICON_BIG, (LPARAM)icon);
    }
}

const int PRESSED = 0x8000;
typedef enum {
    WM_APP_FITTOTEXT = WM_APP, WM_APP_SETTINGS_CHANGED, WM_APP_PARSE_SETTINGS,
} AppMessage;
BOOL sleep(Model *model, ActionParam param) {
    SetTimer(model->dialog, DO_ACTIONS_TIMER, param.milliseconds, NULL);
    return FALSE;
}

BOOL mouseButton(Model *model, ActionParam param) {
    INPUT input = {
        .type = INPUT_MOUSE,
        .mi = { 0, 0, 0, param.clickType, 0, 0 },
    };
    SendInput(1, &input, sizeof(input));
    return TRUE;
}

BOOL mouseWheel(Model *model, ActionParam param) {
    INPUT input = {
        .type = INPUT_MOUSE,
        .mi = { 0, 0, param.wheelDelta, MOUSEEVENTF_WHEEL, 0, 0 },
    };
    SendInput(1, &input, sizeof(input));
    return TRUE;
}

BOOL mouseToPoint(Model *model, ActionParam param) {
    SetCursorPos(param.point.x, param.point.y);
    return TRUE;
}

BOOL mouseToDragEnd(Model *model, ActionParam param) {
    SetDlgItemText(model->dialog, IDC_TEXTBOX, L"");
    return TRUE;
}

BOOL clearTextbox(Model *model, ActionParam param) {
    DragMenuState dragMenuState = getDragMenuState(model);
    model->dragCount = 0;
    POINT cursor; GetCursorPos(&cursor);
    model->naturalPoint = cursor;
    updateDragMenuState(model, dragMenuState);
    SetDlgItemText(model->dialog, IDC_TEXTBOX, L"");
    return TRUE;
}

BOOL setIcon(Model *model, ActionParam param) {
    changeIcon(model, param.resourceID, 0);
    return TRUE;
}

void addAction(
    Model *model, BOOL (*function)(Model*, ActionParam), ActionParam param
) {
    model->actions = getActionList(model->actionCount + 1, model->actions);
    model->actions[model->actionCount].function = function;
    model->actions[model->actionCount].param = param;
    model->actionCount++;
}

void doActions(Model *model) {
    if (!IsIconic(model->window)) {
        model->actions += model->actionCount;
        model->actionCount = 0;
        return;
    }

    BOOL keepGoing = TRUE;
    while (keepGoing && model->actionCount > 0) {
        keepGoing = model->actions->function(model, model->actions->param);
        model->actions++;
        model->actionCount--;
    }

    if (keepGoing) {
        // ironically, if keepGoing is true at this point it means we are done
        if (IsIconic(model->window)) {
            ShowWindow(model->window, SW_RESTORE);
            // redraw is necessary to display drag path if labels are hidden
            redraw(model, getScreen(&model->monitor));
        }
    }
}

void addSleep(Model *model, int ms) {
    addAction(model, sleep, actionParamMilliseconds(ms));
}

Point addDrag(
    Model *model, UINT dpi, POINT start, POINT stop, Point idealNormal,
    double durationMs, int segmentCount, int maxInitialPx, int maxSegmentPx
) {
    // segmentCount does not include the initial segment (which may have
    // length zero if maxInitialPx is zero)
    Point a = { start.x, start.y };
    Point d = { stop.x, stop.y };
    Point vector = add(d, scale(a, -1));
    double length = sqrt(dot(vector, vector));
    double initialT = 0;
    if (maxInitialPx > 0) {
        double hypotheticalSegmentCount = length / maxInitialPx;
        if (hypotheticalSegmentCount > segmentCount + 1) {
            initialT = maxInitialPx / length;
        } else {
            initialT = 1.0 / (segmentCount + 1);
        }
    }

    // adjust segmentCount so that the average length of a non-initial segment
    // is at most maxSegmentPx (yes I'm aware that we already used the old
    // segmentCount to calculate initialT, but this would only change that
    // calculation if maxSegmentPx < maxInitialPx, which is not a case we care
    // about)
    double oldSegmentCount = segmentCount;
    segmentCount = max(
        segmentCount,
        (int)ceil(
            segmentCount * length * (1 - initialT) / (
                segmentCount * maxSegmentPx
            )
        )
    );
    // keep the average duration of each segment the same
    durationMs *= segmentCount / oldSegmentCount;

    PointPair control = getControlPoints(vector, idealNormal, dpi);
    Point b = add(a, control.p[0]);
    Point c = add(d, control.p[1]);
    Point initialPoint = interpolateBezier(a, b, c, d, initialT);
    addAction(
        model, mouseToPoint, actionParamPoint(roundPoint(initialPoint))
    );
    int lastMs = 0;
    for (int i = 1; i <= segmentCount; i++) {
        int ms = (int)round(durationMs * i / segmentCount);
        addSleep(model, ms - lastMs);
        lastMs = ms;
        double t = initialT + (1 - initialT) * i / segmentCount;
        Point point = interpolateBezier(a, b, c, d, t);
        addAction(model, mouseToPoint, actionParamPoint(roundPoint(point)));
    }

    return getFinalTangent(control.p[1]);
}

BOOL clickOrDrag(
    Model *model, DragMenuState state, DWORD downType, DWORD upType
) {
    DWORD iconID = ICO_MOUSE;
    switch (downType) {
        case MOUSEEVENTF_LEFTDOWN: iconID = ICO_LEFT_DOWN; break;
        case MOUSEEVENTF_RIGHTDOWN: iconID = ICO_RIGHT_DOWN; break;
        case MOUSEEVENTF_MIDDLEDOWN: iconID = ICO_WHEEL_DOWN; break;
    }

    changeIcon(model, iconID, 0);
    switch (upType) {
        case MOUSEEVENTF_LEFTUP: iconID = ICO_LEFT_UP; break;
        case MOUSEEVENTF_RIGHTUP: iconID = ICO_RIGHT_UP; break;
        case MOUSEEVENTF_MIDDLEUP: iconID = ICO_WHEEL_UP; break;
    }

    POINT cursor;
    GetCursorPos(&cursor);
    // use state.count rather than model->dragCount so that we ignore the
    // final drag segment if it has zero length
    if (state.count > 0) {
        if (
            // this whole condition is equivalent to (state.count > 2) unless
            // something other than this program moved the cursor since the
            // second drag segment was finalized. during testing I kept moving
            // the cursor with the mouse and getting confused why it wasn't
            // clicking before dragging so I changed it back to this.
            model->dragCount > 2 && (
                cursor.x != model->drag[2].x
                    || cursor.y != model->drag[2].y
            )
        ) {
            // if user has moved the cursor since setting the end of the
            // 2-segment drag, assume they are trying to re-activate the
            // starting window or tab before dragging
            addSleep(model, 100);
            addAction(
                model, mouseButton, actionParamClickType(MOUSEEVENTF_LEFTDOWN)
            );
            addAction(
                model, mouseButton, actionParamClickType(MOUSEEVENTF_LEFTUP)
            );
        }

        Screen screen = getScreen(&model->monitor);
        POINT dragStart = model->drag[0];
        addAction(model, mouseToPoint, actionParamPoint(dragStart));
        addSleep(model, 100);
        addAction(model, mouseButton, actionParamClickType(downType));
        POINT dragMid = model->dragCount > 1 ? model->drag[1] : cursor;

        // as in (tangent, normal) not (sin, cos, tan)
        Point lastTangent = { 0, -1 };
        Point idealNormal = getIdealNormal(screen, dragStart);
        if (idealNormal.x == 0 && idealNormal.y == 0) {
            idealNormal = lastTangent;
        }

        lastTangent = addDrag(
            model, screen.dpi, dragStart, dragMid, idealNormal,
            200, // durationMs
            20, // segmentCount
            8, // maxInitialPx
            40 // maxSegmentPx
        );
        if (state.count > 1) {
            addSleep(model, 1000);
            POINT dragStop = model->dragCount > 2 ? model->drag[2] : cursor;

            idealNormal = getIdealNormal(screen, dragStart);
            if (idealNormal.x == 0 && idealNormal.y == 0) {
                idealNormal = lastTangent;
            }

            lastTangent = addDrag(
                model, screen.dpi, dragMid, dragStop, idealNormal,
                200, // durationMs
                20, // segmentCount
                8, // maxInitialPx
                40 // maxSegmentPx
            );
        }
        addSleep(model, 100);
        addAction(model, mouseButton, actionParamClickType(upType));
        addSleep(model, 100);
        addAction(model, setIcon, actionParamResourceID(iconID));
    } else {
        addSleep(model, 100);
        addAction(model, mouseButton, actionParamClickType(downType));
        addAction(model, mouseButton, actionParamClickType(upType));
        addSleep(model, 100);
        changeIcon(model, iconID, ICON_CHANGE_DELAY_MS);
    }

    addAction(model, clearTextbox, actionParamNone);
    ShowWindow(model->window, SW_MINIMIZE);
    doActions(model);
    return TRUE;
}

LRESULT CALLBACK DlgProc(
    HWND dialog,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    if (message == WM_INITDIALOG) {
        Model *model = getModel(dialog);
        DWORD style = GetWindowLongPtr(dialog, GWL_STYLE);
        SetWindowLongPtr(
            dialog, GWL_STYLE,
            model->showCaption ? style | WS_THICKFRAME | WS_CAPTION
                : style & ~WS_THICKFRAME & ~WS_CAPTION
        );
        // if you specify margins other than (3, 3), Windows will change them
        // back when the font changes
        SendMessage(
            GetDlgItem(dialog, IDC_TEXTBOX),
            EM_SETMARGINS,
            EC_LEFTMARGIN | EC_RIGHTMARGIN,
            MAKELPARAM(3, 3)
        );
        UINT dpi = GetDpiForWindow(dialog);
        SendMessage(
            dialog,
            WM_SETFONT,
            (WPARAM)getSystemFont(dpi),
            FALSE
        );
        return TRUE;
    } else if (message == WM_SETFONT) {
        // https://stackoverflow.com/a/17075471
        if (LOWORD(lParam)) {
            EnumChildWindows(dialog, SetFontRedraw, wParam);
        } else {
            EnumChildWindows(dialog, SetFontNoRedraw, wParam);
        }

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
        UINT dpi = GetDpiForWindow(dialog);
        AdjustWindowRectExForDpi(
            &rect,
            GetWindowLongPtr(textBox, GWL_STYLE),
            FALSE, // bMenu
            GetWindowLongPtr(textBox, GWL_EXSTYLE),
            dpi
        );
        SIZE oldMinSize = getMinDialogClientSize(model, model->dpi);
        HWND toolbar = GetDlgItem(dialog, IDC_TOOLBAR);
        BOOL toolbarChanged = toolbar == NULL || dpi != model->dpi
            || rect.bottom - rect.top != model->minTextBoxSize.cy;
        if (toolbarChanged) {
            if (toolbar != NULL) { DestroyWindow(toolbar); }
            toolbar = CreateWindow(
                TOOLBARCLASSNAME,
                NULL,
                WS_VISIBLE | WS_CHILD | WS_TABSTOP | TBSTYLE_LIST,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                dialog,
                (HMENU)IDC_TOOLBAR,
                GetWindowInstance(dialog),
                0
            );
            SendMessage(toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS);
            SendMessage(toolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(0, 0));
            TBBUTTON button = {
                .iBitmap = 0,
                .idCommand = IDC_BUTTON,
                .fsState = TBSTATE_ENABLED,
                .fsStyle = BTNS_WHOLEDROPDOWN | BTNS_AUTOSIZE,
                .bReserved = { 0 },
                .dwData = 0,
                .iString = -1,
            };
            SendMessage(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(button), 0);
            SendMessage(toolbar, TB_ADDBUTTONS, 1, (LPARAM)&button);
            int textBoxHeightPx = max(
                rect.bottom - rect.top, ptToIntPx(model->textBoxHeightPt, dpi)
            );
            SendMessage(
                toolbar, TB_SETPADDING, 0, MAKELPARAM(1, textBoxHeightPx)
            );
            // this can be any arbitrary number
            SendMessage(toolbar, TB_SETBUTTONWIDTH, 0, MAKELPARAM(10, 10));
            SendMessage(toolbar, TB_AUTOSIZE, 0, 0);
        }

        model->minTextBoxSize.cx = rect.right - rect.left;
        model->minTextBoxSize.cy = rect.bottom - rect.top;
        model->dpi = dpi;
        SIZE extraSize = getMinDialogClientSize(model, model->dpi);
        RECT client; GetClientRect(dialog, &client);
        if (oldMinSize.cy > 0 && extraSize.cy != oldMinSize.cy) {
            extraSize.cx -= oldMinSize.cx;
            extraSize.cy -= oldMinSize.cy;
        } else {
            extraSize.cx -= client.right;
            extraSize.cy -= client.bottom;
            if (model->showCaption) {
                extraSize.cx = max(0, extraSize.cx);
                extraSize.cy = max(0, extraSize.cy);
            }
        }

        if (extraSize.cx != 0 || extraSize.cy != 0) {
            RECT frame; GetWindowRect(dialog, &frame);
            SetWindowPos(
                dialog, NULL, 0, 0,
                frame.right + extraSize.cx - frame.left,
                frame.bottom + extraSize.cy - frame.top,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
            );
        } else if (toolbarChanged) {
            SendMessage(
                dialog, WM_SIZE, SIZE_RESTORED,
                MAKELPARAM(client.right, client.bottom)
            );
        }

        // draw labels after dialog size has been finalized to avoid dialog
        // size noticeably changing after labels are drawn
        if (!model->drawnYet) {
            model->drawnYet = TRUE;
            redraw(model, getScreen(&model->monitor));
            if (model->watcherData.folder != INVALID_HANDLE_VALUE) {
                WatcherError startError = startWatcher(
                    &model->watcherData, dialog
                );
                if (startError != WATCHER_SUCCESS) {
                    _snwprintf_s(
                        watcherErrorString, watcherErrorLength, _TRUNCATE,
                        watcherErrorFormat, watcherVerbs[startError]
                    );
                    MessageBox(
                        NULL, watcherErrorString, L"MouseJump Error",
                        MB_ICONERROR
                    );
                }
            }

            if (model->toolText != NULL) {
                setTooltip(
                    model,
                    model->toolText,
                    getErrorTitle(model->settingsPath, model->lineNumber),
                    TTI_ERROR_LARGE,
                    FALSE
                );
            }
        }

        return TRUE;
    } else if (message == WM_SIZE) {
        RECT client;
        GetClientRect(dialog, &client);
        Model *model = getModel(dialog);
        UINT dpi = GetDpiForWindow(dialog);
        int textBoxHeight = max(
            model->minTextBoxSize.cy, ptToIntPx(model->textBoxHeightPt, dpi)
        );
        int buttonWidth = 0;
        HWND toolbar = GetDlgItem(dialog, IDC_TOOLBAR);
        RECT buttonRect;
        if (toolbar != NULL) {
            SendMessage(toolbar, TB_GETRECT, IDC_BUTTON, (LPARAM)&buttonRect);
            buttonWidth = buttonRect.right - buttonRect.left;
        }

        if (shouldShowDropdown(model)) {
            client.right -= buttonWidth;
        }

        HWND textBox = GetDlgItem(dialog, IDC_TEXTBOX);
        int controlTop = (client.bottom - textBoxHeight) / 2;
        SetWindowPos(
            textBox,
            NULL,
            0, controlTop,
            client.right, textBoxHeight,
            SWP_NOZORDER | SWP_NOACTIVATE
        );
        if (toolbar != NULL) {
            RECT toolbarFrame;
            GetWindowRect(toolbar, &toolbarFrame);
            ScreenToClient(dialog, (LPPOINT)&toolbarFrame.left);
            ScreenToClient(dialog, (LPPOINT)&toolbarFrame.right);
            POINT offset = { buttonRect.left, buttonRect.top };
            MapWindowPoints(toolbar, dialog, &offset, 1);
            SetWindowPos(
                toolbar,
                NULL,
                toolbarFrame.left + client.right - offset.x,
                toolbarFrame.top + controlTop - offset.y,
                0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
            );
        }

        if (model->toolText != NULL) {
            RECT frame; GetWindowRect(textBox, &frame);
            SendMessage(
                model->tooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(
                    (frame.left + frame.right) / 2, frame.bottom
                )
            );
        }

        return 0;
    } else if (message == WM_MOVE) {
        Model *model = getModel(dialog);
        if (model->toolText != NULL) {
            HWND textBox = GetDlgItem(dialog, IDC_TEXTBOX);
            RECT frame; GetWindowRect(textBox, &frame);
            SendMessage(
                model->tooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(
                    (frame.left + frame.right) / 2, frame.bottom
                )
            );
        }

        return 0;
    } else if (message == WM_GETMINMAXINFO) {
        if (getModel(dialog)->showCaption) {
            LPMINMAXINFO minMaxInfo = (LPMINMAXINFO)lParam;
            SIZE minSize = getMinDialogClientSize(
                getModel(dialog), GetDpiForWindow(dialog)
            );
            RECT client, frame;
            GetClientRect(dialog, &client); GetWindowRect(dialog, &frame);
            minMaxInfo->ptMinTrackSize.x
                = frame.right + minSize.cx - client.right - frame.left;
            minMaxInfo->ptMinTrackSize.y = minSize.cy
                = frame.bottom + minSize.cy - client.bottom - frame.top;
        }

        return 0;
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
        Model *model = getModel(dialog);
        UINT dpi = HIWORD(wParam);
        SendMessage(
            dialog,
            WM_SETFONT,
            (WPARAM)getSystemFont(dpi),
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
                TPM_RIGHTBUTTON | TPM_RIGHTALIGN,
                point.x, point.y,
                0,
                dialog,
                NULL
            );
        }

        return TRUE;
    } else if (message == WM_SYSCOMMAND && (wParam & 0xFFF0) == SC_KEYMENU) {
        if (GetMenu(dialog) != NULL) { return 0; }
        RECT client; GetClientRect(dialog, &client);
        HWND toolbar = GetDlgItem(dialog, IDC_TOOLBAR);
        if (toolbar != NULL) {
            RECT buttonRect;
            SendMessage(toolbar, TB_GETRECT, IDC_BUTTON, (LPARAM)&buttonRect);
            MapWindowPoints(toolbar, dialog, (LPPOINT)&buttonRect.right, 1);
            client.right = buttonRect.right;
        }

        SetMenu(dialog, getDropdownMenu());
        // using AdjustWindowRectExForDpi causes problems because Windows
        // never actually redraws the title bar to reflect DPI changes
        AdjustWindowRectEx(
            &client,
            GetWindowLongPtr(dialog, GWL_STYLE),
            TRUE,
            GetWindowLongPtr(dialog, GWL_EXSTYLE)
        );
        SetWindowPos(
            dialog, NULL, 0, 0,
            client.right - client.left, client.bottom - client.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
        );
        return 0;
    } else if (message == WM_ENTERMENULOOP) {
        Model *model = getModel(dialog);
        if (model->toolText != NULL) {
            ignorePop = TRUE;
            SendMessage(
                model->tooltip, TTM_TRACKACTIVATE, FALSE, getToolInfo(dialog)
            );
            ignorePop = FALSE;
        }
    } else if (message == WM_EXITMENULOOP) {
        Model *model = getModel(dialog);
        BOOL focused = GetFocus() == GetDlgItem(dialog, IDC_TEXTBOX);
        if (GetMenu(dialog) != NULL) {
            RECT client; GetClientRect(dialog, &client);
            HWND toolbar = GetDlgItem(dialog, IDC_TOOLBAR);
            if (toolbar != NULL) {
                RECT buttonRect;
                SendMessage(
                    toolbar, TB_GETRECT, IDC_BUTTON, (LPARAM)&buttonRect
                );
                if (!model->showCaption && focused) {
                    MapWindowPoints(
                        toolbar, dialog, (LPPOINT)&buttonRect.left, 1
                    );
                    client.right = buttonRect.left;
                } else {
                    MapWindowPoints(
                        toolbar, dialog, (LPPOINT)&buttonRect.right, 1
                    );
                    client.right = buttonRect.right;
                }
            }

            SetMenu(dialog, NULL);
            AdjustWindowRectEx(
                &client,
                GetWindowLongPtr(dialog, GWL_STYLE),
                FALSE,
                GetWindowLongPtr(dialog, GWL_EXSTYLE)
            );
            SetWindowPos(
                dialog, NULL, 0, 0,
                client.right - client.left, client.bottom - client.top,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
            );
        }

        if (model->toolText != NULL && (focused || !model->autoHideTooltip)) {
            SendMessage(
                model->tooltip, TTM_TRACKACTIVATE, TRUE, getToolInfo(dialog)
            );
        }

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
    } else if (message == WM_NOTIFY && ((LPNMHDR)lParam)->code == TTN_POP) {
        Model *model = getModel(dialog);
        if (((LPNMHDR)lParam)->hwndFrom == model->tooltip && !ignorePop) {
            // This program uses toolText to indicate whether the tooltip has
            // been closed or just hidden because the focused changed.
            model->toolText = NULL;
            return TRUE;
        }
    } else if (message == WM_TIMER && wParam == DO_ACTIONS_TIMER) {
        KillTimer(dialog, wParam);
        doActions(getModel(dialog));
        return TRUE;
    } else if (message == WM_TIMER && wParam == REDRAW_TIMER) {
        Model *model = getModel(dialog);
        // redraw kills the timer so we don't have to
        redraw(model, getScreen(&model->monitor));
        return TRUE;
    } else if (message == WM_TIMER && wParam == CHANGE_ICON_TIMER) {
        Model *model = getModel(dialog);
        // changeIcon kills the timer so we don't have to
        changeIcon(model, model->nextIcon, 0);
        return TRUE;
    } else if (message == WM_NCHITTEST && skipHitTest) {
        return HTTRANSPARENT;
    } else if (message == WM_APP_SETTINGS_CHANGED) {
        readWatchedFile((LoadRequest*)lParam);
        return TRUE;
    } else if (message == WM_APP_PARSE_SETTINGS) {
        ParseRequest *request = (ParseRequest*)lParam;
        Model *model = getModel(dialog);
        BOOL showCaptionOld = model->showCaption;
        int lineNumber;
        LPWSTR parseError = parseModel(
            model,
            request->buffer,
            request->size,
            request->event != INVALID_HANDLE_VALUE,
            &lineNumber
        );
        if (request->event != INVALID_HANDLE_VALUE) {
            SetEvent(request->event);
        }

        if (model->showCaption != showCaptionOld) {
            RECT oldClient; GetClientRect(dialog, &oldClient);
            ClientToScreen(dialog, (LPPOINT)&oldClient.left);
            DWORD selectionStart, selectionStop;
            SendMessage(
                GetDlgItem(dialog, IDC_TEXTBOX), EM_GETSEL,
                (WPARAM)&selectionStart, (LPARAM)&selectionStop
            );
            ignoreDestroy = TRUE;
            DestroyWindow(dialog);
            ignoreDestroy = FALSE;
            model->minTextBoxSize.cx = model->minTextBoxSize.cy = 0;
            model->dialog = CreateDialog(
                GetModuleHandle(NULL), L"TOOL", model->window, DlgProc
            );
            model->watcherData.window = model->dialog;
            // force window style to take effect before measuring
            SetWindowPos(
                model->dialog, NULL, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
            );
            RECT client; GetClientRect(model->dialog, &client);
            RECT frame; GetWindowRect(model->dialog, &frame);
            ClientToScreen(model->dialog, (LPPOINT)&client.left);
            SetWindowPos(
                model->dialog,
                NULL,
                frame.left - client.left + oldClient.left,
                frame.top - client.top + oldClient.top,
                frame.right + oldClient.right - client.right - frame.left,
                frame.bottom + oldClient.bottom - client.bottom - frame.top,
                SWP_NOZORDER | SWP_NOACTIVATE
            );
            SetDlgItemText(model->dialog, IDC_TEXTBOX, model->text);
            SendMessage(
                GetDlgItem(model->dialog, IDC_TEXTBOX), EM_SETSEL,
                selectionStart, selectionStop
            );
        }

        setTooltip(
            model,
            parseError,
            getErrorTitle(model->settingsPath, lineNumber),
            TTI_ERROR_LARGE,
            FALSE
        );
        redraw(model, getScreen(&model->monitor));
        return TRUE;
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
            } else if (
                // when activated from the menu, just let the menu close
                command == IDC_BUTTON && HIWORD(wParam) != 0
            ) {
                Model *model = getModel(dialog);
                if (model->toolText != NULL) {
                    SendMessage(
                        model->tooltip,
                        TTM_TRACKACTIVATE, FALSE, getToolInfo(dialog)
                    );
                    model->toolText = NULL;
                    if (GetFocus() == GetDlgItem(dialog, IDC_TEXTBOX)) {
                        // don't show menu when tooltip is open and textbox is
                        // focused
                        return TRUE;
                    }
                }

                // https://docs.microsoft.com/en-us/windows/win32/controls/handle-drop-down-buttons
                LPNMTOOLBAR toolbarMessage = (LPNMTOOLBAR)lParam;
                RECT buttonRect = { 0, 0, 0, 1 };
                HWND toolbar = GetDlgItem(dialog, IDC_TOOLBAR);
                SendMessage(
                    toolbar, TB_GETRECT, command, (LPARAM)&buttonRect
                );
                MapWindowPoints(
                    toolbar, HWND_DESKTOP, (LPPOINT)&buttonRect, 2
                );
                buttonRect.bottom--; // unexplained off-by-one
                TPMPARAMS popupParams;
                popupParams.cbSize = sizeof(popupParams);
                popupParams.rcExclude = buttonRect;
                TrackPopupMenuEx(
                    GetSubMenu(getDropdownMenu(), 0),
                    TPM_VERTICAL | TPM_LEFTBUTTON | TPM_RIGHTALIGN,
                    buttonRect.right,
                    buttonRect.bottom,
                    dialog,
                    &popupParams
                );

                return TRUE;
            } else if (command == IDM_TO_LABEL) {
                HWND textBox = GetDlgItem(dialog, IDC_TEXTBOX);
                SendMessage(textBox, EM_SETSEL, 0, -1);
                SetFocus(textBox);
                Model *model = getModel(dialog);
                setTooltip(
                    model, L"Type the label text to move the mouse",
                    L"Move to label", TTI_INFO_LARGE, TRUE
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
                DragMenuState dragMenuState = getDragMenuState(model);
                Screen screen = getScreen(&model->monitor);
                Point oldOffsetPt = model->offsetPt;
                if (slow) {
                    model->offsetPt.x += xDirection
                        * pxToPt(model->smallDeltaPx, screen.dpi);
                    model->offsetPt.y += yDirection
                        * pxToPt(model->smallDeltaPx, screen.dpi);
                } else {
                    model->offsetPt.x += xDirection
                        * pxToPt(model->deltaPx, screen.dpi);
                    model->offsetPt.y += yDirection
                        * pxToPt(model->deltaPx, screen.dpi);
                }

                LPWSTR newText = model->text;
                Layout layout = getLayout(model, screen);
                LabelRange labelRange = getLabelRange(
                    model->text, layout.count
                );
                if (labelRange.matchLength > 0) {
                    POINT cursorPos = getBubblePositionPx(
                        screen, layout, model->offsetPt, labelRange.start
                    );
                    cursorPos.x += screen.left;
                    cursorPos.y += screen.top;
                    setMatchPoint(model, cursorPos);
                } else {
                    LPWSTR temp = model->text;
                    model->text = L"-";
                    unsetMatchPoint(model);
                    model->text = temp;
                    POINT cursorPos;
                    GetCursorPos(&cursorPos);
                    cursorPos.x += ptToIntPx(model->offsetPt.x, screen.dpi)
                        - ptToIntPx(oldOffsetPt.x, screen.dpi);
                    cursorPos.y += ptToIntPx(model->offsetPt.y, screen.dpi)
                        - ptToIntPx(oldOffsetPt.y, screen.dpi);
                    SetCursorPos(cursorPos.x, cursorPos.y);
                    if (model->dragCount > 0) {
                        GetCursorPos(&model->naturalPoint);
                        POINT dragEnd = model->drag[model->dragCount - 1];
                        newText = setNaturalEdge(
                            model->text,
                            model->naturalPoint.x != dragEnd.x
                                || model->naturalPoint.y != dragEnd.y
                        );
                    }
                }

                updateDragMenuState(model, dragMenuState);
                if (newText != model->text) {
                    HWND textBox = GetDlgItem(dialog, IDC_TEXTBOX);
                    DWORD start, stop;
                    SendMessage(
                        textBox, EM_GETSEL, (WPARAM)&start, (LPARAM)&stop
                    );
                    int delta = wcslen(newText) - wcslen(model->text);
                    SetWindowText(textBox, newText);
                    SendMessage(
                        textBox, EM_SETSEL, start + delta, stop + delta
                    );
                } else {
                    redraw(model, screen);
                }

                return TRUE;
            } else if (command == IDM_CLICK) {
                Model *model = getModel(dialog);
                DragMenuState state = getDragMenuState(model);
                return clickOrDrag(
                    model, state, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP
                );
            } else if (command == IDM_RIGHT_CLICK) {
                Model *model = getModel(dialog);
                DragMenuState state = getDragMenuState(model);
                return clickOrDrag(
                    model, state, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP
                );
            } else if (command == IDM_WHEEL_CLICK) {
                Model *model = getModel(dialog);
                DragMenuState state = getDragMenuState(model);
                // use state.count instead of model->dragCount to ignore empty
                // segments
                if (state.count >= 2) {
                    return FALSE; // 2-segment wheel drag is just not useful
                }

                return clickOrDrag(
                    model, state, MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP
                );
            } else if (command == IDM_DOUBLE_CLICK) {
                Model *model = getModel(dialog);
                DragMenuState state = getDragMenuState(model);
                if (state.count > 0) { return FALSE; }
                changeIcon(model, ICO_DOUBLE_DOWN, 0);
                addSleep(model, 100);
                addAction(
                    model, mouseButton,
                    actionParamClickType(MOUSEEVENTF_LEFTDOWN)
                );
                addAction(
                    model, mouseButton,
                    actionParamClickType(MOUSEEVENTF_LEFTUP)
                );
                addAction(
                    model, mouseButton,
                    actionParamClickType(MOUSEEVENTF_LEFTDOWN)
                );
                addAction(
                    model, mouseButton,
                    actionParamClickType(MOUSEEVENTF_LEFTUP)
                );
                addSleep(model, 100);
                changeIcon(model, ICO_DOUBLE_UP, ICON_CHANGE_DELAY_MS);
                addAction(model, clearTextbox, actionParamNone);
                ShowWindow(model->window, SW_MINIMIZE);
                doActions(model);
                return TRUE;
            } else if (command == IDM_ROTATE_UP) {
                Model *model = getModel(dialog);
                changeIcon(model, ICO_UPWARD_DOWN, 0);

                HWND focus = GetFocus();
                if (focus == GetDlgItem(dialog, IDC_TEXTBOX)) {
                    HWND nextControl = GetNextDlgTabItem(
                        dialog, focus, FALSE
                    );
                    if (nextControl) {
                        SetFocus(nextControl);
                        // manually redraw because unfocusing the textbox
                        // redraws after a zero-second timer rather than
                        // immediately
                        redraw(model, getScreen(&model->monitor));
                    }
                }

                mouseWheel(model, actionParamWheelDelta(WHEEL_DELTA));
                changeIcon(model, ICO_UPWARD_UP, ICON_CHANGE_DELAY_MS);
                return TRUE;
            } else if (command == IDM_ROTATE_DOWN) {
                Model *model = getModel(dialog);
                changeIcon(model, ICO_DOWNWARD_DOWN, 0);

                HWND focus = GetFocus();
                if (focus == GetDlgItem(dialog, IDC_TEXTBOX)) {
                    HWND nextControl = GetNextDlgTabItem(
                        dialog, focus, FALSE
                    );
                    if (nextControl) {
                        SetFocus(nextControl);
                        // manually redraw because unfocusing the textbox
                        // redraws after a zero-second timer rather than
                        // immediately
                        redraw(model, getScreen(&model->monitor));
                    }
                }

                mouseWheel(model, actionParamWheelDelta(-WHEEL_DELTA));
                changeIcon(model, ICO_DOWNWARD_UP, ICON_CHANGE_DELAY_MS);
                return TRUE;
            } else if (command == IDM_DRAG) {
                Model *model = getModel(dialog);
                int oldActionCount = model->actionCount;

                POINT cursor;
                GetCursorPos(&cursor);
                BOOL erase = FALSE;
                if (model->dragCount > 0) {
                    POINT start = model->drag[model->dragCount - 1];
                    erase = start.x == cursor.x && start.y == cursor.y;
                }

                DragMenuState dragMenuState = getDragMenuState(model);

                LPWSTR newText = NULL;
                if (erase) {
                    model->dragCount--;
                    model->naturalPoint = model->drag[model->dragCount];
                    newText = model->dragCount > 0 ? L"-" : L"";
                } else if (model->dragCount < 3) {
                    model->naturalPoint = cursor;
                    model->drag[model->dragCount] = cursor;
                    model->dragCount++;
                    newText = L"";
                    if (model->dragCount == 2) {
                        changeIcon(model, ICO_LEFT_DOWN, 0);
                        addSleep(model, 100);
                        addAction(
                            model,
                            mouseButton,
                            actionParamClickType(MOUSEEVENTF_LEFTDOWN)
                        );
                        addAction(
                            model,
                            mouseButton,
                            actionParamClickType(MOUSEEVENTF_LEFTUP)
                        );
                        addSleep(model, 100);
                        changeIcon(model, ICO_LEFT_UP, ICON_CHANGE_DELAY_MS);
                    }
                } else {
                    changeIcon(model, ICO_LEFT_DOWN, 0);
                    addSleep(model, 100);
                    addAction(
                        model,
                        mouseButton,
                        actionParamClickType(MOUSEEVENTF_LEFTDOWN)
                    );
                    addAction(
                        model,
                        mouseButton,
                        actionParamClickType(MOUSEEVENTF_LEFTUP)
                    );
                    addSleep(model, 100);
                    changeIcon(model, ICO_LEFT_UP, ICON_CHANGE_DELAY_MS);
                    addAction(model, mouseToDragEnd, actionParamNone);
                }

                if (newText) {
                    HWND textBox = GetDlgItem(model->dialog, IDC_TEXTBOX);
                    SetWindowText(textBox, newText);
                    int length = wcslen(newText);
                    SendMessage(textBox, EM_SETSEL, length, length);
                }

                updateDragMenuState(model, dragMenuState);

                if (model->actionCount > oldActionCount) {
                    ShowWindow(model->window, SW_MINIMIZE);
                    doActions(model);
                }

                return TRUE;
            } else if (command == IDM_REMOVE_DRAG) {
                Model *model = getModel(dialog);
                if (model->dragCount > 0) {
                    DragMenuState dragMenuState = getDragMenuState(model);
                    model->dragCount--;
                    model->naturalPoint = model->drag[model->dragCount];
                    SetCursorPos(
                        model->naturalPoint.x, model->naturalPoint.y
                    );
                    updateDragMenuState(model, dragMenuState);
                    LPCWSTR newText = model->dragCount > 0 ? L"-" : L"";
                    HWND textBox = GetDlgItem(model->dialog, IDC_TEXTBOX);
                    SetWindowText(textBox, newText);
                    int length = wcslen(newText);
                    SendMessage(textBox, EM_SETSEL, length, length);
                }

                return TRUE;
            } else if (command == IDM_SHOW_LABELS) {
                HWND nextControl = GetNextDlgTabItem(
                    dialog, GetFocus(), FALSE
                );
                if (nextControl) { SetFocus(nextControl); }
                return TRUE;
            } else if (command == IDM_KEEP_OPEN) {
                Model *model = getModel(dialog);
                model->keepOpen = !model->keepOpen;
                ITaskbarList3 *taskbar = getTaskbar();
                if (model->keepOpen) {
                    HICON icon = LoadIcon(
                        GetModuleHandle(NULL), MAKEINTRESOURCE(ICO_PIN)
                    );
                    taskbar->lpVtbl->SetOverlayIcon(
                        taskbar, model->window, icon, L"Keep open"
                    );
                } else {
                    taskbar->lpVtbl->SetOverlayIcon(
                        taskbar, model->window, NULL, NULL
                    );
                }

                MENUITEMINFO info = {
                    .cbSize = sizeof(MENUITEMINFO),
                    .fMask = MIIM_STATE,
                    .fState = model->keepOpen ? MFS_CHECKED : MFS_UNCHECKED,
                };
                SetMenuItemInfo(
                    getDropdownMenu(), IDM_KEEP_OPEN, FALSE, &info
                );
                return TRUE;
            } else if (command == IDM_SWITCH_MONITOR) {
                Model *model = getModel(dialog);
                MonitorCallbackVars vars = {
                    .oldMonitor = model->monitor,
                    .newMonitor = NULL,
                    .firstMonitor = NULL,
                    .chooseNext = FALSE,
                };
                EnumDisplayMonitors(
                    NULL, NULL, SwitchMonitorCallback, (LPARAM)&vars
                );
                if (vars.newMonitor == NULL && vars.chooseNext) {
                    vars.newMonitor = vars.firstMonitor;
                }

                if (vars.newMonitor == NULL) {
                    POINT origin = { 0, 0 };
                    vars.newMonitor = MonitorFromPoint
                        (origin, MONITOR_DEFAULTTOPRIMARY
                    );
                }

                model->monitor = vars.newMonitor;
                redraw(model, getScreen(&model->monitor));
            }
        } else if (LOWORD(wParam) == IDC_TEXTBOX) {
            if (
                HIWORD(wParam) == EN_SETFOCUS
                    || HIWORD(wParam) == EN_KILLFOCUS
            ) {
                if (LOWORD(wParam) != IDC_TEXTBOX) { return TRUE; }

                HWND focus = GetFocus();
                BOOL focused = focus == GetDlgItem(dialog, IDC_TEXTBOX);
                updateShowLabelsChecked(focused);

                // redraw in case label visibility changed
                SetTimer(dialog, REDRAW_TIMER, 0, NULL);

                Model *model = getModel(dialog);
                if (model->showCaption && model->toolText == NULL) {
                    return TRUE;
                }

                HWND toolbar = NULL;
                if (!model->showCaption) {
                    toolbar = GetDlgItem(dialog, IDC_TOOLBAR);
                }

                if (toolbar != NULL) {
                    RECT buttonRect;
                    SendMessage(
                        toolbar, TB_GETRECT, IDC_BUTTON, (LPARAM)&buttonRect
                    );
                    MapWindowPoints(toolbar, dialog, (LPPOINT)&buttonRect, 2);
                    if (focused) {
                        buttonRect.right = buttonRect.left;
                    }

                    RECT client; GetClientRect(dialog, &client);
                    RECT frame; GetWindowRect(dialog, &frame);
                    SetWindowPos(
                        dialog, NULL, 0, 0,
                        frame.right + buttonRect.right - client.right
                            - frame.left,
                        frame.bottom - frame.top,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
                    );
                }

                if (
                    model->autoHideTooltip && model->toolText != NULL
                        // Tooltip should not disappear when clicked.
                        && focus != model->tooltip
                ) {
                    // keep model->toolText from being set to NULL,
                    // otherwise the tooltip won't reappear when the focus
                    // changes back
                    ignorePop = TRUE;
                    SendMessage(
                        model->tooltip, TTM_TRACKACTIVATE, focused,
                        (LPARAM)getToolInfo(dialog)
                    );
                    ignorePop = FALSE;
                }

                return TRUE;
            } else if (HIWORD(wParam) == EN_UPDATE) {
                HWND textBox = GetDlgItem(dialog, IDC_TEXTBOX);
                getModel(dialog)->text = getTextBoxText(textBox);
                SendMessage(dialog, WM_APP_FITTOTEXT, 0, 0);
                return TRUE;
            } else if (HIWORD(wParam) == EN_CHANGE) {
                Model *model = getModel(dialog);

                DragMenuState dragMenuState = getDragMenuState(model);

                Screen screen = getScreen(&model->monitor);
                Layout layout = getLayout(model, screen);
                LabelRange labelRange = getLabelRange(
                    model->text, layout.count
                );
                if (labelRange.matchLength > 0) {
                    POINT cursorPos = getBubblePositionPx(
                        screen, layout, model->offsetPt, labelRange.start
                    );
                    cursorPos.x += screen.left;
                    cursorPos.y += screen.top;
                    setMatchPoint(model, cursorPos);
                } else {
                    unsetMatchPoint(model);
                }

                updateDragMenuState(model, dragMenuState);

                redraw(model, screen);

                SendMessage(
                    model->tooltip, TTM_TRACKACTIVATE, FALSE,
                    (LPARAM)getToolInfo(dialog)
                );
                model->toolText = NULL;
                return TRUE;
            }
        }
    } else if (message == WM_CLOSE) {
        if (!ignoreDestroy) { PostQuitMessage(0); }
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
        if (
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
                        textBox, EM_GETSEL,
                        (WPARAM)&selectionStart, (LPARAM)&selectionStop
                    );
                    if (
                        selectionStart < GetWindowTextLength(textBox) && (
                            message->wParam == VK_RIGHT || selectionStop > 0
                        )
                    ) { return 0; }
                }
            }
        } else if (message->wParam == VK_BACK) {
            if (
                getModifiers() == 0
                    && GetFocus() == GetDlgItem(dialog, IDC_TEXTBOX)
            ) { return 0; }
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
        .settingsPath = getSettingsPath(L"settings.json"),
        .tooltip = NULL,
        .toolText = NULL,
        .autoHideTooltip = FALSE,
        .keepOpen = FALSE,
        .monitor = MonitorFromWindow(
            GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY
        ),
        .drawnYet = FALSE,
        .colorKey = RGB(255, 0, 255),
        .offsetPt = { 0, 0 },
        .systemFontChanges = 0,
        .textBoxWidthPt = 23.25,
        .textBoxHeightPt = 17.25,
        .minTextBoxSize = { 0, 0 },
        .text = L"",
        .hasMatch = FALSE,
        .dragCount = 0,
        .actions = NULL,
        .actionCount = 0,
        .nextIcon = 0,
    };
    WatcherError initError = initializeWatcher(
        &model.watcherData, model.settingsPath,
        WM_APP_SETTINGS_CHANGED, WM_APP_PARSE_SETTINGS
    );
    if (
        initError != WATCHER_SUCCESS && initError != WATCHER_OPEN_FOLDER
    ) {
        _snwprintf_s(
            watcherErrorString, watcherErrorLength, _TRUNCATE,
            watcherErrorFormat, watcherVerbs[initError]
        );
        MessageBox(
            NULL, watcherErrorString, L"MouseJump Error", MB_ICONERROR
        );
    }

    DWORD contentSize;
    WatcherError loadError = watcherReadFile(
        model.watcherData.path, &model.watcherData.content, &contentSize
    );
    if (
        loadError != WATCHER_SUCCESS && (
            loadError != WATCHER_OPEN_FILE || (
                GetLastError() != ERROR_FILE_NOT_FOUND
                    && GetLastError() != ERROR_PATH_NOT_FOUND
            )
        )
    ) {
        _snwprintf_s(
            watcherErrorString, watcherErrorLength, _TRUNCATE,
            watcherErrorFormat, watcherVerbs[loadError]
        );
        MessageBox(
            NULL, watcherErrorString, L"MouseJump Error", MB_ICONERROR
        );
    }

    model.toolText = parseModel(
        &model, model.watcherData.content, contentSize,
        loadError == WATCHER_SUCCESS, &model.lineNumber
    );

    WNDCLASS windowClass = {
        .lpfnWndProc   = WndProc,
        .hInstance     = hInstance,
        .hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(ICO_MOUSE)),
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

    WatcherError stopError = stopWatcher(&model.watcherData);
    if (stopError != WATCHER_SUCCESS) {
        _snwprintf_s(
            watcherErrorString, watcherErrorLength, _TRUNCATE,
            watcherErrorFormat, watcherVerbs[stopError]
        );
        MessageBox(
            NULL, watcherErrorString, L"MouseJump Error", MB_ICONERROR
        );
    }

    destroyCache();
    return 0;
}

/*
To run, download and install Build Tools for Visual Studio 2017 RC
https://www.visualstudio.com/downloads/#build-tools-for-visual-studio-2017-rc

Open "Developer Command Prompt for VS 2017 RC" under "Visual Studio 2017 RC"
in the start menu

cd /Users/Evan/Documents/GitHub/mousejump
cl /EHsc /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS mousejump.cpp && mousejump.exe

To debug, insert this snippet after the thing that probably went wrong:

if (GetLastError()) {
  TCHAR buffer[20];
  _itow(GetLastError(), buffer, 10);
  MessageBox(NULL, buffer, _T("Error Code"), NULL);
}

Then look up the error code here:
https://msdn.microsoft.com/en-us/library/windows/desktop/ms681381.aspx

For some reason, calling the Font constructor always causes Error 122,
ERROR_INSUFFICIENT_BUFFER, so if you see that error, you can probably ignore
it.
*/

#include <windows.h>
#include <tchar.h>
#include <gdiplus.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <string>

#pragma comment(linker, "/subsystem:windows")
#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "gdiplus")

using namespace Gdiplus;
using namespace std;

typedef struct {
  HDC deviceContext;
  HBITMAP bitmap;
  POINT start;
  HGDIOBJ originalBitmap;
} View;

View getView() {
  View view;
  view.deviceContext = NULL;
  view.bitmap = NULL;
  view.start = {0, 0};
  view.originalBitmap = NULL;
  return view;
}

void destroyView(View view) {
  if (view.deviceContext) {
    SelectObject(view.deviceContext, view.originalBitmap);
    view.originalBitmap = NULL;

    DeleteDC(view.deviceContext);
    view.deviceContext = NULL;
  }

  if (view.bitmap) {
    DeleteObject(view.bitmap);
    view.bitmap = NULL;
  }

  view.start = {0, 0};
}

bool updateView(HMONITOR monitor, View& view) {
  MONITORINFOEX monitorInfo;
  monitorInfo.cbSize = sizeof(MONITORINFOEX);
  if (!GetMonitorInfo(monitor, &monitorInfo)) {
    return false;
  }

  HDC infoContext = CreateIC(_T("DISPLAY"), monitorInfo.szDevice, NULL, NULL);
  if (!infoContext) {
    return false;
  }

  destroyView(view);

  view.deviceContext = CreateCompatibleDC(infoContext);
  view.bitmap = CreateCompatibleBitmap(
    infoContext,
    monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
    monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top
  );

  DeleteDC(infoContext);

  view.start = {monitorInfo.rcMonitor.left, monitorInfo.rcMonitor.top};
  view.originalBitmap = SelectObject(view.deviceContext, view.bitmap);

  return true;
}

Color getSystemColor(int colorConstant) {
  COLORREF colorBytes = GetSysColor(colorConstant);
  return Color(
    GetRValue(colorBytes),
    GetGValue(colorBytes),
    GetBValue(colorBytes)
  );
}

LOGFONT getSystemTooltipFont() {
  NONCLIENTMETRICS metrics;
  metrics.cbSize = sizeof(NONCLIENTMETRICS);
  SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &metrics, 0);
  return metrics.lfStatusFont;
}

int getPixelsPerLogicalInch(HMONITOR monitor) {
  MONITORINFOEX monitorInfo;
  monitorInfo.cbSize = sizeof(MONITORINFOEX);
  GetMonitorInfo(monitor, &monitorInfo);
  HDC infoContext = CreateIC(_T("DISPLAY"), monitorInfo.szDevice, NULL, NULL);
  int pixelsPerLogicalInch = GetDeviceCaps(infoContext, LOGPIXELSY);
  DeleteDC(infoContext);

  return pixelsPerLogicalInch;
}

double logicalHeightToPointSize(int logicalHeight, HMONITOR monitor) {
  double pixelsPerLogicalInch = getPixelsPerLogicalInch(monitor);
  return abs(logicalHeight) * 72 / pixelsPerLogicalInch;
}

Size getTextSize(const Graphics &graphics, const Font *font, LPTSTR text) {
  RectF bounds;
  graphics.MeasureString(text, -1, font, PointF(0.0f, 0.0f), &bounds);
  return Size((int)round(bounds.Width), (int)round(bounds.Height));
}

int getChosenWidth(
  const Graphics &graphics, const Font *font, LPTSTR text, int chosenChars
) {
  RectF bounds;
  graphics.MeasureString(text, -1, font, PointF(0.0f, 0.0f), &bounds);
  REAL fullWidth = bounds.Width;

  graphics.MeasureString(
    text, chosenChars, font, PointF(0.0f, 0.0f), &bounds
  );
  REAL chosenWidth = bounds.Width;

  graphics.MeasureString(
    &text[chosenChars], -1, font, PointF(0.0f, 0.0f), &bounds
  );
  REAL unchosenWidth = bounds.Width;

  return (int)round(0.5 * (chosenWidth + fullWidth - unchosenWidth));
}

StringFormat *getCenteredFormat() {
  static StringFormat format(
    StringFormatFlagsNoWrap | StringFormatFlagsNoClip,
    LANG_NEUTRAL
  );
  format.SetAlignment(StringAlignmentCenter);
  format.SetLineAlignment(StringAlignmentCenter);
  format.SetTrimming(StringTrimmingNone);
  return &format;
}

int getBitmapWidth(HBITMAP bitmap) {
  BITMAP bitmapInfo;
  GetObject(bitmap, sizeof(BITMAP), &bitmapInfo);
  return bitmapInfo.bmWidth;
}

int getBitmapHeight(HBITMAP bitmap) {
  BITMAP bitmapInfo;
  GetObject(bitmap, sizeof(BITMAP), &bitmapInfo);
  return bitmapInfo.bmHeight;
}

typedef struct {
  // Keycode reference:
  // https://msdn.microsoft.com/en-us/library/windows/desktop/dd375731.aspx
  LPTSTR spelling;
  int keycode;
} Symbol;

typedef struct {
  double cellWidth;
  double cellHeight;
  double pixelsPastEdge;
} GridSettings;

double inflatedArea(double width, double height, double border) {
  return (width + 2 * border) * (height + 2 * border);
}

int getBubbleCount(
  vector<vector<Symbol> > levels,
  GridSettings gridSettings,
  int width, int height
) {
  int symbolLimit = 1;
  for (int i = 0; i < levels.size(); i++) {
    symbolLimit *= levels[i].size();
  }

  double cellArea =
    gridSettings.cellWidth * gridSettings.cellHeight * sqrt(0.75);
  double gridArea = inflatedArea(width, height, gridSettings.pixelsPastEdge);
  int areaLimit = (int)round(gridArea / cellArea);

  return min(symbolLimit, areaLimit);
}

int digitsToNumber(vector<int> bases, vector<int> digits) {
  int number = 0;
  if (digits.size() > 0 && bases.size() > 0) {
    number = digits[0];
  }

  for (int i = 1; i < bases.size(); i++) {
    number *= bases[i - 1];
    if (i < digits.size()) {
      number += digits[i];
    }
  }

  return number;
}

vector<int> numberToDigits(vector<int> bases, int number) {
  vector<int> digits(bases.size());
  for (int i = bases.size() - 1; i > 0; i--) {
    digits[i] = number % bases[i - 1];
    number /= bases[i - 1];
  }
  digits[0] = number;
  return digits;
}

int getProduct(vector<int> factors) {
  int product = 1;
  for (int i = 0; i < factors.size(); i++) {
    product *= factors[i];
  }
  return product;
}

int getShorterWords(vector<int> bases, int wordCount) {
  int extraWords = getProduct(bases) - wordCount;
  int shorteningCost = bases[bases.size() - 1] - 1;
  return extraWords / shorteningCost;
}

int getFullWords(vector<int> bases, int wordCount) {
  return wordCount - getShorterWords(bases, wordCount);
}

int ceilDivide(int x, int y) {
  return (x + y - 1) / y;
}

int getWordIndexStart(vector<int> bases, int wordCount, vector<int> word) {
  int fullWords = getFullWords(bases, wordCount);
  int fullWordIndex = digitsToNumber(bases, word);
  if (fullWordIndex < fullWords) {
    return fullWordIndex;
  }

  vector<int> shorterBases(bases.begin(), --bases.end());
  int skippedShorterWords = ceilDivide(fullWords, bases[bases.size() - 1]);
  int shorterWordIndex = digitsToNumber(shorterBases, word);
  return shorterWordIndex + fullWords - skippedShorterWords;
}

int getWordIndexStop(vector<int> bases, int wordCount, vector<int> word) {
  if (word.size() <= 0) {
    return wordCount;
  }

  vector<int> highWord(word);
  highWord[highWord.size() - 1]++;
  return getWordIndexStart(bases, wordCount, highWord);
}

int getWordChoices(vector<int> bases, int wordCount, vector<int> word) {
  int start = getWordIndexStart(bases, wordCount, word);
  int stop = getWordIndexStop(bases, wordCount, word);
  return stop - start;
}

vector<int> getWord(vector<int> bases, int wordCount, int wordIndex) {
  int fullWords = getFullWords(bases, wordCount);
  if (wordIndex < fullWords) {
    return numberToDigits(bases, wordIndex);
  }

  vector<int> shorterBases(bases.begin(), --bases.end());
  int skippedShorterWords = ceilDivide(fullWords, bases[bases.size() - 1]);
  int shorterWordIndex = wordIndex - fullWords + skippedShorterWords;
  return numberToDigits(shorterBases, shorterWordIndex);
}

vector<int> getBases(vector<vector<Symbol> > levels, int wordCount) {
  vector<int> bases;
  int possibleWords = 1;
  for (int i = 0; i < levels.size() && possibleWords < wordCount; i++) {
    bases.push_back(levels[i].size());
    possibleWords *= levels[i].size();
  }

  return bases;
}

GridSettings adjustGridSettings(
  GridSettings gridSettings,
  int width, int height,
  int bubbleCount
) {
  double gridArea = inflatedArea(width, height, gridSettings.pixelsPastEdge);
  double bubbleCover =
    gridSettings.cellWidth * gridSettings.cellHeight * sqrt(0.75) * \
    bubbleCount;
  double scale = max(sqrt(gridArea / bubbleCover), 1);

  GridSettings result;
  result.pixelsPastEdge = gridSettings.pixelsPastEdge;
  result.cellWidth = gridSettings.cellWidth * scale;
  result.cellHeight = gridSettings.cellHeight * scale;
  return result;
}

PointF getNormal(PointF v) {
  return PointF(v.Y, -v.X);
}

REAL dot(PointF v1, PointF v2) {
  return v1.X * v2.X + v1.Y * v2.Y;
}

PointF getCorner(RectF rect, int corner) {
  REAL x = rect.X;
  REAL y = rect.Y;
  if (corner == 1 || corner == 2) {
    x = rect.GetRight();
  }
  if (corner == 2 || corner == 3) {
    y = rect.GetBottom();
  }
  return PointF(x, y);
}

int getMinMultiplierInBounds(RectF bounds, PointF scaled, PointF other) {
  PointF otherN = getNormal(other);
  REAL under = dot(scaled, otherN);
  REAL a = dot(getCorner(bounds, 0), otherN) / under;
  REAL b = dot(getCorner(bounds, 1), otherN) / under;
  REAL c = dot(getCorner(bounds, 2), otherN) / under;
  REAL d = dot(getCorner(bounds, 3), otherN) / under;
  return (int)ceil(min(a, min(b, min(c, d))));
}

int getMaxMultiplierInBounds(RectF bounds, PointF scaled, PointF other) {
  PointF otherN = getNormal(other);
  REAL under = dot(scaled, otherN);
  REAL a = dot(getCorner(bounds, 0), otherN) / under;
  REAL b = dot(getCorner(bounds, 1), otherN) / under;
  REAL c = dot(getCorner(bounds, 2), otherN) / under;
  REAL d = dot(getCorner(bounds, 3), otherN) / under;
  return (int)floor(max(a, max(b, max(c, d))));
}

float getBoundsDistance(RectF bounds, PointF v) {
  return max(
    max(bounds.GetLeft() - v.X, v.X - bounds.GetRight()),
    max(bounds.GetTop() - v.Y, v.Y - bounds.GetBottom())
  );
}

struct BoundsDistanceComparator {
  RectF bounds;
  bool operator() (PointF v1, PointF v2) {
    return getBoundsDistance(bounds, v1) < getBoundsDistance(bounds, v2);
  }
} myobject;

#define degrees(x) (x * (3.1415926535897932384626433832795 / 180))
vector<PointF> getHoneycomb(RectF bounds, int points) {
  PointF v1(cos(degrees(15)), sin(degrees(15)));
  PointF v2(cos(degrees(75)), sin(degrees(75)));

  int v1Start = getMinMultiplierInBounds(bounds, v1, v2);
  int v1Stop = getMaxMultiplierInBounds(bounds, v1, v2) + 1;

  int v2Start = getMinMultiplierInBounds(bounds, v2, v1);
  int v2Stop = getMaxMultiplierInBounds(bounds, v2, v1) + 1;

  while ((v1Stop - v1Start) * (v2Stop - v2Start) < points) {
    v1Start--; v1Stop++; v2Start--; v2Stop++;
  }

  vector<PointF> honeycomb;
  honeycomb.reserve((v1Stop - v1Start) * (v2Stop - v2Start));
  for (int i = v1Start; i < v1Stop; i++) {
    for (int j = v2Start; j < v2Stop; j++) {
      honeycomb.push_back(PointF(v1.X * i + v2.X * j, v1.Y * i + v2.Y * j));
    }
  }

  sort(honeycomb.begin(), honeycomb.end(), BoundsDistanceComparator{bounds});
  honeycomb.resize(points);

  return honeycomb;
}

void scaleHoneycomb(vector<PointF> &honeycomb, REAL xScale, REAL yScale) {
  for (int i = 0; i < honeycomb.size(); i++) {
    honeycomb[i].X *= xScale;
    honeycomb[i].Y *= yScale;
  }
}

void translateHoneycomb(
  vector<PointF> &honeycomb, REAL xOffset, REAL yOffset
) {
  for (int i = 0; i < honeycomb.size(); i++) {
    honeycomb[i].X += xOffset;
    honeycomb[i].Y += yOffset;
  }
}

Point clampToRect(Rect rect, Point point) {
  return Point(
    max(rect.X, min(rect.X + rect.Width - 1, point.X)),
    max(rect.Y, min(rect.Y + rect.Height - 1, point.Y))
  );
}

vector<Point> getJumpPoints(
  GridSettings gridSettings,
  Rect bounds,
  int bubbleCount,
  PointF origin
) {
  RectF newBounds(
    (bounds.X - origin.X - gridSettings.pixelsPastEdge) /
      gridSettings.cellWidth,
    (bounds.Y - origin.Y - gridSettings.pixelsPastEdge) /
      gridSettings.cellHeight,
    (bounds.Width + 2 * gridSettings.pixelsPastEdge) /
      gridSettings.cellWidth,
    (bounds.Height + 2 * gridSettings.pixelsPastEdge) /
      gridSettings.cellHeight
  );

  vector<PointF> honeycomb = getHoneycomb(newBounds, bubbleCount);
  scaleHoneycomb(honeycomb, gridSettings.cellWidth, gridSettings.cellHeight);
  translateHoneycomb(honeycomb, origin.X, origin.Y);

  vector<Point> jumpPoints;
  jumpPoints.reserve(honeycomb.size());
  for (int i = 0; i < honeycomb.size(); i++) {
    jumpPoints.push_back(
      clampToRect(
        bounds,
        Point((int)floor(honeycomb[i].X), (int)floor(honeycomb[i].Y))
      )
    );
  }

  return jumpPoints;
}

typedef struct {
  LPTSTR fontFamily;
  double fontPointSize;
  Color textColor;
  Color chosenTextColor;
  Color fillColor;
  Color borderColor;
  double borderRadius;
  int borderWidth;
  int earHeight;
  int paddingTop;
  int paddingRight;
  int paddingBottom;
  int paddingLeft;
} Style;

typedef struct {
  Font *font;
  SolidBrush *textBrush;
  SolidBrush *chosenTextBrush;
  SolidBrush *fillBrush;
  Pen *borderPen;
} ArtSupplies;

ArtSupplies getArtSupplies(Style style) {
  ArtSupplies artSupplies;
  artSupplies.font = new Font(style.fontFamily, style.fontPointSize);
  artSupplies.textBrush = new SolidBrush(style.textColor);
  artSupplies.chosenTextBrush = new SolidBrush(style.chosenTextColor);
  artSupplies.fillBrush = new SolidBrush(style.fillColor);
  artSupplies.borderPen = new Pen(style.borderColor, style.borderWidth);
  return artSupplies;
}

void destroyArtSupplies(ArtSupplies artSupplies) {
  delete artSupplies.font;
  delete artSupplies.textBrush;
  delete artSupplies.chosenTextBrush;
  delete artSupplies.fillBrush;
  delete artSupplies.borderPen;
}

Rect getBorderBounds(Style style, Point point, Size textSize, int earCorner) {
  int width = textSize.Width + style.paddingLeft + style.paddingRight +
    2 * style.borderWidth;
  int height = textSize.Height + style.paddingTop + style.paddingBottom +
    2 * style.borderWidth;

  int top = point.Y + style.earHeight;
  if (earCorner == 2 || earCorner == 3) {
    top = point.Y - style.earHeight - height + 1;
  }

  int left = point.X;
  if (earCorner == 1 || earCorner == 2) {
    left = point.X - width + 1;
  }

  return Rect(left, top, width, height);
}

void addTopLeftCorner(
  GraphicsPath &border,
  Style style,
  Rect borderBounds,
  bool ear
) {
  REAL extraWidth = -0.5;
  REAL fudge = -0.1;
  if (style.borderWidth > 0) {
    extraWidth = 0.5 * (style.borderWidth - 1);
    fudge = 0.2;
  }
  REAL top = borderBounds.Y + extraWidth;
  REAL right = borderBounds.X + borderBounds.Width - 1 - extraWidth;
  REAL bottom = borderBounds.Y + borderBounds.Height - 1 - extraWidth;
  REAL left = borderBounds.X + extraWidth;

  REAL earHeight = style.earHeight - sqrt(2) * extraWidth + fudge;

  REAL diameter = (REAL)(2 * style.borderRadius);
  if (ear) {
    border.AddLine(left, top - earHeight, left + earHeight, top);
  } else if (diameter == 0) {
    border.AddLine(left, top, left, top);
  } else {
    border.AddArc(left, top, diameter, diameter, 180, 90);
  }
}

void addTopRightCorner(
  GraphicsPath &border,
  Style style,
  Rect borderBounds,
  bool ear
) {
  REAL extraWidth = -0.5;
  REAL fudge = -0.1;
  if (style.borderWidth > 0) {
    extraWidth = 0.5 * (style.borderWidth - 1);
    fudge = 0.2;
  }
  REAL top = borderBounds.Y + extraWidth;
  REAL right = borderBounds.X + borderBounds.Width - 1 - extraWidth;
  REAL bottom = borderBounds.Y + borderBounds.Height - 1 - extraWidth;
  REAL left = borderBounds.X + extraWidth;

  REAL earHeight = style.earHeight - sqrt(2) * extraWidth + fudge;

  REAL diameter = (REAL)(2 * style.borderRadius);
  if (ear) {
    border.AddLine(right - earHeight, top, right, top - earHeight);
  } else if (diameter == 0) {
    border.AddLine(right, top, right, top);
  } else {
    border.AddArc(right - diameter, top, diameter, diameter, 270, 90);
  }
}

void addBottomRightCorner(
  GraphicsPath &border,
  Style style,
  Rect borderBounds,
  bool ear
) {
  REAL extraWidth = -0.5;
  REAL fudge = -0.1;
  if (style.borderWidth > 0) {
    extraWidth = 0.5 * (style.borderWidth - 1);
    fudge = 0.2;
  }
  REAL top = borderBounds.Y + extraWidth;
  REAL right = borderBounds.X + borderBounds.Width - 1 - extraWidth;
  REAL bottom = borderBounds.Y + borderBounds.Height - 1 - extraWidth;
  REAL left = borderBounds.X + extraWidth;

  REAL earHeight = style.earHeight - sqrt(2) * extraWidth + fudge;

  REAL diameter = (REAL)(2 * style.borderRadius);
  if (ear) {
    border.AddLine(right, bottom + earHeight, right - earHeight, bottom);
  } else if (diameter == 0) {
    border.AddLine(right, bottom, right, bottom);
  } else {
    border.AddArc(
      right - diameter, bottom - diameter, diameter, diameter, 0, 90
    );
  }
}

void addBottomLeftCorner(
  GraphicsPath &border,
  Style style,
  Rect borderBounds,
  bool ear
) {
  REAL extraWidth = -0.5;
  REAL fudge = -0.1;
  if (style.borderWidth > 0) {
    extraWidth = 0.5 * (style.borderWidth - 1);
    fudge = 0.2;
  }
  REAL top = borderBounds.Y + extraWidth;
  REAL right = borderBounds.X + borderBounds.Width - 1 - extraWidth;
  REAL bottom = borderBounds.Y + borderBounds.Height - 1 - extraWidth;
  REAL left = borderBounds.X + extraWidth;

  REAL earHeight = style.earHeight - sqrt(2) * extraWidth + fudge;

  REAL diameter = (REAL)(2 * style.borderRadius);
  if (ear) {
    border.AddLine(left + earHeight, bottom, left, bottom + earHeight);
  } else if (diameter == 0) {
    border.AddLine(left, bottom, left, bottom);
  } else {
    border.AddArc(left, bottom - diameter, diameter, diameter, 90, 90);
  }
}

void drawBubble(
  Graphics &graphics,
  Style style,
  ArtSupplies artSupplies,
  Point point,
  LPTSTR text,
  int chosenChars
) {
  Size textSize = getTextSize(graphics, artSupplies.font, text);

  Rect screenBounds;
  graphics.GetClipBounds(&screenBounds);

  int corners[] = {0, 1, 3, 2};
  int earCorner = corners[0];
  Rect borderBounds = getBorderBounds(style, point, textSize, earCorner);

  if (!screenBounds.Contains(borderBounds)) {
    for (int i = 1; i < 4; i++) {
      Rect newBorderBounds = getBorderBounds(
        style, point, textSize, corners[i]
      );

      if (screenBounds.Contains(newBorderBounds)) {
        earCorner = corners[i];
        borderBounds = newBorderBounds;
        break;
      }
    }
  }

  GraphicsPath border;
  addTopLeftCorner(border, style, borderBounds, earCorner == 0);
  addTopRightCorner(border, style, borderBounds, earCorner == 1);
  addBottomRightCorner(border, style, borderBounds, earCorner == 2);
  addBottomLeftCorner(border, style, borderBounds, earCorner == 3);
  border.CloseFigure();
  border.CloseFigure();
  graphics.FillPath(artSupplies.fillBrush, &border);
  if (style.borderWidth > 0) {
    graphics.DrawPath(artSupplies.borderPen, &border);
  }

  RectF textBounds(
    borderBounds.X + style.borderWidth + style.paddingLeft,
    borderBounds.Y + style.borderWidth + style.paddingTop,
    textSize.Width,
    textSize.Height
  );

  int chosenWidth = getChosenWidth(
    graphics, artSupplies.font, text, chosenChars
  );

  Rect chosenHalf(screenBounds);
  chosenHalf.Width = textBounds.X + chosenWidth - chosenHalf.X;

  Rect unchosenHalf(screenBounds);
  unchosenHalf.Width -= chosenHalf.Width;
  unchosenHalf.X = chosenHalf.GetRight();

  GraphicsContainer maskingTape = graphics.BeginContainer();

  graphics.SetClip(unchosenHalf);
  graphics.DrawString(
    text, -1,
    artSupplies.font,
    textBounds,
    getCenteredFormat(),
    artSupplies.textBrush
  );

  graphics.SetClip(chosenHalf);
  graphics.DrawString(
    text, -1,
    artSupplies.font,
    textBounds,
    getCenteredFormat(),
    artSupplies.chosenTextBrush
  );

  graphics.EndContainer(maskingTape);
}

typedef struct {
  vector<vector<Symbol> > levels;
  int primaryClick;
  int secondaryClick;
  int middleClick;
  int primaryHold;
  int secondaryHold;
  int middleHold;
  int exit;
  int clear;
  int left;
  int up;
  int right;
  int down;
  int hStride;
  int vStride;
} Keymap;

typedef struct {
  HMONITOR monitor;
  Keymap keymap;
  GridSettings gridSettings;
  Style style;
  vector<int> word;
} Model;

Model getModel(HMONITOR monitor) {
  Model model;
  model.monitor = monitor;

  model.keymap.levels =
    { { {_T("a"), 0x41},
        {_T("b"), 0x42},
        {_T("c"), 0x43},
        {_T("d"), 0x44},
        {_T("e"), 0x45},
        {_T("f"), 0x46},
        {_T("g"), 0x47},
        {_T("h"), 0x48},
        {_T("i"), 0x49},
        {_T("j"), 0x4a},
        {_T("k"), 0x4b},
        {_T("l"), 0x4c},
        {_T("m"), 0x4d},
        {_T("n"), 0x4e},
        {_T("o"), 0x4f},
        {_T("p"), 0x50},
        {_T("q"), 0x51},
        {_T("r"), 0x52},
        {_T("s"), 0x53},
        {_T("t"), 0x54},
        {_T("u"), 0x55},
        {_T("v"), 0x56},
        {_T("w"), 0x57},
        {_T("x"), 0x58},
        {_T("y"), 0x59},
        {_T("z"), 0x5a}
      },
      { {_T("a"), 0x41},
        {_T("b"), 0x42},
        {_T("c"), 0x43},
        {_T("d"), 0x44},
        {_T("e"), 0x45},
        {_T("f"), 0x46},
        {_T("g"), 0x47},
        {_T("h"), 0x48},
        {_T("i"), 0x49},
        {_T("j"), 0x4a},
        {_T("k"), 0x4b},
        {_T("l"), 0x4c},
        {_T("m"), 0x4d},
        {_T("n"), 0x4e},
        {_T("o"), 0x4f},
        {_T("p"), 0x50},
        {_T("q"), 0x51},
        {_T("r"), 0x52},
        {_T("s"), 0x53},
        {_T("t"), 0x54},
        {_T("u"), 0x55},
        {_T("v"), 0x56},
        {_T("w"), 0x57},
        {_T("x"), 0x58},
        {_T("y"), 0x59},
        {_T("z"), 0x5a}
      }
    };
  model.keymap.primaryClick = 0x0d; // VK_RETURN
  model.keymap.secondaryClick = 0x5d; // VK_APPS
  model.keymap.middleClick = 0x09; // VK_TAB
  model.keymap.primaryHold = 0x2e; // VK_DELETE
  model.keymap.secondaryHold = 0;
  model.keymap.middleHold = 0;
  model.keymap.exit = 0x1b; // VK_ESCAPE
  model.keymap.clear = 0x08; // VK_BACK
  model.keymap.left = 0x25; // VK_LEFT
  model.keymap.up = 0x26; // VK_UP
  model.keymap.right = 0x27; // VK_RIGHT
  model.keymap.down = 0x28; // VK_DOWN
  model.keymap.hStride = 9;
  model.keymap.vStride = 9;

  model.gridSettings.cellWidth = 97;
  model.gridSettings.cellHeight = 39;
  model.gridSettings.pixelsPastEdge = 12;

  LOGFONT fontInfo = getSystemTooltipFont();
  model.style.fontFamily = new TCHAR[_tcslen(fontInfo.lfFaceName) + 1];
  _tcscpy(model.style.fontFamily, fontInfo.lfFaceName);
  model.style.fontPointSize = logicalHeightToPointSize(
    fontInfo.lfHeight,
    monitor
  );

  model.style.textColor = getSystemColor(COLOR_INFOTEXT);
  model.style.chosenTextColor = getSystemColor(COLOR_GRAYTEXT);
  model.style.fillColor = getSystemColor(COLOR_INFOBK);
  model.style.borderColor = Color(118, 118, 118);

  model.style.borderRadius = 2;
  model.style.borderWidth = 1;
  model.style.earHeight = 4;
  model.style.paddingTop = -2;
  model.style.paddingRight = -1;
  model.style.paddingBottom = 0;
  model.style.paddingLeft = -1;

  return model;
}

void destroyModel(Model model) {
  delete[] model.style.fontFamily;
}

void drawModel(Model model, View& view) {
  ArtSupplies artSupplies = getArtSupplies(model.style);
  Rect bitmapBounds(
    0, 0, getBitmapWidth(view.bitmap), getBitmapHeight(view.bitmap)
  );

  Graphics graphics(view.deviceContext);
  graphics.SetClip(bitmapBounds);
  graphics.SetSmoothingMode(SmoothingModeAntiAlias);
  graphics.Clear(Color(0, 0, 0, 0));

  int bubbleCount = getBubbleCount(
    model.keymap.levels,
    model.gridSettings,
    bitmapBounds.Width, bitmapBounds.Height
  );

  GridSettings adjusted = adjustGridSettings(
    model.gridSettings,
    bitmapBounds.Width, bitmapBounds.Height,
    bubbleCount
  );

  vector<Point> jumpPoints = getJumpPoints(
    adjusted,
    bitmapBounds,
    bubbleCount,
    PointF(bitmapBounds.Width * 0.5, bitmapBounds.Height * 0.5)
  );

  vector<int> bases = getBases(model.keymap.levels, bubbleCount);
  int wordIndexStart = getWordIndexStart(bases, bubbleCount, model.word);
  int wordIndexStop = getWordIndexStop(bases, bubbleCount, model.word);
  for (int i = 0; i < jumpPoints.size(); i++) {
    if (i < wordIndexStart || i >= wordIndexStop) {
      continue;
    }
    wstring label;
    vector<int> sequence = getWord(bases, bubbleCount, i);
    for (int j = 0; j < sequence.size(); j++) {
      label += model.keymap.levels[j][sequence[j]].spelling;
    }
    vector<WCHAR> labelVector(label.begin(), label.end());
    labelVector.push_back(0);
    drawBubble(
      graphics,
      model.style,
      artSupplies,
      jumpPoints[i],
      &labelVector[0],
      model.word.size()
    );
  }

  destroyArtSupplies(artSupplies);
}

void showView(View view, HWND window) {
  POINT origin = {0, 0};
  SIZE bitmapSize = {
    getBitmapWidth(view.bitmap), getBitmapHeight(view.bitmap)
  };

  BLENDFUNCTION blendFunction;
  blendFunction.BlendOp = AC_SRC_OVER;
  blendFunction.BlendFlags = 0;
  blendFunction.SourceConstantAlpha = 255;
  blendFunction.AlphaFormat = AC_SRC_ALPHA;

  // Draw the layered (meaning partially transparent) window.
  // If you mess up any of these parameters, you'll probably end up with an
  // invisible window and no way figure out what went wrong. In that case, you
  // should start from this working example and work backwards:
  // http://www.nuonsoft.com/blog/2009/05/27/how-to-use-updatelayeredwindow/

  UpdateLayeredWindow(
    window,
    NULL, // don't care about displays that use a color palette
    &view.start,
    &bitmapSize,
    view.deviceContext, // copies the active bitmap from this device context
    &origin, // copy bits starting from this location on the bitmap
    0, // no chromakey color
    &blendFunction, // boilerplate
    ULW_ALPHA // per-pixel alpha instead of chromakey
  );
}

Model model;
View view;

void moveCursorBy(int xOffset, int yOffset) {
  INPUT inputs[1];
  inputs[0].type = INPUT_MOUSE;
  inputs[0].mi.dx = xOffset;
  inputs[0].mi.dy = yOffset;
  inputs[0].mi.mouseData = 0;
  inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE;
  inputs[0].mi.time = 0;
  inputs[0].mi.dwExtraInfo = 0;
  SendInput(1, inputs, sizeof(INPUT));
}

INPUT getClick(DWORD dwFlags) {
  INPUT click;
  click.type = INPUT_MOUSE;
  click.mi.dx = 0;
  click.mi.dy = 0;
  click.mi.mouseData = 0;
  click.mi.dwFlags = dwFlags;
  click.mi.time = 0;
  click.mi.dwExtraInfo = 0;
  return click;
}

void click(int button, bool goalState) {
  int keycodes[3] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON};
  int actions[2][3] = {
    {MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_MIDDLEUP},
    {MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_MIDDLEDOWN}
  };
  if ((GetKeyState(keycodes[button]) < 0) == goalState) {
    INPUT inputs[2] = {
      getClick(actions[!goalState][button]),
      getClick(actions[goalState][button])
    };
    SendInput(2, inputs, sizeof(INPUT));
  } else {
    INPUT inputs[1] = {getClick(actions[goalState][button])};
    SendInput(1, inputs, sizeof(INPUT));
  }
}

LRESULT CALLBACK WndProc(
  HWND window,
  UINT message,
  WPARAM wParam,
  LPARAM lParam
) {
  LRESULT result = 0;

  switch (message) {
  case WM_KEYDOWN:
    if (wParam == model.keymap.exit) {
      PostMessage(window, WM_CLOSE, 0, 0);
    } else if (wParam == model.keymap.clear) {
      if (model.word.size() > 0) {
        model.word.clear();
        drawModel(model, view);
        showView(view, window);
      }
    } else if (wParam == model.keymap.left) {
      moveCursorBy(-model.keymap.hStride, 0);
    } else if (wParam == model.keymap.up) {
      moveCursorBy(0, -model.keymap.vStride);
    } else if (wParam == model.keymap.right) {
      moveCursorBy(model.keymap.hStride, 0);
    } else if (wParam == model.keymap.down) {
      moveCursorBy(0, model.keymap.vStride);
    } else if (wParam == model.keymap.primaryClick) {
      click(GetSystemMetrics(SM_SWAPBUTTON) != 0, 0);
      PostMessage(window, WM_CLOSE, 0, 0);
    } else if (wParam == model.keymap.secondaryClick) {
      click(GetSystemMetrics(SM_SWAPBUTTON) == 0, 0);
      PostMessage(window, WM_CLOSE, 0, 0);
    } else if (wParam == model.keymap.middleClick) {
      click(2, 0);
      PostMessage(window, WM_CLOSE, 0, 0);
    } else if (wParam == model.keymap.primaryHold) {
      click(GetSystemMetrics(SM_SWAPBUTTON) != 0, 1);
      if (!SetForegroundWindow(window)) {
        PostMessage(window, WM_CLOSE, 0, 0);
      }
    } else if (wParam == model.keymap.secondaryHold) {
      click(GetSystemMetrics(SM_SWAPBUTTON) == 0, 1);
      if (!SetForegroundWindow(window)) {
        PostMessage(window, WM_CLOSE, 0, 0);
      }
    } else if (wParam == model.keymap.middleHold) {
      click(2, 1);
      if (!SetForegroundWindow(window)) {
        PostMessage(window, WM_CLOSE, 0, 0);
      }
    } else {
      int wordCount = getBubbleCount(
        model.keymap.levels,
        model.gridSettings,
        getBitmapWidth(view.bitmap), getBitmapHeight(view.bitmap)
      );
      vector<int> bases = getBases(model.keymap.levels, wordCount);
      int wordChoices = getWordChoices(bases, wordCount, model.word);
      if (model.word.size() < model.keymap.levels.size()) {
        wordChoices = min(
          wordChoices,
          model.keymap.levels[model.word.size()].size()
        );
      }
      if (wordChoices > 1) {
        for (int i = 0; i < wordChoices; i++) {
          if (wParam == model.keymap.levels[model.word.size()][i].keycode) {
            model.word.push_back(i);
            drawModel(model, view);
            showView(view, window);
            break;
          }
        }
      }
    }
    break;
  case WM_DISPLAYCHANGE:
    if (!updateView(model.monitor, view)) {
      PostMessage(window, WM_CLOSE, 0, 0);
    }

    drawModel(model, view);
    showView(view, window);
    break;
  case WM_DESTROY:
    PostQuitMessage(0);
    break;

  default:
    result = DefWindowProc(window, message, wParam, lParam);
    break;
  }

  return result;
}

int CALLBACK WinMain(
  HINSTANCE appInstance,
  HINSTANCE hPrevInstance,
  LPSTR lpCmdLine,
  int showWindowFlags
) {
  // Use GDI+ because vanilla GDI has no concept of transparency and can't
  // even draw an opaque rectangle on a transparent bitmap
  GdiplusStartupInput gdiplusStartupInput;
  ULONG_PTR           gdiplusToken;
  GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

  LPTSTR windowClassName = _T("mousejump");

  WNDCLASSEX windowClass;
  windowClass.cbSize = sizeof(WNDCLASSEX);
  windowClass.style = 0; // don't bother handling resize
  windowClass.lpfnWndProc = WndProc;
  windowClass.cbClsExtra = 0;
  windowClass.cbWndExtra = 0;
  windowClass.hInstance = appInstance;
  windowClass.hIcon = LoadIcon(appInstance, MAKEINTRESOURCE(IDI_APPLICATION));
  windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
  windowClass.hbrBackground = (HBRUSH)COLOR_WINDOW; // doesn't matter
  windowClass.lpszMenuName = NULL;
  windowClass.lpszClassName = windowClassName;
  windowClass.hIconSm = LoadIcon(appInstance, MAKEINTRESOURCE(IDI_APPLICATION));

  RegisterClassEx(&windowClass);

  POINT cursorPos;
  GetCursorPos(&cursorPos);
  HMONITOR monitor = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTONEAREST);

  model = getModel(monitor);
  view = getView();
  updateView(model.monitor, view);
  drawModel(model, view);

  HWND window = CreateWindowEx(
    WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
    windowClassName,
    _T("MouseJump"),
    WS_POPUP,
    0, 0, // don't care because showView sets position
    0, 0, // and size too
    NULL,
    NULL,
    appInstance,
    NULL
  );

  showView(view, window);

  ShowWindow(window, showWindowFlags);

  MSG message;
  while (GetMessage(&message, NULL, 0, 0)) {
    TranslateMessage(&message);
    DispatchMessage(&message);
  }

  destroyView(view);
  destroyModel(model);

  GdiplusShutdown(gdiplusToken);

  return (int)message.wParam;
}

/*
To run:
1. Download and install Build Tools for Visual Studio 2017 RC
https://www.visualstudio.com/downloads/#build-tools-for-visual-studio-2017-rc
2. Open "Developer Command Prompt for VS 2017 RC" under
   "Visual Studio 2017 RC" in the start menu
3.
cd /Users/Evan/Documents/GitHub/mousejump
cl /EHsc /W3 mousejump.cpp && mousejump.exe

To debug, insert this snippet after the thing that probably went wrong:

if (GetLastError()) {
  TCHAR buffer[20];
  _itow(GetLastError(), buffer, 10);
  MessageBox(NULL, buffer, _T("Error Code"), 0);
}

Then look up the error code here:
https://msdn.microsoft.com/en-us/library/windows/desktop/ms681381.aspx

For some reason, calling the Font constructor always causes Error 122,
ERROR_INSUFFICIENT_BUFFER, so if you see that error, you can probably ignore
it.
*/

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <tchar.h>
#include <gdiplus.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <string>
#include <agents.h>
#include <chrono>
#include <thread>
#include <random>

#pragma comment(linker, "/subsystem:windows")
#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "gdiplus")

using namespace Gdiplus;
using namespace std;

#undef min
#undef max
inline int imin(int a, int b) { return b < a ? b : a; }
inline int imax(int a, int b) { return b > a ? b : a; }
inline size_t umin(size_t a, size_t b) { return b < a ? b : a; }
inline size_t umax(size_t a, size_t b) { return b > a ? b : a; }

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
  view.start.x = 0;
  view.start.y = 0;
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

  view.start.x = 0;
  view.start.y = 0;
}

bool updateView(View& view, HMONITOR monitor) {
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

  view.start.x = monitorInfo.rcMonitor.left;
  view.start.y = monitorInfo.rcMonitor.top;
  view.originalBitmap = SelectObject(view.deviceContext, view.bitmap);

  return true;
}

typedef struct {
  double cellWidth;
  double cellHeight;
  double pixelsPastEdge;
} GridSettings;

typedef struct {
  wstring fontFamily;
  double fontPointSize;
  Color textColor;
  Color chosenTextColor;
  Color fillColor;
  Color borderColor;
  double borderRadius;
  double borderWidth;
  double earHeight;
  double paddingTop;
  double paddingRight;
  double paddingBottom;
  double paddingLeft;
  Color dragColor;
  double dragWidth;
  double dragRadius;
} Style;

typedef struct {
  wstring spelling;
  UINT keycode;
} Symbol;

typedef struct {
  vector<vector<Symbol> > levels;
  UINT primaryClick;
  UINT secondaryClick;
  UINT middleClick;
  UINT drag;
  UINT exit;
  UINT clear;
  UINT left;
  UINT up;
  UINT right;
  UINT down;
  int hStride;
  int vStride;
} Keymap;

typedef struct {
  bool dragging;
  POINT dragStart;
  POINT dragStop;
} DragInfo;

typedef struct {
  Keymap keymap;
  GridSettings gridSettings;
  Style style;
  int width;
  int height;
  size_t wordLength;
  size_t wordStart;
  bool showBubbles;
  size_t originIndex;
  DragInfo dragInfo;
} Model;

double inflatedArea(double width, double height, double border) {
  return fmax(0, (width + 2 * border) * (height + 2 * border));
}

size_t getLeafWords(Model model) {
  size_t symbolLimit = 1;
  for (size_t i = 0; i < model.keymap.levels.size(); i++) {
    symbolLimit *= model.keymap.levels[i].size();
  }

  GridSettings g = model.gridSettings;
  double cellArea = fmax(1, fabs(g.cellWidth * g.cellHeight));
  double gridArea = inflatedArea(model.width, model.height, g.pixelsPastEdge);
  size_t areaLimit = (size_t)round(gridArea / cellArea);

  return umax(umin(symbolLimit, areaLimit), 1);
}

typedef struct {
  size_t leafWords;
  size_t shorterWordLength;
  size_t longWordLength;
  size_t possibleShorterWords;
  size_t longWordsPerShorterWord;
  size_t possibleLongWords;
  size_t shorterWords;
  size_t longWords;
} WordIndexInfo;

WordIndexInfo getWordIndexInfo(Model model) {
  WordIndexInfo info;

  info.leafWords = getLeafWords(model);

  info.shorterWordLength = 0;
  info.possibleShorterWords = 1;
  info.longWordsPerShorterWord = 1;
  info.possibleLongWords = 1;
  for (
    info.longWordLength = 0;
    info.longWordLength < model.keymap.levels.size() &&
    info.possibleLongWords <= info.leafWords;
    info.longWordLength++
  ) {
    info.shorterWordLength = info.longWordLength;
    info.possibleShorterWords = info.possibleLongWords;
    info.longWordsPerShorterWord =
      model.keymap.levels[info.longWordLength].size();
    info.possibleLongWords *= info.longWordsPerShorterWord;
  }

  info.shorterWords =
    (info.possibleLongWords - info.leafWords) /
    (info.longWordsPerShorterWord - 1);

  info.longWords = info.leafWords - info.shorterWords;

  return info;
}

int getWordsOnNextBranches(Model model, size_t branches) {
  WordIndexInfo info = getWordIndexInfo(model);

  size_t remainingLongWords = 0;
  if (model.wordStart < info.longWords) {
    remainingLongWords = info.longWords - model.wordStart;
  }

  size_t possibleLongWordDelta = branches;
  for (size_t i = model.wordLength; i < info.longWordLength; i++) {
    possibleLongWordDelta *= model.keymap.levels[i].size();
  }

  size_t longWordDelta = umin(possibleLongWordDelta, remainingLongWords);

  size_t shorterWordDelta =
    (possibleLongWordDelta - longWordDelta) / info.longWordsPerShorterWord;

  return longWordDelta + shorterWordDelta;
}

vector<size_t> getWord(Model model, size_t index) {
  WordIndexInfo info = getWordIndexInfo(model);
  size_t skippedShorterWords = info.possibleShorterWords - info.shorterWords;

  size_t adjustedIndex = index;
  size_t wordLength = info.longWordLength;
  if (index >= info.longWords) {
    adjustedIndex += skippedShorterWords - info.longWords;
    wordLength = info.shorterWordLength;
  }

  vector<size_t> word(wordLength);
  if (wordLength > 0) {
    for (size_t i = wordLength - 1; i > 0; i--) {
      word[i] = adjustedIndex % model.keymap.levels[i - 1].size();
      adjustedIndex /= model.keymap.levels[i - 1].size();
    }
    word[0] = adjustedIndex;
  }

  return word;
}

GridSettings adjustGridSettings(
  GridSettings gridSettings,
  int width, int height,
  size_t bubbleCount
) {
  double gridArea = inflatedArea(width, height, gridSettings.pixelsPastEdge);
  double bubbleCover =
    gridSettings.cellWidth * gridSettings.cellHeight * bubbleCount;
  double scale = fmax(sqrt(gridArea / bubbleCover), 1);

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
  REAL x = corner == 1 || corner == 2 ? rect.GetRight() : rect.X;
  REAL y = corner == 2 || corner == 3 ? rect.GetBottom() : rect.Y;
  return PointF(x, y);
}

int getMinMultiplierInBounds(RectF bounds, PointF scaled, PointF other) {
  PointF otherN = getNormal(other);
  REAL under = dot(scaled, otherN);
  REAL a = dot(getCorner(bounds, 0), otherN) / under;
  REAL b = dot(getCorner(bounds, 1), otherN) / under;
  REAL c = dot(getCorner(bounds, 2), otherN) / under;
  REAL d = dot(getCorner(bounds, 3), otherN) / under;
  return (int)ceil(fmin(a, fmin(b, fmin(c, d))));
}

int getMaxMultiplierInBounds(RectF bounds, PointF scaled, PointF other) {
  PointF otherN = getNormal(other);
  REAL under = dot(scaled, otherN);
  REAL a = dot(getCorner(bounds, 0), otherN) / under;
  REAL b = dot(getCorner(bounds, 1), otherN) / under;
  REAL c = dot(getCorner(bounds, 2), otherN) / under;
  REAL d = dot(getCorner(bounds, 3), otherN) / under;
  return (int)floor(fmax(a, fmax(b, fmax(c, d))));
}

REAL getBoundsDistance(RectF bounds, PointF v) {
  return fmax(
    fmax(bounds.GetLeft() - v.X, v.X - bounds.GetRight()),
    fmax(bounds.GetTop() - v.Y, v.Y - bounds.GetBottom())
  );
}

class BoundsDistanceComparator {
private:
  RectF bounds;
public:
  BoundsDistanceComparator(const RectF &bounds) {
    this->bounds = bounds;
  }

  bool operator() (PointF v1, PointF v2) {
    return getBoundsDistance(bounds, v1) < getBoundsDistance(bounds, v2);
  }
};

PointF pointFromDoubles(double x, double y) {
  return PointF((REAL)x, (REAL)y);
}

#define degrees(x) (x * (3.1415926535897932384626433832795 / 180))
vector<PointF> getHoneycomb(RectF bounds, size_t points) {
  PointF v1 = pointFromDoubles(cos(degrees(15)), sin(degrees(15)));
  PointF v2 = pointFromDoubles(cos(degrees(75)), sin(degrees(75)));

  int v1Start = getMinMultiplierInBounds(bounds, v1, v2);
  int v1Stop = getMaxMultiplierInBounds(bounds, v1, v2) + 1;

  int v2Start = getMinMultiplierInBounds(bounds, v2, v1);
  int v2Stop = getMaxMultiplierInBounds(bounds, v2, v1) + 1;

  while ((v1Stop - v1Start) * (v2Stop - v2Start) < (int)points) {
    v1Start--; v1Stop++; v2Start--; v2Stop++;
  }

  vector<PointF> honeycomb;
  honeycomb.reserve((v1Stop - v1Start) * (v2Stop - v2Start));
  for (int i = v1Start; i < v1Stop; i++) {
    for (int j = v2Start; j < v2Stop; j++) {
      honeycomb.push_back(PointF(v1.X * i + v2.X * j, v1.Y * i + v2.Y * j));
    }
  }

  sort(honeycomb.begin(), honeycomb.end(), BoundsDistanceComparator(bounds));
  honeycomb.resize(points);

  return honeycomb;
}

void scaleHoneycomb(vector<PointF> &honeycomb, double xScale, double yScale) {
  for (size_t i = 0; i < honeycomb.size(); i++) {
    honeycomb[i].X *= (REAL)xScale;
    honeycomb[i].Y *= (REAL)yScale;
  }
}

void translateHoneycomb(
  vector<PointF> &honeycomb, double xOffset, double yOffset
) {
  for (size_t i = 0; i < honeycomb.size(); i++) {
    honeycomb[i].X += (REAL)xOffset;
    honeycomb[i].Y += (REAL)yOffset;
  }
}

Point clampToSize(int width, int height, Point point) {
  return Point(
    imax(0, imin(width - 1, point.X)),
    imax(0, imin(height - 1, point.Y))
  );
}

RectF rectFromDoubles(double x, double y, double width, double height) {
  return RectF(
    (REAL)(x + fmin(width, 0)), (REAL)(y + fmin(height, 0)),
    (REAL)fabs(width), (REAL)fabs(height)
  );
}

const size_t ORIGIN_COUNT = 3;

vector<Point> getJumpPoints(Model model) {
  size_t bubbleCount = getLeafWords(model);

  GridSettings gridSettings = adjustGridSettings(
    model.gridSettings, model.width, model.height, bubbleCount
  );

  double xScale = gridSettings.cellWidth / sqrt(sqrt(0.75));
  double yScale = gridSettings.cellHeight / sqrt(sqrt(0.75));

  double xOrigin = 0;
  double yOrigin = 0;
  switch (model.originIndex) {
  case 1:
    xOrigin = sqrt(1.0 / 3) * cos(degrees(45));
    yOrigin = sqrt(1.0 / 3) * sin(degrees(45));
    break;
  case 2:
    xOrigin = sqrt(1.0 / 3) * cos(degrees(105));
    yOrigin = sqrt(1.0 / 3) * sin(degrees(105));
    break;
  }
  xOrigin = 0.5 * model.width + xOrigin * xScale;
  yOrigin = 0.5 * model.height + yOrigin * yScale;

  RectF bounds = rectFromDoubles(
    (-xOrigin - gridSettings.pixelsPastEdge) / xScale,
    (-yOrigin - gridSettings.pixelsPastEdge) / yScale,
    (model.width + 2 * gridSettings.pixelsPastEdge) / xScale,
    (model.height + 2 * gridSettings.pixelsPastEdge) / yScale
  );

  vector<PointF> honeycomb = getHoneycomb(bounds, bubbleCount);
  scaleHoneycomb(honeycomb, xScale, yScale);
  translateHoneycomb(honeycomb, xOrigin, yOrigin);
  shuffle(honeycomb.begin(), honeycomb.end(), default_random_engine());

  size_t visibleWords = 0;
  if (model.showBubbles) {
    visibleWords = getWordsOnNextBranches(model, 1);
  }

  vector<Point> jumpPoints;
  jumpPoints.reserve(visibleWords);
  for (size_t i = model.wordStart; i < model.wordStart + visibleWords; i++) {
    jumpPoints.push_back(
      clampToSize(
        model.width, model.height,
        Point((int)floor(honeycomb[i].X), (int)floor(honeycomb[i].Y))
      )
    );
  }

  return jumpPoints;
}

int pixelateSize(double size) {
  if (size > 0) {
    return imax((int)round(size), 1);
  }

  return 0;
}

int pixelate(double offset) {
  return offset < 0 ? -pixelateSize(-offset) : pixelateSize(offset);
}

typedef struct {
  Font *font;
  SolidBrush *textBrush;
  SolidBrush *chosenTextBrush;
  SolidBrush *fillBrush;
  Pen *borderPen;
  Pen *dragPen;
} ArtSupplies;

ArtSupplies getArtSupplies(Style style) {
  ArtSupplies artSupplies;
  artSupplies.font = new Font(
    style.fontFamily.c_str(), (REAL)style.fontPointSize
  );
  artSupplies.textBrush = new SolidBrush(style.textColor);
  artSupplies.chosenTextBrush = new SolidBrush(style.chosenTextColor);
  artSupplies.fillBrush = new SolidBrush(style.fillColor);
  artSupplies.borderPen = new Pen(
    style.borderColor,
    (REAL)pixelateSize(style.borderWidth)
  );
  artSupplies.dragPen = new Pen(
    style.dragColor,
    (REAL)pixelateSize(style.dragWidth)
  );

  return artSupplies;
}

void destroyArtSupplies(ArtSupplies artSupplies) {
  delete artSupplies.font;
  delete artSupplies.textBrush;
  delete artSupplies.chosenTextBrush;
  delete artSupplies.fillBrush;
  delete artSupplies.borderPen;
}

typedef struct {
  REAL h;
  REAL top;
  REAL right;
  REAL bottom;
  REAL left;
  REAL d;
} PathInfo;

PathInfo getPathInfo(Style style, Rect borderBounds) {
  int borderWidth = pixelateSize(style.borderWidth);
  double deflation = 0.5 * borderWidth - 0.5;
  double fudge = borderWidth > 0 ? 0.2 : -0.1;
  int earHeight = pixelateSize(style.earHeight);

  PathInfo pathInfo;
  pathInfo.h = 0;
  if (earHeight > 0) {
    pathInfo.h = (REAL)fmax(earHeight - sqrt(2) * deflation + fudge, 0);
  }

  pathInfo.top = (REAL)(borderBounds.Y + deflation);
  pathInfo.right =
    (REAL)(borderBounds.X + borderBounds.Width - 1 - deflation);
  pathInfo.bottom =
    (REAL)(borderBounds.Y + borderBounds.Height - 1 - deflation);
  pathInfo.left = (REAL)(borderBounds.X + deflation);

  pathInfo.d = (REAL)(2 * style.borderRadius);

  return pathInfo;
}

void addTopLeftCorner(GraphicsPath &border, PathInfo p, bool ear) {
  if (ear) {
    border.AddLine(p.left, p.top - p.h, p.left + p.h, p.top);
  } else if (p.d == 0) {
    border.AddLine(p.left, p.top, p.left, p.top);
  } else {
    border.AddArc(p.left, p.top, p.d, p.d, 180, 90);
  }
}

void addTopRightCorner(GraphicsPath &border, PathInfo p, bool ear) {
  if (ear) {
    border.AddLine(p.right - p.h, p.top, p.right, p.top - p.h);
  } else if (p.d == 0) {
    border.AddLine(p.right, p.top, p.right, p.top);
  } else {
    border.AddArc(p.right - p.d, p.top, p.d, p.d, 270, 90);
  }
}

void addBottomRightCorner(GraphicsPath &border, PathInfo p, bool ear) {
  if (ear) {
    border.AddLine(p.right, p.bottom + p.h, p.right - p.h, p.bottom);
  } else if (p.d == 0) {
    border.AddLine(p.right, p.bottom, p.right, p.bottom);
  } else {
    border.AddArc(p.right - p.d, p.bottom - p.d, p.d, p.d, 0, 90);
  }
}

void addBottomLeftCorner(GraphicsPath &border, PathInfo p, bool ear) {
  if (ear) {
    border.AddLine(p.left + p.h, p.bottom, p.left, p.bottom + p.h);
  } else if (p.d == 0) {
    border.AddLine(p.left, p.bottom, p.left, p.bottom);
  } else {
    border.AddArc(p.left, p.bottom - p.d, p.d, p.d, 90, 90);
  }
}

Size getTextSize(const Graphics &graphics, const Font *font, LPTSTR text) {
  RectF bounds;
  graphics.MeasureString(text, -1, font, PointF(0.0f, 0.0f), &bounds);
  return Size((int)round(bounds.Width), (int)round(bounds.Height));
}

int getChosenWidth(
  const Graphics &graphics, const Font *font, LPTSTR text, int chosenChars
) {
  PointF origin(0, 0);

  RectF bounds;
  graphics.MeasureString(text, -1, font, origin, &bounds);
  REAL fullWidth = bounds.Width;

  graphics.MeasureString(text, chosenChars, font, origin, &bounds);
  REAL chosenWidth = bounds.Width;

  graphics.MeasureString(&text[chosenChars], -1, font, origin, &bounds);
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

Rect getBorderBounds(Style style, Point point, Size textSize, int earCorner) {
  int width = textSize.Width +
    pixelate(style.paddingLeft) + pixelate(style.paddingRight) +
    2 * pixelateSize(style.borderWidth);
  int height = textSize.Height +
    pixelate(style.paddingTop) + pixelate(style.paddingBottom) +
    2 * pixelateSize(style.borderWidth);

  int top = point.Y + pixelateSize(style.earHeight);
  if (earCorner == 2 || earCorner == 3) {
    top = point.Y - pixelateSize(style.earHeight) - height + 1;
  }

  int left = point.X;
  if (earCorner == 1 || earCorner == 2) {
    left = point.X - width + 1;
  }

  return Rect(left, top, width, height);
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
    for (size_t i = 1; i < 4; i++) {
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
  PathInfo pathInfo = getPathInfo(style, borderBounds);
  addTopLeftCorner(border, pathInfo, earCorner == 0);
  addTopRightCorner(border, pathInfo, earCorner == 1);
  addBottomRightCorner(border, pathInfo, earCorner == 2);
  addBottomLeftCorner(border, pathInfo, earCorner == 3);
  border.CloseFigure();

  graphics.FillPath(artSupplies.fillBrush, &border);

  if (style.borderWidth > 0) {
    graphics.DrawPath(artSupplies.borderPen, &border);
  }

  RectF textBounds = rectFromDoubles(
    borderBounds.X + pixelateSize(style.borderWidth) +
      pixelate(style.paddingLeft),
    borderBounds.Y + pixelateSize(style.borderWidth) +
      pixelate(style.paddingTop),
    textSize.Width, textSize.Height
  );

  int chosenWidth = getChosenWidth(
    graphics, artSupplies.font, text, chosenChars
  );

  Rect chosenHalf(screenBounds);
  chosenHalf.Width = (int)textBounds.X + chosenWidth - chosenHalf.X;

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

double logicalHeightToPointSize(int logicalHeight) {
  return abs(logicalHeight) * 72.0 / 96.0;
}

Model getModel(int width, int height) {
  Model model;

  Symbol alphabet[] = {
    {L"a", 0x41},
    {L"b", 0x42},
    {L"c", 0x43},
    {L"d", 0x44},
    {L"e", 0x45},
    {L"f", 0x46},
    {L"g", 0x47},
    {L"h", 0x48},
    {L"i", 0x49},
    {L"j", 0x4a},
    {L"k", 0x4b},
    {L"l", 0x4c},
    {L"m", 0x4d},
    {L"n", 0x4e},
    {L"o", 0x4f},
    {L"p", 0x50},
    {L"q", 0x51},
    {L"r", 0x52},
    {L"s", 0x53},
    {L"t", 0x54},
    {L"u", 0x55},
    {L"v", 0x56},
    {L"w", 0x57},
    {L"x", 0x58},
    {L"y", 0x59},
    {L"z", 0x5a}
  };

  for (int i = 0; i < 2; i++) {
    model.keymap.levels.push_back(
      vector<Symbol>(
        alphabet,
        alphabet + sizeof(alphabet) / sizeof(alphabet[0])
      )
    );
  }

  // Keycode reference:
  // https://msdn.microsoft.com/en-us/library/windows/desktop/dd375731.aspx
  model.keymap.primaryClick = 0x0d; // VK_RETURN
  model.keymap.secondaryClick = 0x5d; // VK_APPS
  model.keymap.middleClick = 0x09; // VK_TAB
  model.keymap.drag = 0x20; // VK_SPACE
  model.keymap.exit = 0x1b; // VK_ESCAPE
  model.keymap.clear = 0x08; // VK_BACK
  model.keymap.left = 0x25; // VK_LEFT
  model.keymap.up = 0x26; // VK_UP
  model.keymap.right = 0x27; // VK_RIGHT
  model.keymap.down = 0x28; // VK_DOWN
  model.keymap.hStride = 9;
  model.keymap.vStride = 9;

  model.gridSettings.cellWidth = -90.268671332903661097298218457374;
  model.gridSettings.cellHeight = 36.293589504981884358707531132346;
  model.gridSettings.pixelsPastEdge = 12;

  LOGFONT fontInfo = getSystemTooltipFont();
  model.style.fontFamily = wstring(fontInfo.lfFaceName);
  model.style.fontPointSize = logicalHeightToPointSize(fontInfo.lfHeight);

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

  model.style.dragColor = Color(255, 0, 0);
  model.style.dragWidth = 1;
  model.style.dragRadius = 10;

  model.width = width;
  model.height = height;

  model.wordLength = 0;
  model.wordStart = 0;

  model.showBubbles = true;

  model.originIndex = 0;

  model.dragInfo.dragging = false;

  return model;
}

void drawModel(Model model, View& view) {
  ArtSupplies artSupplies = getArtSupplies(model.style);

  Graphics graphics(view.deviceContext);
  graphics.SetClip(Rect(0, 0, model.width, model.height));
  graphics.SetSmoothingMode(SmoothingModeAntiAlias);
  graphics.Clear(Color(0, 0, 0, 0));

  if (model.dragInfo.dragging) {
    POINT start = model.dragInfo.dragStart;
    POINT stop = model.dragInfo.dragStop;
    if (start.x == stop.x && start.y == stop.y) {
      int r = pixelateSize(model.style.dragRadius);
      graphics.DrawEllipse(
        artSupplies.dragPen, start.x - r, start.y - r, 2 * r, 2 * r
      );
    } else {
      graphics.DrawLine(
        artSupplies.dragPen, start.x, start.y, stop.x, stop.y
      );
    }
  }

  vector<Point> jumpPoints = getJumpPoints(model);

  for (size_t i = 0; i < jumpPoints.size(); i++) {
    wstring label;
    vector<size_t> sequence = getWord(model, i + model.wordStart);
    for (size_t j = 0; j < sequence.size(); j++) {
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
      model.wordLength
    );
  }

  destroyArtSupplies(artSupplies);
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

const UINT MODEL_CHANGED = WM_USER;
const UINT DRAG_INFO_CHANGED = WM_USER + 1;

const int PATIENT_DONE = 0;
const int PATIENT_RETRY = 1;
const int PATIENT_CANCEL = 2;

typedef struct {
  int status;
  int delay;
} PatientResult;

PatientResult defaultDelay(int status) {
  int delay = 0;
  switch (status) {
  case PATIENT_RETRY: delay = 200; break;
  case PATIENT_CANCEL: delay = 10; break;
  }

  PatientResult result = {status, delay};
  return result;
}

class PatientAction {
public:
  virtual PatientResult operator() () =0;
  virtual ~PatientAction() {};
};

class PatientCursorMover: public PatientAction {
protected:
  HWND window;
  DragInfo &dragInfo;
  virtual POINT getGoalPos(POINT oldPos) const =0;
private:
  POINT oldPos;
  int triesLeft;
  bool triedYet;
  void setCursorPos(int x, int y) {
    if (dragInfo.dragging) {
      dragInfo.dragStop.x = x;
      dragInfo.dragStop.y = y;
      SendMessage(window, DRAG_INFO_CHANGED, 0, 0);
    } else {
      SetCursorPos(x, y);
    }
  }
  void getCursorPos(POINT *result) {
    if (dragInfo.dragging) {
      *result = dragInfo.dragStop;
    } else {
      GetCursorPos(result);
    }
  }
public:
  PatientCursorMover(HWND window, DragInfo &dragInfo) :
    window(window), dragInfo(dragInfo), triesLeft(5), triedYet(false)
  {}
  PatientResult operator() () {
    POINT goalPos;
    if (triedYet) {
      goalPos = getGoalPos(oldPos);
    } else {
      triedYet = true;

      getCursorPos(&oldPos);
      goalPos = getGoalPos(oldPos);
      if (oldPos.x == goalPos.x && oldPos.y == goalPos.y) {
        return defaultDelay(PATIENT_DONE);
      }
    }

    setCursorPos(goalPos.x, goalPos.y);

    POINT newPos;
    getCursorPos(&newPos);
    if (newPos.x != oldPos.x || newPos.y != oldPos.y) {
      return defaultDelay(PATIENT_DONE);
    }

    triesLeft--;
    if (triesLeft <= 0) {
      MessageBox(NULL, _T("Failed to move cursor"), _T("MouseJump"), 0);
      return defaultDelay(PATIENT_CANCEL);
    }

    return defaultDelay(PATIENT_RETRY);
  }
};

class PatientSetCursorPos: public PatientCursorMover {
private:
  POINT goalPos;
protected:
  POINT getGoalPos(POINT oldPos) const {
    return goalPos;
  }
public:
  PatientSetCursorPos(HWND window, DragInfo &dragInfo, int x, int y) :
    PatientCursorMover(window, dragInfo), goalPos{x, y}
  {}
};

class PatientMoveCursorBy: public PatientCursorMover {
private:
  POINT offset;
protected:
  POINT getGoalPos(POINT oldPos) const {
    return {oldPos.x + offset.x, oldPos.y + offset.y};
  }
public:
  PatientMoveCursorBy(
    HWND window, DragInfo &dragInfo, int xOffset, int yOffset
  ) :
    PatientCursorMover(window, dragInfo), offset{xOffset, yOffset}
  {}
};

class PatientMoveToDragStart: public PatientCursorMover {
protected:
  POINT getGoalPos(POINT oldPos) const {
    return dragInfo.dragStart;
  }
public:
  PatientMoveToDragStart(HWND window, DragInfo &dragInfo) :
    PatientCursorMover(window, dragInfo)
  {}
};

class PatientMoveToDragStop: public PatientCursorMover {
protected:
  POINT getGoalPos(POINT oldPos) const {
    return dragInfo.dragStop;
  }
public:
  PatientMoveToDragStop(HWND window, DragInfo &dragInfo) :
    PatientCursorMover(window, dragInfo)
  {}
};

class PatientToggleDragging: public PatientAction {
private:
  HWND window;
  DragInfo &dragInfo;
public:
  PatientToggleDragging(HWND window, DragInfo &dragInfo) :
    window(window), dragInfo(dragInfo)
  {}
  PatientResult operator() () {
    dragInfo.dragging = !dragInfo.dragging;
    if (dragInfo.dragging) {
      GetCursorPos(&dragInfo.dragStart);
      dragInfo.dragStop = dragInfo.dragStart;
    }

    SendMessage(window, DRAG_INFO_CHANGED, 0, 0);

    return defaultDelay(PATIENT_DONE);
  }
};

class PatientWaitBasedOnDragging: public PatientAction {
private:
  DragInfo &dragInfo;
  int draggingDelay;
  int normalDelay;
public:
  PatientWaitBasedOnDragging(
    DragInfo &dragInfo, int draggingDelay, int normalDelay
  ) :
    dragInfo(dragInfo), draggingDelay(draggingDelay), normalDelay(normalDelay)
  {}
  PatientResult operator() () {
    return {PATIENT_DONE, dragInfo.dragging ? draggingDelay : normalDelay};
  }
};

class PatientClick: public PatientAction {
private:
  int button;
  bool press;
  int triesLeft;
  bool triedYet;
  UINT getKeycode() const {
    switch (button) {
    case 2: return VK_RBUTTON;
    case 1: return VK_MBUTTON;
    default: return VK_LBUTTON;
    }
  }
  int getAction() const {
    switch (button) {
    case 2: return press ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    case 1: return press ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    default: return press ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    }
  }
public:
  PatientClick(int button, bool press) {
    this->button = button;
    this->press = press;
    triesLeft = 5;
    triedYet = false;
  }
  PatientResult operator() () {
    if (!triedYet) {
      triedYet = true;

      if ((button == 0 || button == 2) && GetSystemMetrics(SM_SWAPBUTTON)) {
        button = 2 * !button;
      }
    }

    UINT keycode = getKeycode();

    if ((GetAsyncKeyState(keycode) < 0) == press) {
      return defaultDelay(PATIENT_DONE);
    }

    INPUT click;
    click.type = INPUT_MOUSE;
    click.mi.dx = 0;
    click.mi.dy = 0;
    click.mi.mouseData = 0;
    click.mi.dwFlags = getAction();
    click.mi.time = 0;
    click.mi.dwExtraInfo = 0;
    SendInput(1, &click, sizeof(INPUT));

    if ((GetAsyncKeyState(keycode) < 0) == press) {
      return defaultDelay(PATIENT_DONE);
    }

    triesLeft--;
    if (triesLeft <= 0) {
      MessageBox(NULL, _T("Failed to send click"), _T("MouseJump"), 0);
      return defaultDelay(PATIENT_CANCEL);
    }

    return defaultDelay(PATIENT_RETRY);
  }
};

class PatientCloseWindow : public PatientAction {
private:
  HWND window;
public:
  PatientCloseWindow(HWND window) {
    this->window = window;
  }
  PatientResult operator() () {
    SendMessage(window, WM_CLOSE, 0, 0);
    return defaultDelay(PATIENT_DONE);
  }
};

class PatientIgnoreFutureEvents : public PatientAction {
public:
  PatientIgnoreFutureEvents() {}
  PatientResult operator() () {
    return {PATIENT_CANCEL, 60000}; // one minute is basically forever
  }
};

class PatientConsumer : public concurrency::agent {
private:
  concurrency::ISource<PatientAction*>& pendingActions;
public:
  PatientConsumer(concurrency::ISource<PatientAction*>& pendingActions) :
    pendingActions(pendingActions)
  {}
protected:
  void run() {
    chrono::high_resolution_clock::time_point cancelEnd =
      chrono::high_resolution_clock::time_point::min();
    while (PatientAction* action = concurrency::receive(pendingActions)) {
      if (
        chrono::high_resolution_clock::time_point::min() < cancelEnd &&
        chrono::high_resolution_clock::now() < cancelEnd
      ) {
        delete action;
        continue;
      }

      cancelEnd = chrono::high_resolution_clock::time_point::min();

      PatientResult result;
      while ((result = (*action)()).status == PATIENT_RETRY) {
        if (result.delay > 0) {
          this_thread::sleep_for(chrono::milliseconds(result.delay));
        }
      }

      delete action;

      if (result.delay > 0) {
        switch (result.status) {
        case PATIENT_CANCEL:
          cancelEnd =
            chrono::high_resolution_clock::now() +
            chrono::milliseconds(result.delay);
          break;
        case PATIENT_DONE:
          this_thread::sleep_for(chrono::milliseconds(result.delay));
          break;
        }
      }
    }

    done();
  }
};

// Dragon's API has a function called PlayString for simulating keystrokes,
// which seems to block other programs from sending mouse input while it's
// running. To get around this, I use a producer-consumer pattern to queue up
// mouse actions and retry them in a separte thread until they work.
concurrency::unbounded_buffer<PatientAction*> pendingActions;
concurrency::ITarget<PatientAction*>& target = pendingActions;
PatientConsumer consumer(pendingActions);

void sendAction(PatientAction *action) {
  concurrency::send(target, (PatientAction*)action);
}

void click(HWND window, DragInfo &dragInfo, int button) {
  sendAction(new PatientCloseWindow(window));
  sendAction(new PatientToggleDragging(window, dragInfo));
  sendAction(new PatientClick(button, false));
  sendAction(new PatientMoveToDragStart(window, dragInfo));
  sendAction(new PatientClick(button, true));
  sendAction(new PatientMoveToDragStop(window, dragInfo));
  sendAction(new PatientWaitBasedOnDragging(dragInfo, 0, 100));
  sendAction(new PatientClick(button, false));
  sendAction(new PatientIgnoreFutureEvents());
}

HMONITOR monitor;
View view;
Model model;
DragInfo dragInfo;

void moveCursorBy(HWND window, int xOffset, int yOffset) {
  sendAction(new PatientMoveCursorBy(window, dragInfo, xOffset, yOffset));
  if (model.showBubbles) {
    model.showBubbles = false;
    PostMessage(window, MODEL_CHANGED, 0, 0);
  }
}

void showAllBubbles(HWND window) {
  model.wordLength = 0;
  model.wordStart = 0;
  model.showBubbles = true;
  PostMessage(window, MODEL_CHANGED, 0, 0);
}

LRESULT CALLBACK WndProc(
  HWND window,
  UINT message,
  WPARAM wParam,
  LPARAM lParam
) {
  LRESULT result = 0;

  switch (message) {
  case MODEL_CHANGED:
    drawModel(model, view);
    showView(view, window);
    break;
  case DRAG_INFO_CHANGED:
    // This read is thread-safe because the consumer thread only sends
    // DRAG_INFO_CHANGED using SendMessage, which blocks until this thread
    // returns.
    model.dragInfo = dragInfo;
    PostMessage(window, MODEL_CHANGED, 0, 0);
    break;
  case WM_KEYDOWN:
    if (wParam == model.keymap.exit) {
      sendAction(new PatientCloseWindow(window));
      sendAction(new PatientIgnoreFutureEvents());
    } else if (wParam == model.keymap.clear) {
      if (model.wordLength <= 0 && model.showBubbles) {
        model.originIndex = (model.originIndex + 1) % ORIGIN_COUNT;
      }

      showAllBubbles(window);
    } else if (wParam == model.keymap.drag) {
      sendAction(new PatientToggleDragging(window, dragInfo));
      sendAction(new PatientMoveToDragStop(window, dragInfo));
      if (model.wordLength > 0 || !model.showBubbles) {
        showAllBubbles(window);
      }
    } else if (wParam == model.keymap.left) {
      moveCursorBy(window, -model.keymap.hStride, 0);
    } else if (wParam == model.keymap.up) {
      moveCursorBy(window, 0, -model.keymap.vStride);
    } else if (wParam == model.keymap.right) {
      moveCursorBy(window, model.keymap.hStride, 0);
    } else if (wParam == model.keymap.down) {
      moveCursorBy(window, 0, model.keymap.vStride);
    } else if (wParam == model.keymap.primaryClick) {
      click(window, dragInfo, 0);
    } else if (wParam == model.keymap.secondaryClick) {
      click(window, dragInfo, 2);
    } else if (wParam == model.keymap.middleClick) {
      click(window, dragInfo, 1);
    } else if (model.showBubbles) {
      size_t wordChoices = 1;
      if (model.wordLength < model.keymap.levels.size()) {
        wordChoices = umin(
          getWordsOnNextBranches(model, 1),
          model.keymap.levels[model.wordLength].size()
        );
      }
      if (wordChoices > 1) {
        for (size_t i = 0; i < wordChoices; i++) {
          if (wParam == model.keymap.levels[model.wordLength][i].keycode) {
            model.wordLength++;
            model.wordStart += getWordsOnNextBranches(model, i);
            vector<Point> jumpPoints = getJumpPoints(model);
            if (jumpPoints.empty()) {
              MessageBox(
                NULL,
                _T("MouseJump"),
                _T("Internal error: jumpPoints is empty"),
                0
              );
            }
            if (jumpPoints.size() == 1) {
              sendAction(
                new PatientSetCursorPos(
                  window,
                  dragInfo,
                  view.start.x + jumpPoints[0].X,
                  view.start.y + jumpPoints[0].Y
                )
              );

              model.showBubbles = false;
            }

            PostMessage(window, MODEL_CHANGED, 0, 0);
            break;
          }
        }
      }
    }
    break;
  case WM_DISPLAYCHANGE:
    if (!updateView(view, monitor)) {
      PostMessage(window, WM_CLOSE, 0, 0);
    } else {
      int newWidth = getBitmapWidth(view.bitmap);
      int newHeight = getBitmapHeight(view.bitmap);
      if (model.width != newWidth || model.height != newHeight) {
        model.width = newWidth;
        model.height = newHeight;
        model.wordLength = 0;
        model.wordStart = 0;
      }

      PostMessage(window, MODEL_CHANGED, 0, 0);
    }

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
  LPTSTR windowClassName = _T("mousejump");

  WNDCLASSEX windowClass;
  windowClass.cbSize = sizeof(WNDCLASSEX);
  windowClass.style = 0; // don't bother handling resize
  windowClass.lpfnWndProc = WndProc;
  windowClass.cbClsExtra = 0;
  windowClass.cbWndExtra = 0;
  windowClass.hInstance = appInstance;
  windowClass.hIcon = NULL;
  windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
  windowClass.hbrBackground = (HBRUSH)COLOR_WINDOW; // doesn't matter
  windowClass.lpszMenuName = NULL;
  windowClass.lpszClassName = windowClassName;
  windowClass.hIconSm = NULL;

  RegisterClassEx(&windowClass);

  POINT cursorPos;
  GetCursorPos(&cursorPos);
  monitor = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTONEAREST);

  view = getView();
  if (!updateView(view, monitor)) {
    return 1;
  }

  // Use GDI+ because vanilla GDI has no concept of transparency and can't
  // even draw an opaque rectangle on a transparent bitmap
  GdiplusStartupInput gdiplusStartupInput;
  ULONG_PTR           gdiplusToken;
  GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

  model = getModel(getBitmapWidth(view.bitmap), getBitmapHeight(view.bitmap));

  dragInfo.dragging = false;

  consumer.start();

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
  SendMessage(window, MODEL_CHANGED, 0, 0);
  ShowWindow(window, showWindowFlags);

  MSG message;
  while (GetMessage(&message, NULL, 0, 0)) {
    TranslateMessage(&message);
    DispatchMessage(&message);
  }

  destroyView(view);

  GdiplusShutdown(gdiplusToken);

  concurrency::send(target, (PatientAction*)NULL);
  concurrency::agent::wait(&consumer);

  return (int)message.wParam;
}

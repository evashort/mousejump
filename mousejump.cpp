/*
To run, download and install Build Tools for Visual Studio 2017 RC
https://www.visualstudio.com/downloads/#build-tools-for-visual-studio-2017-rc

Open "Developer Command Prompt for VS 2017 RC" under "Visual Studio 2017 RC"
in the start menu

cd /Users/Evan/Documents/GitHub/mousejump
cl /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS mousejump.cpp && mousejump.exe

To debug, insert this snippet after the thing that probably went wrong:

if (GetLastError()) {
  TCHAR buffer[20];
  _itow(GetLastError(), buffer, 10);
  MessageBox(NULL, buffer, _T("Error Code"), NULL);
}

Then look up the error code here:
https://msdn.microsoft.com/en-us/library/windows/desktop/ms681381.aspx
*/

#include <windows.h>
#include <tchar.h>
#include <gdiplus.h>
#include <math.h>

#pragma comment(linker, "/subsystem:windows")
#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "gdiplus")

using namespace Gdiplus;

BOOL CALLBACK MonitorEnumProc(
  HMONITOR monitor,
  HDC deviceContext,
  LPRECT intersectionBounds,
  LPARAM customData
) {
  *(HMONITOR*)customData = monitor;
  return false;
}

HMONITOR getMonitorAtPoint(POINT point) {
  RECT pointRect = {point.x, point.y, point.x + 1, point.y + 1};

  HMONITOR monitor = NULL;
  EnumDisplayMonitors(NULL, &pointRect, MonitorEnumProc, (LPARAM)(&monitor));

  return monitor;
}

HDC getInfoContext(HMONITOR monitor) {
  MONITORINFOEX monitorInfo;
  monitorInfo.cbSize = sizeof(MONITORINFOEX);
  GetMonitorInfo(monitor, &monitorInfo);
  return CreateIC(_T("DISPLAY"), monitorInfo.szDevice, NULL, NULL);
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
  HDC infoContext = getInfoContext(monitor);
  int pixelsPerLogicalInch = GetDeviceCaps(infoContext, LOGPIXELSY);
  DeleteDC(infoContext);

  return pixelsPerLogicalInch;
}

double logicalHeightToPointSize(int logicalHeight, HMONITOR monitor) {
  double pixelsPerLogicalInch = getPixelsPerLogicalInch(monitor);
  return abs(logicalHeight) * 72 / pixelsPerLogicalInch;
}

Rect getMonitorBounds(HMONITOR monitor) {
  MONITORINFOEX monitorInfo;
  monitorInfo.cbSize = sizeof(MONITORINFOEX);
  GetMonitorInfo(monitor, &monitorInfo);

  int x = monitorInfo.rcMonitor.left;
  int y = monitorInfo.rcMonitor.top;
  int width = monitorInfo.rcMonitor.right - y;
  int height = monitorInfo.rcMonitor.bottom - x;
  return Rect(x, y, width, height);
}

Size getTextSize(const Graphics &graphics, const Font *font, LPTSTR text) {
  RectF bounds;
  graphics.MeasureString(text, -1, font, PointF(0.0f, 0.0f), &bounds);
  return Size((int)round(bounds.Width), (int)round(bounds.Height));
}

StringFormat *getCenteredFormat() {
  static StringFormat format(StringFormatFlagsNoWrap, LANG_NEUTRAL);
  format.SetAlignment(StringAlignmentCenter);
  format.SetLineAlignment(StringAlignmentCenter);
  format.SetTrimming(StringTrimmingNone);
  return &format;
}

SIZE getBitmapSize(HBITMAP bitmap) {
  BITMAP bitmapInfo;
  GetObject(bitmap, sizeof(BITMAP), &bitmapInfo);

  SIZE bitmapSize;
  bitmapSize.cx = bitmapInfo.bmWidth;
  bitmapSize.cy = bitmapInfo.bmHeight;

  return bitmapSize;
}

typedef struct {
  LPTSTR fontFamily;
  double fontPointSize;
  Color textColor;
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
  SolidBrush *fillBrush;
  Pen *borderPen;
} ArtSupplies;

ArtSupplies getArtSupplies(Style style) {
  ArtSupplies artSupplies;
  artSupplies.font = new Font(style.fontFamily, style.fontPointSize);
  artSupplies.textBrush = new SolidBrush(style.textColor);
  artSupplies.fillBrush = new SolidBrush(style.fillColor);
  artSupplies.borderPen = new Pen(style.borderColor, style.borderWidth);
  return artSupplies;
}

void destroyArtSupplies(ArtSupplies artSupplies) {
  delete artSupplies.font;
  delete artSupplies.textBrush;
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
  REAL extraWidth = 0;
  if (style.borderWidth >= 1) {
    extraWidth = 0.5 * (style.borderWidth - 1);
  }
  REAL top = borderBounds.Y + extraWidth;
  REAL right = borderBounds.X + borderBounds.Width - 1 - extraWidth;
  REAL bottom = borderBounds.Y + borderBounds.Height - 1 - extraWidth;
  REAL left = borderBounds.X + extraWidth;

  REAL earHeight = style.earHeight - sqrt(2) * extraWidth + 0.2;

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
  REAL extraWidth = 0;
  if (style.borderWidth >= 1) {
    extraWidth = 0.5 * (style.borderWidth - 1);
  }
  REAL top = borderBounds.Y + extraWidth;
  REAL right = borderBounds.X + borderBounds.Width - 1 - extraWidth;
  REAL bottom = borderBounds.Y + borderBounds.Height - 1 - extraWidth;
  REAL left = borderBounds.X + extraWidth;

  REAL earHeight = style.earHeight - sqrt(2) * extraWidth + 0.2;

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
  REAL extraWidth = 0;
  if (style.borderWidth >= 1) {
    extraWidth = 0.5 * (style.borderWidth - 1);
  }
  REAL top = borderBounds.Y + extraWidth;
  REAL right = borderBounds.X + borderBounds.Width - 1 - extraWidth;
  REAL bottom = borderBounds.Y + borderBounds.Height - 1 - extraWidth;
  REAL left = borderBounds.X + extraWidth;

  REAL earHeight = style.earHeight - sqrt(2) * extraWidth + 0.2;

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
  REAL extraWidth = 0;
  if (style.borderWidth >= 1) {
    extraWidth = 0.5 * (style.borderWidth - 1);
  }
  REAL top = borderBounds.Y + extraWidth;
  REAL right = borderBounds.X + borderBounds.Width - 1 - extraWidth;
  REAL bottom = borderBounds.Y + borderBounds.Height - 1 - extraWidth;
  REAL left = borderBounds.X + extraWidth;

  REAL earHeight = style.earHeight - sqrt(2) * extraWidth + 0.2;

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
  LPTSTR chosenText
) {
  Size textSize = getTextSize(graphics, artSupplies.font, text);

  Rect screenBounds;
  graphics.GetClipBounds(&screenBounds);

  int earCorner = 0;
  Rect borderBounds = getBorderBounds(style, point, textSize, earCorner);

  if (!screenBounds.Contains(borderBounds)) {
    for (int i = 1; i < 4; i++) {
      Rect newBorderBounds = getBorderBounds(style, point, textSize, i);

      if (screenBounds.Contains(newBorderBounds)) {
        earCorner = i;
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
  graphics.DrawPath(artSupplies.borderPen, &border);

  RectF textBounds(
    borderBounds.X + style.borderWidth + style.paddingLeft,
    borderBounds.Y + style.borderWidth + style.paddingTop,
    textSize.Width,
    textSize.Height
  );

  graphics.DrawString(
    text, -1,
    artSupplies.font,
    textBounds,
    getCenteredFormat(),
    artSupplies.textBrush
  );
}

typedef struct {
  HMONITOR monitor;
  HDC deviceContext;
  Style style;
} Model;

Model getModel(HMONITOR monitor) {
  Model model;
  model.monitor = monitor;

  HDC infoContext = getInfoContext(monitor);
  model.deviceContext = CreateCompatibleDC(infoContext);
  DeleteDC(infoContext);

  LOGFONT fontInfo = getSystemTooltipFont();
  model.style.fontFamily = new TCHAR[_tcslen(fontInfo.lfFaceName) + 1];
  _tcscpy(model.style.fontFamily, fontInfo.lfFaceName);
  model.style.fontPointSize = logicalHeightToPointSize(
    fontInfo.lfHeight,
    monitor
  );

  model.style.textColor = getSystemColor(COLOR_INFOTEXT);
  model.style.fillColor = getSystemColor(COLOR_INFOBK);
  model.style.borderColor = Color(118, 118, 118);

  model.style.borderRadius = 2;
  model.style.borderWidth = 1;
  model.style.earHeight = 4;
  model.style.paddingTop = 0;
  model.style.paddingRight = 0;
  model.style.paddingBottom = 0;
  model.style.paddingLeft = 0;

  return model;
}

void destroyModel(Model model) {
  DeleteDC(model.deviceContext);
  delete[] model.style.fontFamily;
}

typedef struct {
  HDC deviceContext;
  POINT start;
  HBITMAP bitmap;
} View;

View getView(Model model) {
  View view;
  view.deviceContext = model.deviceContext;

  Rect monitorBounds = getMonitorBounds(model.monitor);
  view.start = {monitorBounds.X, monitorBounds.Y};

  view.bitmap = CreateCompatibleBitmap(
    getInfoContext(model.monitor),
    monitorBounds.Width,
    monitorBounds.Height
  );

  ArtSupplies artSupplies = getArtSupplies(model.style);

  HGDIOBJ oldBitmap = SelectObject(model.deviceContext, view.bitmap);

  Graphics graphics(model.deviceContext);
  graphics.SetClip(Rect(0, 0, monitorBounds.Width, monitorBounds.Height));
  graphics.SetSmoothingMode(SmoothingModeAntiAlias);

  for (int y = 0; y < 26; y++) {
    for (int x = 0; x < 26; x++) {
      drawBubble(
        graphics,
        model.style,
        artSupplies,
        Point(70 * x, 40 * y),
        _T("label"),
        _T("")
      );
    }
  }

  SelectObject(model.deviceContext, oldBitmap);

  destroyArtSupplies(artSupplies);

  return view;
}

void showView(View view, HWND window) {
  POINT origin;
  origin.x = 0;
  origin.y = 0;

  SIZE bitmapSize = getBitmapSize(view.bitmap);

  BLENDFUNCTION blendFunction;
  blendFunction.BlendOp = AC_SRC_OVER;
  blendFunction.BlendFlags = 0;
  blendFunction.SourceConstantAlpha = 255;
  blendFunction.AlphaFormat = AC_SRC_ALPHA;

  HGDIOBJ oldBitmap = SelectObject(view.deviceContext, view.bitmap);

  // Draw the layered (meaning partially transparent) window.
  // This is extremely finicky and would be almost impossible without a
  // working example to copy from.
  // Thankfully Marc Gregoire has me covered:
  // http://www.nuonsoft.com/blog/2009/05/27/how-to-use-updatelayeredwindow/

  UpdateLayeredWindow(
    window,
    NULL, // don't care about displays that use a color palette
    &view.start,
    &bitmapSize,
    view.deviceContext, // copies the active bitmap from this device context
    &origin, // copy bits starting from this location on the bitmap
    0, // no chromakey color
    &blendFunction, // extreme boilerplate
    ULW_ALPHA // per-pixel alpha instead of chromakey
  );

  SelectObject(view.deviceContext, oldBitmap);
}

void destroyView(View view) {
  DeleteObject(view.bitmap);
}

LRESULT CALLBACK WndProc(
  HWND window,
  UINT message,
  WPARAM wParam,
  LPARAM lParam
) {
  LRESULT result = 0;

  switch (message) {
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
  HMONITOR monitor = getMonitorAtPoint(cursorPos);

  Model model = getModel(monitor);
  View view = getView(model);

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
  destroyView(view);

  ShowWindow(window, showWindowFlags);

  MSG message;
  while (GetMessage(&message, NULL, 0, 0)) {
    TranslateMessage(&message);
    DispatchMessage(&message);
  }

  destroyModel(model);

  GdiplusShutdown(gdiplusToken);

  return (int)message.wParam;
}

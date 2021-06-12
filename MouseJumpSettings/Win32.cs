using System;
using System.Runtime.InteropServices;

namespace MouseJumpSettings
{
    // https://social.msdn.microsoft.com/Forums/vstudio/en-US/9beda373-b616-43e7-8eca-f2cc876385db/how-can-i-get-the-language-of-the-fontfamily
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct LOGFONTW
    {
        public int lfHeight;
        public int lfWidth;
        public int lfEscapement;
        public int lfOrientation;
        public int lfWeight;
        public byte lfItalic;
        public byte lfUnderline;
        public byte lfStrikeOut;
        public byte lfCharSet;
        public byte lfOutPrecision;
        public byte lfClipPrecision;
        public byte lfQuality;
        public byte lfPitchAndFamily;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string lfFaceName;
    }

    public delegate int EnumFontFamiliesExDelegate(ref LOGFONTW lplf, IntPtr lpntme, uint FontType, IntPtr lParam);

    public class Win32
    {
        [DllImport("gdi32", CharSet = CharSet.Unicode)]
        public static extern int EnumFontFamiliesExW(
            IntPtr hdc,
            ref MouseJumpSettings.LOGFONTW lpLogfont,
            MouseJumpSettings.EnumFontFamiliesExDelegate
            lpEnumFontFamExProc,
            IntPtr lParam,
            uint dwFlags
        );

        [DllImport("user32")]
        public static extern IntPtr GetDC(IntPtr hWnd);

        [DllImport("user32")]
        public static extern IntPtr ReleaseDC(IntPtr hWnd, IntPtr hdc);
    }
}

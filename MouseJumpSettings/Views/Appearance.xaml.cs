using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace MouseJumpSettings.Views
{
    /// <summary>
    /// An empty page that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class Appearance : Page
    {
        Settings settings;
        ObservableCollection<FontInfo> fonts = new ObservableCollection<FontInfo>();
        ObservableCollection<double> fontSizes = new ObservableCollection<double>
        {
            8, 9, 10, 11, 12, 14, 18, 24, 30, 36, 48, 60, 72, 96
        };

        public Appearance()
        {
            settings = (Application.Current as App).Settings;
            this.InitializeComponent();
            List<FontInfo> fontList = new List<FontInfo>();
            EnumFontFamiliesExDelegate callback =
                (ref LOGFONTW logfont, IntPtr metric, uint fontType, IntPtr lParam) => {
                if (
                    // https://devblogs.microsoft.com/oldnewthing/20120719-00/?p=7093
                    !logfont.lfFaceName.StartsWith("@")
                        && !logfont.lfFaceName.EndsWith(" Light")
                        && !logfont.lfFaceName.EndsWith(" Light Condensed")
                        && !logfont.lfFaceName.EndsWith(" Light SemiCondensed")
                        && !logfont.lfFaceName.EndsWith(" Semilight")
                        && !logfont.lfFaceName.EndsWith(" SemiLight")
                        && !logfont.lfFaceName.EndsWith(" SemiLight Condensed")
                        && !logfont.lfFaceName.EndsWith(" SemiLight SemiConde")
                        && !logfont.lfFaceName.EndsWith(" Semibold")
                        && !logfont.lfFaceName.EndsWith(" SemiBold")
                        && !logfont.lfFaceName.EndsWith(" SemiBold Condensed")
                        && !logfont.lfFaceName.EndsWith(" SemiBold SemiConden")
                        && !logfont.lfFaceName.EndsWith(" Medium")
                        && !logfont.lfFaceName.EndsWith(" Black")
                        && !logfont.lfFaceName.EndsWith(" MDL2 Assets")
                        && logfont.lfFaceName != "OpenSymbol"
                    )
                {
                    fontList.Add(
                        new FontInfo
                        {
                            Name = logfont.lfFaceName,
                            Weight = new Windows.UI.Text.FontWeight((ushort)logfont.lfWeight)
                        }
                    );
                }

                return 1;
            };

            IntPtr device = Win32.GetDC(IntPtr.Zero);
            LOGFONTW logfont = new LOGFONTW()
            {
                lfCharSet = 1,
                lfPitchAndFamily = 0,
                lfFaceName = "",
            };
            Win32.EnumFontFamiliesExW(device, ref logfont, callback, IntPtr.Zero, 0);
            Win32.ReleaseDC(IntPtr.Zero, device);
            // https://docs.microsoft.com/en-us/dotnet/csharp/linq/group-query-results
            foreach (
                FontInfo font in (
                    from font in fontList
                    orderby font.Name, font.Weight.Weight
                    group font by font.Name into nameGroup
                    select nameGroup.First() // minimum weight
                )
            )
            {
                fonts.Add(font);
            }
        }

        private void ColorPicker_Loaded(object sender, RoutedEventArgs e)
        {
            (sender as ColorPicker).Color = (Application.Current as App).Settings.LabelColor;
        }

        private void ColorPicker_ColorChanged(ColorPicker sender, ColorChangedEventArgs args)
        {
            (Application.Current as App).Settings.LabelColor = args.NewColor;
        }

        private void Font_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            object obj = e.AddedItems.First();
            if (obj is FontInfo val)
            {
                settings.Font = val.Name;
            }
            else if (obj is string str)
            {
                settings.Font = str;
            }
        }

        private void FontSize_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            object obj = e.AddedItems.First();
            if (obj is double val || (obj is string str && double.TryParse(str, out val)))
            {
                settings.FontSize = val;
            }
        }
    }
}

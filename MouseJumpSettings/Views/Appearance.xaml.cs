using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using System;
using System.Linq;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Runtime.InteropServices;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace MouseJumpSettings.Views
{
    /// <summary>
    /// An empty page that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class Appearance : Page
    {
        // https://docs.microsoft.com/en-us/windows/winui/api/microsoft.ui.xaml.controls.combobox?view=winui-3.0
        ObservableCollection<ComboBoxItem> fonts = new ObservableCollection<ComboBoxItem>();

        public Appearance()
        {
            this.InitializeComponent();
            LOGFONTW lf = new LOGFONTW();
            lf.lfCharSet = 1;
            lf.lfPitchAndFamily = 0;
            lf.lfFaceName = "";
            Dictionary<string, HashSet<int>> fontWeights = new Dictionary<string, HashSet<int>>();
            EnumFontFamiliesExDelegate d = (ref LOGFONTW lplf, IntPtr lpntme, uint FontType, IntPtr lParam) =>
            {
            if (
                // https://devblogs.microsoft.com/oldnewthing/20120719-00/?p=7093
                !lplf.lfFaceName.StartsWith("@")
                    && !lplf.lfFaceName.EndsWith(" Light")
                    && !lplf.lfFaceName.EndsWith(" Light Condensed")
                    && !lplf.lfFaceName.EndsWith(" Light SemiCondensed")
                    && !lplf.lfFaceName.EndsWith(" Semilight")
                    && !lplf.lfFaceName.EndsWith(" SemiLight")
                    && !lplf.lfFaceName.EndsWith(" SemiLight Condensed")
                    && !lplf.lfFaceName.EndsWith(" SemiLight SemiConde")
                    && !lplf.lfFaceName.EndsWith(" Semibold")
                    && !lplf.lfFaceName.EndsWith(" SemiBold")
                    && !lplf.lfFaceName.EndsWith(" SemiBold Condensed")
                    && !lplf.lfFaceName.EndsWith(" SemiBold SemiConden")
                    && !lplf.lfFaceName.EndsWith(" Medium")
                    && !lplf.lfFaceName.EndsWith(" Black")
                    && !lplf.lfFaceName.EndsWith(" MDL2 Assets")
                    && lplf.lfFaceName != "OpenSymbol"
                )
                {
                    HashSet<int> weights;
                    if (!fontWeights.TryGetValue(lplf.lfFaceName, out weights))
                    {
                        weights = new HashSet<int>();
                        fontWeights[lplf.lfFaceName] = weights;
                    }

                    weights.Add(lplf.lfWeight);
                }

                return 1;
            };

            IntPtr hdc = Win32.GetDC(IntPtr.Zero);
            var r = Win32.EnumFontFamiliesExW(hdc, ref lf, d, IntPtr.Zero, 0);
            Win32.ReleaseDC(IntPtr.Zero, hdc);
            foreach (KeyValuePair<string, HashSet<int>> pair in fontWeights.OrderBy(pair => pair.Key))
            {
                fonts.Add(
                    new ComboBoxItem {
                        Content = pair.Key,
                        FontFamily = new FontFamily(pair.Key),
                        FontWeight = new Windows.UI.Text.FontWeight((ushort)pair.Value.Min())
                    }
                );
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
    }
}

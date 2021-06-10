using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using System;
using System.Collections.Generic;
using System.Linq;
using Windows.Data.Json;
using Windows.UI;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace MouseJumpSettings.Views
{
    /// <summary>
    /// An empty page that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class Appearance : Page
    {
        public Appearance()
        {
            this.InitializeComponent();
        }

        private void pickerLoaded(object sender, RoutedEventArgs e)
        {
            string hexString = (Application.Current as App).Json.GetNamedString("labelColor");
            int sliceLength = (hexString.Length - 1) / 3;
            int[] channels = (
                from i in Enumerable.Range(0, 3)
                select (sliceLength == 1 ? 0x11 : 1) * int.Parse(
                    hexString.Substring(1 + i * sliceLength, sliceLength),
                    System.Globalization.NumberStyles.HexNumber,
                    System.Globalization.NumberFormatInfo.InvariantInfo
                )
            ).ToArray();
            
            (sender as ColorPicker).Color = Color.FromArgb(
                0xff, (byte)channels[0], (byte)channels[1], (byte)channels[2]
            );
        }
    }
}

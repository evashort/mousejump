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

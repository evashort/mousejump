using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Navigation;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices.WindowsRuntime;
using Windows.Foundation;
using Windows.Foundation.Collections;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace MouseJumpSettings
{
    /// <summary>
    /// An empty window that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class MainWindow : Window
    {
        public MainWindow()
        {
            this.InitializeComponent();
        }

        // https://techcommunity.microsoft.com/t5/windows-dev-appconsult/using-the-navigationview-in-your-uwp-applications/ba-p/317200
        private void nvTopLevelNav_Loaded(object sender, RoutedEventArgs e)
        {
            NavigationView navigationView = sender as NavigationView;
            // set the initial SelectedItem
            foreach (NavigationViewItemBase item in navigationView.MenuItems)
            {
                if (item is NavigationViewItem && (item.Content as TextBlock).Tag.ToString() == "Nav_About")
                {
                    navigationView.SelectedItem = item;
                    break;
                }
            }

            contentFrame.Navigate(typeof(Views.About));
        }

        private void nvTopLevelNav_ItemInvoked(NavigationView sender, NavigationViewItemInvokedEventArgs args)
        {
            TextBlock ItemContent = args.InvokedItem as TextBlock;
            if (ItemContent != null)
            {
                switch (ItemContent.Tag)
                {
                    case "Nav_About":
                        contentFrame.Navigate(typeof(Views.About));
                        break;

                    case "Nav_Appearance":
                        contentFrame.Navigate(typeof(Views.Appearance));
                        break;

                    case "Nav_Shortcuts":
                        contentFrame.Navigate(typeof(Views.Shortcuts));
                        break;

                    case "Nav_Labels":
                        contentFrame.Navigate(typeof(Views.Labels));
                        break;

                    case "Nav_Startup":
                        contentFrame.Navigate(typeof(Views.Startup));
                        break;

                    case "Nav_Review":
                        contentFrame.Navigate(typeof(Views.Review));
                        break;
                }
            }
        }
    }
}

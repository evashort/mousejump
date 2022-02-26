using Microsoft.UI.Xaml.Controls;
using System.Collections.ObjectModel;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace MouseJumpSettings.Views
{
    public sealed partial class Labels : Page
    {
        // https://docs.microsoft.com/en-us/windows/apps/design/controls/tree-view#tree-view-using-data-binding
        private ObservableCollection<Item> DataSource = new ObservableCollection<Item>();

        public Labels()
        {
            this.InitializeComponent();
            DataSource = GetData();
        }

        private ObservableCollection<Item> GetData()
        {
            return new ObservableCollection<Item>
            {
                new Item
                {
                    Name = "main",
                    Children = new ObservableCollection<Item>
                    {
                        new Item
                        {
                            Name = "letters",
                        },
                        new Item
                        {
                            Name = "numbers",
                        },
                    },
                },
                new Item
                {
                    Name = "unused",
                },
            };
        }
    }

    public class Item
    {
        public string Name { get; set; }
        public ObservableCollection<Item> Children { get; set; } = new ObservableCollection<Item>();

        public override string ToString()
        {
            return Name;
        }
    }
}

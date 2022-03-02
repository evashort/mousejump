using Microsoft.UI.Xaml;
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
        private int Priority { get; set; }
        private double Weight { get; set; }
        private string Output { get; set; }

        public Labels()
        {
            this.InitializeComponent();
            DataSource = GetData();
            outputBox.IsReadOnly = false;
            outputBox.Document.SetText(Microsoft.UI.Text.TextSetOptions.None, "1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n");
            outputBox.IsReadOnly = true;
        }

        private ObservableCollection<Item> GetData()
        {
            return new ObservableCollection<Item>
            {
                new Item
                {
                    Name = "main",
                    Operation = LabelOperation.Merge,
                    Children = new ObservableCollection<Item>
                    {
                        new Item
                        {
                            Name = "letters",
                            Operation = LabelOperation.Basic,
                            Weight = 1,
                        },
                        new Item
                        {
                            HasSeparator = true,
                            Name = "numbers",
                            Operation = LabelOperation.Edit,
                            Weight = 2,
                        },
                    },
                },
                new Item
                {
                    Name = "unused",
                    Operation = LabelOperation.Join,
                },
                new Item
                {
                    Name = "New list",
                    Operation = LabelOperation.New,
                },
            };
        }
    }

    public class Item
    {
        public string Name { get; set; }
        public LabelOperation Operation { get; set; }
        public string OperationName
        {
            get
            {
                return Operation switch
                {
                    LabelOperation.Basic => "Basic",
                    LabelOperation.Merge => "Merge",
                    LabelOperation.Join => "Join",
                    LabelOperation.Edit => "Edit",
                    LabelOperation.New => "Add",
                    _ => "",
                };
            }
        }
        public string PathData
        {
            get
            {
                // https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-fluent-icons-font
                // Edit > Select All
                // Path > Object to Path
                // order is important here because Object to Path can convert objects to stroked paths
                // Path > Stroke to Path
                // Path > Union
                // https://github.com/Klowner/inkscape-applytransforms
                // Extensions > Modify Path > Apply Transform
                return Operation switch
                {
                    LabelOperation.Basic => IconPaths.Basic,
                    LabelOperation.Merge => IconPaths.Merge,
                    LabelOperation.Join => IconPaths.Join,
                    LabelOperation.Edit => IconPaths.Edit,
                    LabelOperation.New => IconPaths.New,
                    _ => "",
                };
            }
        }
        public double? Weight { get; set; }
        public bool Used { get; set; }
        public bool HasSeparator { get; set; }
        public Visibility SeparatorVisibility {
            get { return HasSeparator ? Visibility.Visible : Visibility.Collapsed; }
        }
        public ObservableCollection<Item> Children { get; set; } = new ObservableCollection<Item>();

        public override string ToString()
        {
            return Name;
        }
    }
}

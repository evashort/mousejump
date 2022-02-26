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
                    Operation = Operation.Union,
                    Children = new ObservableCollection<Item>
                    {
                        new Item
                        {
                            Name = "letters",
                            Operation = Operation.Literal,
                            Weight = 1,
                        },
                        new Item
                        {
                            IsSeparator = true,
                        },
                        new Item
                        {
                            Name = "numbers",
                            Operation = Operation.Difference,
                            Weight = 2,
                        },
                    },
                },
                new Item
                {
                    Name = "unused",
                    Operation = Operation.Join,
                },
            };
        }
    }

    public enum Operation
    {
        Literal,
        Union,
        Join,
        Difference,
    }

    public class Item
    {
        public string Name { get; set; }
        public Operation Operation { get; set; }
        public string Icon
        {
            get
            {
                // https://docs.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
                switch (Operation)
                {
                    case Operation.Literal:
                        return "\xEA37";
                    case Operation.Union:
                        return "\xE948";
                    case Operation.Join:
                        return "\xE947";
                    case Operation.Difference:
                        return "\xE71C";
                }

                return "";
            }
        }
        public double? Weight { get; set; }
        public bool Used { get; set; }
        public bool IsSeparator { get; set; }
        public string AltText {
            get
            {
                if (IsSeparator)
                {
                    return "Separator";
                }

                string OperationText = "";
                switch (Operation)
                {
                    case Operation.Literal:
                        OperationText = "List";
                        break;
                    case Operation.Union:
                        OperationText = "Union";
                        break;
                    case Operation.Join:
                        OperationText = "Join";
                        break;
                    case Operation.Difference:
                        OperationText = "Filter";
                        break;
                }

                if (Weight != null)
                {
                    return $"{Name}, {OperationText}, {Weight}";
                }
                else
                {
                    return $"{Name}, {OperationText}";
                }
            }
        }
        public Visibility SeparatorVisibility {
            get { return IsSeparator ? Visibility.Visible : Visibility.Collapsed; }
        }
        public Visibility NameVisibility
        {
            get { return IsSeparator ? Visibility.Collapsed : Visibility.Visible; }
        }
        public ObservableCollection<Item> Children { get; set; } = new ObservableCollection<Item>();

        public override string ToString()
        {
            return Name;
        }
    }
}

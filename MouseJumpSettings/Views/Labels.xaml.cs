using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using System;
using System.Collections.ObjectModel;
using System.ComponentModel;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace MouseJumpSettings.Views
{
    public sealed partial class Labels : Page, INotifyPropertyChanged
    {
        public event PropertyChangedEventHandler PropertyChanged;

        private Settings settings;

        private LabelList selected;
        private LabelList Selected {
            get
            {
                return selected;
            }
            set
            {
                selected = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Index)));
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IndexVisibility)));
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(MaxIndex)));
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(GroupIndex)));
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(GroupIndexVisibility)));
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(MinGroupIndex)));
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(MaxGroupIndex)));
            }
        }

        private int Index {
            get => Selected != null && Selected.ParentOperation == LabelOperation.Join ? Selected.Index : 0;
            set => Selected.Index = value;
        }

        private double GroupIndex
        {
            get => Selected != null
                && Selected.ParentOperation == LabelOperation.Merge
                && Selected.Siblings.Count != 1
                ? Selected.Index - (Selected.InGroup ? 0 : 0.5) : 0;
            set
            {
                int intValue = (int)(value + 1);
                if (intValue - 1 == value)
                {
                    Selected.SetGroupIndex(intValue - 1);
                }
                else
                {
                    Selected.Index = intValue;
                }

                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(MaxIndex)));
                if (Selected.Index != value)
                {
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(GroupIndex)));
                }
            }
        }

        private Visibility IndexVisibility
        {
            get
            => Selected != null
            && Selected.ParentOperation == LabelOperation.Join
            ? Visibility.Visible : Visibility.Collapsed;
            set { }
        }

        private Visibility GroupIndexVisibility
        {
            get
            => Selected != null
            && Selected.ParentOperation == LabelOperation.Merge
            ? Visibility.Visible : Visibility.Collapsed;
            set { }
        }

        private int MaxIndex
        {
            get
            => Selected != null
            && Selected.ParentOperation == LabelOperation.Join
            ? selected.Siblings.Count - 1 : 0;
            set { }
        }

        private double MinGroupIndex
        {
            get
            => Selected != null
            && Selected.ParentOperation == LabelOperation.Merge
            && Selected.Siblings.Count != 1
            ? -0.5 : 0;
            set { }
        }

        private double MaxGroupIndex
        {
            get
            => Selected != null
            && Selected.ParentOperation == LabelOperation.Merge
            && Selected.Siblings.Count != 1
            ? Selected.SiblingGroupCount - (Selected.InGroup ? 0.5 : 1.5) : 0;
            set { }
        }

        private double Weight { get; set; }

        public Labels()
        {
            settings = (Application.Current as App).Settings;
            this.InitializeComponent();
            outputBox.IsReadOnly = false;
            outputBox.Document.SetText(Microsoft.UI.Text.TextSetOptions.None, "1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n");
            outputBox.IsReadOnly = true;
        }
    }
}

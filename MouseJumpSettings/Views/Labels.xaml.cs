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
        private LabelList Selected
        {
            get => selected;
            set
            {
                int oldMaxIndex = -MinNegIndex;
                selected = value;
                int newMaxIndex = -MinNegIndex;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IndexHeader)));
                if (newMaxIndex >= oldMaxIndex)
                {
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(MinNegIndex)));
                }

                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(NegIndex)));
                if (newMaxIndex < oldMaxIndex)
                {
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(MinNegIndex)));
                }
            }
        }

        private int NegIndex
        {
            get => Selected == null ? 0 : (
                Selected.ParentOperation == LabelOperation.Merge
                ? -2 * Selected.Index + (Selected.InGroup ? -1 : 0)
                : -Selected.Index
            );
            set
            {
                if (Selected == null)
                {
                    return;
                }

                if (Selected.ParentOperation == LabelOperation.Merge)
                {
                    if (-value % 2 == 0)
                    {
                        Selected.Index = -value / 2;
                    }
                    else
                    {
                        Selected.SetGroupIndex(-value / 2);
                    }

                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(MinNegIndex)));
                }
                else
                {
                    Selected.Index = -value;
                }
            }
        }

        private int MinNegIndex
        {
            get => Selected == null ? 0 : (
                Selected.ParentOperation == LabelOperation.Merge
                ? (Selected.InGroup ? 0 : 2) - 2 * Selected.SiblingGroupCount
                : 1 - Selected.SiblingGroupCount
            );
            set { }
        }

        private string IndexHeader
        {
            get => Selected != null && Selected.ParentOperation == LabelOperation.Merge
                ? "-Index (odd numbers are groups)"
                : "-Index";
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

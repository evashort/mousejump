using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using System;
using System.ComponentModel;
using System.Linq;

namespace MouseJumpSettings.Views
{
    public sealed partial class Labels : Page, INotifyPropertyChanged
    {
        private readonly Settings settings;

        private bool renaming = false;
        private LabelList selected;
        private LabelList Selected {
            get => selected;
            set
            {
                if (!renaming)
                {
                    selected = value;
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(SelectedName)));
                }
            }
        }

        private string SelectedName
        {
            get => Selected?.Name;
            set {
                if (selected == null)
                {
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(SelectedName)));
                }
                else if (value != Selected.Name)
                {
                    Selected.Name = value;
                    renaming = true;
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(LabelLists)));
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Selected)));
                    renaming = false;
                }
            }
        }

        private readonly NewList newList;
        private IOrderedEnumerable<IGrouping<LabelListGroup, LabelList>> LabelLists
        {
            get
            {
                return from labelList in (
                           from name in settings.LabelListNames
                           select settings.GetLabelList(name)
                       ).Append(newList)
                       orderby labelList.IsNew, labelList.Name
                       group labelList by labelList.Group into grp
                       orderby grp.Key
                       select grp;
            }
            set { }
        }

        public Labels()
        {
            settings = (Application.Current as App).Settings;
            newList = new(settings);
            this.InitializeComponent();
            outputBox.IsReadOnly = false;
            outputBox.Document.SetText(Microsoft.UI.Text.TextSetOptions.None, "1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n");
            outputBox.IsReadOnly = true;
        }

        public event PropertyChangedEventHandler PropertyChanged;
    }
}

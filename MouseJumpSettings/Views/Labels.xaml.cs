using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using System.Collections.Generic;
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

        private readonly List<NewList> newLists;
        private IOrderedEnumerable<IGrouping<LabelListGroup, LabelList>> LabelLists
        {
            get
            {
                return from labelList in (
                           from name in settings.LabelListNames
                           select settings.GetLabelList(name)
                       ).Concat(newLists)
                       orderby labelList.Name
                       group labelList by labelList.Group into grp
                       orderby grp.Key
                       select grp;
            }
            set { }
        }

        public Labels()
        {
            settings = (Application.Current as App).Settings;
            newLists = new()
            {
                new InputList(settings, "a-z input")
                {
                    Parent = settings.GetLabelList("a-z"),
                },
                new InputList(settings, "list 1"),
                new WrapList(settings, "aaa-zzz edited", LabelOperation.Edit) {
                    ParentsSelected = new List<KeyValuePair<LabelList, bool>>
                    {
                        new(settings.GetLabelList("default"), true),
                    },
                },
            };
            this.InitializeComponent();
            outputBox.IsReadOnly = false;
            outputBox.Document.SetText(Microsoft.UI.Text.TextSetOptions.None, "1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n");
            outputBox.IsReadOnly = true;
        }

        public event PropertyChangedEventHandler PropertyChanged;
    }
}

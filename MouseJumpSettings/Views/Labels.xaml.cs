using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System;

namespace MouseJumpSettings.Views
{
    public sealed partial class Labels : Page, INotifyPropertyChanged
    {
        private readonly Settings settings;

        private bool surpressSelectedChange = false;
        private LabelList Selected {
            get => settings.SelectedList;
            set
            {
                if (!surpressSelectedChange)
                {
                    settings.SelectedList = value;
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(SelectedName)));
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(SelectedIsJoin)));
                }
            }
        }

        private string SelectedName
        {
            get => Selected?.Name;
            set {
                if (Selected == null)
                {
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(SelectedName)));
                }
                else if (value != Selected.Name)
                {
                    Selected.Name = value;
                    surpressSelectedChange = true;
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(LabelLists)));
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(LabelListsGrouped)));
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(LabelSource)));
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Selected)));
                    surpressSelectedChange = false;
                }
            }
        }

        private bool SelectedIsJoin
        {
            get => Selected != null && Selected.Operation == LabelOperation.Join;
            set { }
        }

        private LabelList LabelSource
        {
            get => settings.LabelLists[settings.LabelSource];
            set
            {
                if (value != null)
                {
                    settings.LabelSource = value.Name;
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(LabelListsGrouped)));
                }
            }
        }

        private readonly List<NewList> newLists;
        private IOrderedEnumerable<LabelList> LabelLists
        {
            get => settings.LabelLists.Values.OrderBy(labelList => labelList.Name);
            set { }
        }

        private IOrderedEnumerable<IGrouping<LabelListGroup, LabelList>> LabelListsGrouped
        {
            get
            {
                return from labelList
                       in settings.LabelLists.Values.Concat(newLists)
                       orderby labelList.Name
                       group labelList by labelList.Group into grp
                       orderby grp.Key
                       select grp;
            }
            set { }
        }

        public CombinedObservableCollection PossibleInputs => settings.possibleInputs;

        public Labels()
        {
            settings = (Application.Current as App).Settings;
            newLists = new();
            this.InitializeComponent();
            outputBox.IsReadOnly = false;
            outputBox.Document.SetText(Microsoft.UI.Text.TextSetOptions.None, "1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n1\n2\n3\n4\n5\n6\n7\n8\n9\n");
            outputBox.IsReadOnly = true;
        }

        private static int? GetNumberSuffix(string prefix, string name)
        {
            if (name.StartsWith(prefix) && int.TryParse(name.AsSpan(prefix.Length), out int suffix))
            {
                return suffix;
            }

            return null;
        }

        private void CreateLabelList(object sender, RoutedEventArgs e)
        {
            const string prefix = "list ";
            int lastSuffix = (
                from name in settings.LabelLists.Keys.Concat(
                    from labelList in newLists select labelList.Name)
                select GetNumberSuffix(prefix, name)).Max() ?? 0;
            lastSuffix = lastSuffix < 0 ? 0 : lastSuffix;
            InputList newList = new(settings, prefix + (lastSuffix + 1));
            newLists.Add(newList);
            labelListToFocus = newList;
            surpressSelectedChange = true;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(LabelListsGrouped)));
            surpressSelectedChange = false;
            Selected = newList;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Selected)));
        }

        private LabelList labelListToFocus;
        private ILabelInput inputToFocus;

        public event PropertyChangedEventHandler PropertyChanged;

        private void LabelListsView_LayoutUpdated(object sender, object e)
        {
            if (labelListToFocus == null)
            {
                return;
            }

            if (labelListsView.ContainerFromItem(labelListToFocus) is ListViewItem item)
            {
                item.Focus(FocusState.Programmatic);
                item.StartBringIntoView();
            }

            labelListToFocus = null;
        }

        private void InputsView_ItemClick(object sender, ItemClickEventArgs e)
        {
            if (settings.SelectedList != null && e.ClickedItem is LabelList labelList)
            {
                inputToFocus = labelList;
                labelList.IsInput = !labelList.IsInput;
            }
        }

        private void InputsView_LayoutUpdated(object sender, object e)
        {
            if (inputToFocus == null)
            {
                return;
            }

            if (inputsView.ContainerFromItem(inputToFocus) is ListViewItem item)
            {
                item.StartBringIntoView();
            }

            inputToFocus = null;
        }
    }
}

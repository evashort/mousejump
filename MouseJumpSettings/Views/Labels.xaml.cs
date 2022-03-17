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
            get => settings.selectedList;
            set
            {
                if (!surpressSelectedChange)
                {
                    settings.selectedList = value;
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(SelectedName)));
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(SelectedIsInterleave)));
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Inputs)));
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

        private bool SelectedIsInterleave
        {
            get => Selected != null && Selected.Operation == LabelOperation.Interleave;
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

        public IOrderedEnumerable<IGrouping<string, LabelList>> Inputs
        {
            get
            {
                if (settings.selectedList == null)
                {
                    return from labelList in Enumerable.Empty<LabelList>()
                           group labelList by "" into grp
                           orderby 1
                           select grp;
                }

                HashSet<string> ancestors = new();
                Queue<string> fringe = new();
                fringe.Enqueue(settings.selectedList.Name);
                while (fringe.TryPeek(out string child))
                {
                    fringe.Dequeue();
                    if (ancestors.Add(child))
                    {
                        foreach (string parent in settings.GetLabelListParents(child))
                        {
                            fringe.Enqueue(parent);
                        }
                    }
                }

                HashSet<string> children = new(settings.GetLabelListChildren(settings.selectedList.Name));
                return from labelList in settings.LabelLists.Values
                       where !ancestors.Contains(labelList.Name)
                       orderby labelList.Name
                       group labelList by (
                       children.Contains(labelList.Name) ? "Selected" : "Unselected"
                       ) into grp
                       orderby grp.Key != "Selected"
                       select grp;
            }
            set { }
        }

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
            toBringIntoView = newList;
            surpressSelectedChange = true;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(LabelListsGrouped)));
            surpressSelectedChange = false;
            Selected = newList;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Selected)));
        }

        private LabelList toBringIntoView;
        private LabelList inputToFocus;

        public event PropertyChangedEventHandler PropertyChanged;

        private void LabelListsView_LayoutUpdated(object sender, object e)
        {
            if (toBringIntoView == null)
            {
                return;
            }

            if (labelListsView.ContainerFromItem(toBringIntoView) is ListViewItem item)
            {
                item.StartBringIntoView();
            }

            toBringIntoView = null;
        }

        private void InputsView_ItemClick(object sender, ItemClickEventArgs e)
        {
            if (settings.selectedList != null && e.ClickedItem is LabelList labelList)
            {
                if (settings.AddLabelListChild(settings.selectedList.Name, labelList.Name))
                {
                    inputToFocus = labelList;
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Inputs)));
                }
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
                item.Focus(FocusState.Programmatic);
            }

            inputToFocus = null;
        }
    }
}

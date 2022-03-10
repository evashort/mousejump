using Microsoft.UI.Xaml;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using Windows.Data.Json;

namespace MouseJumpSettings.Views
{
    public class LabelList : INotifyPropertyChanged
    {
        private readonly Settings settings;
        private string name;
        private string parentName;
        private int index;

        public event PropertyChangedEventHandler PropertyChanged;

        public LabelList(Settings settings, string name, string parentName, int index=0)
        {
            this.settings = settings;
            this.name = name;
            this.parentName = parentName;
            this.index = index;
        }

        public bool InGroup => !settings.IsSingletonGroup(parentName, index);

        public int SiblingGroupCount => settings.CountGroups(parentName);

        public int Index
        {
            get => index;
            set
            {
                ObservableSortedList<LabelList> siblings = Siblings;
                int removeIndex = siblings.BinarySearch(this);
                bool shifted = !settings.RemoveChildAt(name, parentName, index, out double weight);
                settings.InsertChild(name, parentName, value, weight);
                if (shifted)
                {
                    foreach (LabelList sibling in siblings)
                    {
                        if (sibling.index >= value) { sibling.index++; }
                    }
                }
                else if (value > index)
                {
                    foreach (LabelList sibling in siblings)
                    {
                        if (sibling.index > value) { break; }
                        if (sibling.index > index) { sibling.index--; }
                    }
                }
                else if (value < index)
                {
                    foreach (LabelList sibling in siblings)
                    {
                        if (sibling.index >= index) { break; }
                        if (sibling.index >= value) { sibling.index++; }
                    }
                }

                index = value;
                int insertIndex = siblings.ChangedAt(removeIndex);
                if (insertIndex == removeIndex)
                {
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(SeparatorVisibility)));
                }

                if (insertIndex + 1 < siblings.Count)
                {
                    LabelList successor = siblings[insertIndex + 1];
                    successor.PropertyChanged?.Invoke(successor, new PropertyChangedEventArgs(nameof(SeparatorVisibility)));
                }

                if (removeIndex < siblings.Count)
                {
                    LabelList successor = siblings[removeIndex];
                    successor.PropertyChanged?.Invoke(successor, new PropertyChangedEventArgs(nameof(SeparatorVisibility)));
                }
            }
        }

        public void SetGroupIndex(int newIndex)
        {
            ObservableSortedList<LabelList> siblings = Siblings;
            int removeIndex = siblings.BinarySearch(this);
            bool shifted = settings.RemoveChildAt(name, parentName, index, out double weight);
            settings.AddChildToGroup(name, parentName, newIndex, weight);
            if (shifted)
            {
                for (int i = removeIndex + 1; i < siblings.Count; i++)
                {
                    siblings[i].index--;
                }
            }

            index = newIndex;
            int insertIndex = siblings.ChangedAt(removeIndex);
            if (insertIndex == removeIndex)
            {
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(SeparatorVisibility)));
            }

            if (insertIndex + 1 < siblings.Count)
            {
                LabelList successor = siblings[insertIndex + 1];
                successor.PropertyChanged?.Invoke(successor, new PropertyChangedEventArgs(nameof(SeparatorVisibility)));
            }

            if (removeIndex < siblings.Count)
            {
                LabelList successor = siblings[removeIndex];
                successor.PropertyChanged?.Invoke(successor, new PropertyChangedEventArgs(nameof(SeparatorVisibility)));
            }
        }

        public double? Weight => settings.GetWeight(name, parentName, index);
        public Visibility SeparatorVisibility
        {
            get
            {
                if (index > 0 && settings.GetOperation(parentName) == LabelOperation.Merge) {
                    foreach (LabelList sibling in Siblings)
                    {
                        if (sibling.Index == index)
                        {
                            return sibling.Name == name ? Visibility.Visible : Visibility.Collapsed;
                        }
                    }
                }

                return Visibility.Collapsed;
            }
            set { }
        }

        public ObservableSortedList<LabelList> Children => settings.GetChildren(Name);
        public ObservableSortedList<LabelList> Siblings => settings.GetChildren(parentName);
        public string Name => name;
        public LabelOperation Operation => settings.GetOperation(name);
        public LabelOperation ParentOperation => settings.GetOperation(parentName);
        public string OperationName => Operation switch
        {
            LabelOperation.Basic => "Basic",
            LabelOperation.Merge => "Merge",
            LabelOperation.Join => "Join",
            LabelOperation.Edit => "Edit",
            LabelOperation.New => "",
            _ => throw new NotImplementedException(),
        };
        public string IconPath => Operation switch
        {
            LabelOperation.Basic => IconPaths.Basic,
            LabelOperation.Merge => IconPaths.Merge,
            LabelOperation.Join => IconPaths.Join,
            LabelOperation.Edit => IconPaths.Edit,
            LabelOperation.New => IconPaths.New,
            _ => throw new NotImplementedException(),
        };
    }

    public class LabelListComparer : IComparer<LabelList>
    {
        public int Compare(LabelList a, LabelList b)
        {
            if (a.Index < b.Index)
            {
                return -1;
            }
            else if (a.Index > b.Index)
            {
                return 1;
            }

            double aWeight = a.Weight ?? 0;
            double bWeight = b.Weight ?? 0;
            if (aWeight > bWeight)
            {
                return -1;
            }
            else if (aWeight < bWeight)
            {
                return 1;
            }

            return a.Name.CompareTo(b.Name);
        }
    }

    public enum LabelOperation
    {
        Basic,
        Merge,
        Join,
        Edit,
        New,
    }
}

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
                bool shifted = settings.SetIndex(name, parentName, index, value);
                if (shifted)
                {
                    foreach (LabelList sibling in Siblings)
                    {
                        if (sibling.index >= value) { sibling.index++; }
                    }
                }
                else if (value > index)
                {
                    foreach (LabelList sibling in Siblings)
                    {
                        if (sibling.index > value) { break; }
                        if (sibling.index > index) { sibling.index--; }
                    }
                }
                else if (value < index)
                {
                    foreach (LabelList sibling in Siblings)
                    {
                        if (sibling.index >= index) { break; }
                        if (sibling.index >= value) { sibling.index++; }
                    }
                }

                index = value;
                // https://github.com/microsoft/microsoft-ui-xaml/issues/3119
                int removeIndex = Siblings.IndexOf(this);
                int insertIndex = 0;
                for (
                    ;
                    insertIndex < Siblings.Count && (
                        insertIndex == removeIndex || Siblings[insertIndex].index < index
                    );
                    insertIndex++
                ) { }
                if (insertIndex > removeIndex)
                {
                    insertIndex--;
                }

                if (insertIndex != removeIndex)
                {
                    Siblings.RemoveAt(removeIndex);
                    Siblings.Insert(insertIndex, this);
                }
                else
                {
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(SeparatorVisibility)));
                }

                if (insertIndex <= removeIndex)
                {
                    removeIndex++;
                }

                if (insertIndex + 1 < Siblings.Count)
                {
                    LabelList successor = Siblings[insertIndex + 1];
                    successor.PropertyChanged?.Invoke(successor, new PropertyChangedEventArgs(nameof(SeparatorVisibility)));
                }

                if (removeIndex < Siblings.Count)
                {
                    LabelList successor = Siblings[removeIndex];
                    successor.PropertyChanged?.Invoke(successor, new PropertyChangedEventArgs(nameof(SeparatorVisibility)));
                }
            }
        }

        public void SetGroupIndex(int newIndex)
        {
            bool shifted = settings.SetGroupIndex(name, parentName, index, newIndex);
            if (shifted)
            {
                foreach (LabelList sibling in Siblings)
                {
                    if (sibling.index > index) { sibling.index--; }
                }
            }

            index = newIndex;
            // https://github.com/microsoft/microsoft-ui-xaml/issues/3119
            int removeIndex = Siblings.IndexOf(this);
            int insertIndex = 0;
            for (; insertIndex < Siblings.Count; insertIndex++)
            {
                if (insertIndex == removeIndex) {
                    continue;
                }

                LabelList sibling = Siblings[insertIndex];
                if (
                    sibling.index > index || (
                        sibling.index == index && (
                            sibling.Weight < Weight || (
                                sibling.Weight == Weight && sibling.name.CompareTo(name) >= 0
                            )
                        )
                    )
                )
                { break; }
            }

            if (insertIndex > removeIndex)
            {
                insertIndex--;
            }

            if (insertIndex != removeIndex)
            {
                Siblings.RemoveAt(removeIndex);
                Siblings.Insert(insertIndex, this);
            }
            else
            {
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(SeparatorVisibility)));
            }

            if (insertIndex <= removeIndex)
            {
                removeIndex++;
            }

            if (insertIndex + 1 < Siblings.Count)
            {
                LabelList successor = Siblings[insertIndex + 1];
                successor.PropertyChanged?.Invoke(successor, new PropertyChangedEventArgs(nameof(SeparatorVisibility)));
            }

            if (removeIndex < Siblings.Count)
            {
                LabelList successor = Siblings[removeIndex];
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

        public ObservableCollection<LabelList> Children => settings.GetChildren(Name);
        public ObservableCollection<LabelList> Siblings => settings.GetChildren(parentName);
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

    public enum LabelOperation
    {
        Basic,
        Merge,
        Join,
        Edit,
        New,
    }
}

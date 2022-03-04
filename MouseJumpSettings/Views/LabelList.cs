using Microsoft.UI.Xaml;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using Windows.Data.Json;

namespace MouseJumpSettings.Views
{
    public class LabelList
    {
        private readonly Settings settings;
        private string name;
        private string parentName;
        private int index;

        public LabelList(Settings settings, string name, string parentName, int index=0)
        {
            this.settings = settings;
            this.name = name;
            this.parentName = parentName;
            this.index = index;
        }

        public int Index => index;
        public double? Weight => settings.GetWeight(name, parentName, index);
        public Visibility SeparatorVisibility
        {
            get
            {
                if (settings.GetOperation(parentName) == LabelOperation.Merge) {
                    foreach (LabelList sibling in settings.GetChildren(parentName))
                    {
                        if (sibling.Index == index)
                        {
                            return sibling.Name == name ? Visibility.Visible : Visibility.Collapsed;
                        }
                    }
                }

                return Visibility.Collapsed;
            }
        }
        public ObservableCollection<LabelList> Children => settings.GetChildren(Name);
        public string Name => name;
        public LabelOperation Operation => settings.GetOperation(name);
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

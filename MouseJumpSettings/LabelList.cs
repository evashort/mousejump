using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;

namespace MouseJumpSettings
{
    public class LabelList : INotifyPropertyChanged
    {
        public readonly Settings settings;
        protected string name;

        public event PropertyChangedEventHandler PropertyChanged;

        public virtual LabelOperation Operation {
            get => settings.GetLabelListOperation(name);
            set => throw new NotSupportedException();
        }

        public LabelList(Settings settings, string name)
        {
            this.settings = settings;
            this.name = name;
        }

        public virtual string Name
        {
            get => name;
            set
            {
                if (settings.RenameLabelList(name, value))
                {
                    name = value;
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Name)));
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Title)));
                }
            }
        }

        public virtual string Title => Name;

        public virtual string IconPath => IconPaths.FromOperation(Operation);

        public virtual int Depth => settings.GetLabelListDepths(settings.LabelSource).GetValueOrDefault(name, -1);

        public virtual LabelListGroup Group => LabelListGroup.FromDepth(Depth);

        public override string ToString() => Name;

        public virtual int Index {
            get {
                int index = settings.Inputs.IndexOf(this);
                return index >= 0 ? -index : 0;
            }
            set => settings.MoveLabelListInput(this, -value);
        }

        public virtual int MinIndex {
            get => 1 - settings.Inputs.Count;
            set => throw new NotSupportedException();
        }

        public virtual double Weight {
            get => settings.GetLabelListInputWeight(this);
            set => settings.SetLabelListInputWeight(this, value);
        }

        public virtual bool IsInput {
            get => settings.Inputs.Contains(this);
            set {
                if (IsInput == value)
                {
                    return;
                }

                if (value)
                {
                    settings.AddLabelListInput(this);
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(MinIndex)));
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Index)));
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsInput)));
                }
                else
                {
                    settings.RemoveLabelListInput(this);
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsInput)));
                    // ensure index number box gets set to zero so that the
                    // number box doesn't try to set the index to the new
                    // MinIndex next time this label list is added as an input
                    // if the number of inputs is smaller than it was before
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Index)));
                }
            }
        }

        public virtual void IndexChanged()
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Index)));
        }

        public virtual void MinIndexChanged()
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(MinIndex)));
        }

        public virtual void IsInputChanged()
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsInput)));
        }

        public virtual void DepthChanged()
        {
            // TODO: call this!
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Depth)));
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Group)));
        }
    }
}

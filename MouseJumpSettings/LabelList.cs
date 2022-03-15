using System;
using System.Collections.Generic;
using System.ComponentModel;

namespace MouseJumpSettings
{
    public abstract class LabelList : INotifyPropertyChanged
    {
        public static LabelList Create(Settings settings, string name)
            => settings.GetLabelListOperation(name) switch
            {
                LabelOperation.Split => new BasicList(settings, name),
                LabelOperation.Edit => new EditList(settings, name),
                LabelOperation.Union => new UnionList(settings, name),
                LabelOperation.Interleave => new InterleaveList(settings, name),
                LabelOperation.Join => new JoinList(settings, name),
                _ => throw new InvalidEnumArgumentException(
                    "operation",
                    (int)settings.GetLabelListOperation(name),
                    typeof(LabelOperation)),
            };

        protected readonly Settings settings;
        protected string name;
        public virtual LabelOperation Operation {
            get => throw new NotImplementedException();
            set => throw new NotImplementedException();
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
                }
                else
                {
                    PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Name)));
                }
            }
        }

        public virtual string Title => Name;

        public virtual string IconPath => IconPaths.FromOperation(Operation);

        public virtual int Depth => settings.GetLabelListDepths(settings.LabelSource).GetValueOrDefault(name, -1);

        public virtual LabelListGroup Group => LabelListGroup.FromDepth(Depth);

        public override string ToString() => Name;

        public event PropertyChangedEventHandler PropertyChanged;
    }
}

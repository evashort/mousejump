using System.Collections.Generic;
using System.ComponentModel;

namespace MouseJumpSettings
{
    public abstract class LabelList : INotifyPropertyChanged
    {
        public static LabelList Create(Settings settings, string name)
            => settings.GetLabelListOperation(name) switch
            {
                Settings.Operation.Split => new BasicList(settings, name),
                Settings.Operation.Edit => new EditList(settings, name),
                Settings.Operation.Union => new UnionList(settings, name),
                Settings.Operation.Interleave => new InterleaveList(settings, name),
                Settings.Operation.Join => new JoinList(settings, name),
                _ => throw new InvalidEnumArgumentException(),
            };

        protected readonly Settings settings;
        protected string name;

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

        public abstract string IconPath { get; }

        public virtual bool IsNew => false;

        public virtual int Depth => settings.GetLabelListDepths(settings.LabelSource).GetValueOrDefault(name, -1);

        public virtual LabelListGroup Group => LabelListGroup.FromDepth(Depth);

        public event PropertyChangedEventHandler PropertyChanged;
    }
}

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using Windows.Globalization.NumberFormatting;

namespace MouseJumpSettings
{
    public abstract class LabelList : ILabelInput
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

        public readonly Settings settings;
        protected string name;

        public abstract event PropertyChangedEventHandler PropertyChanged;

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
            }
        }

        public virtual string Title => Name;

        public virtual string IconPath => IconPaths.FromOperation(Operation);

        public virtual int Depth => settings.GetLabelListDepths(settings.LabelSource).GetValueOrDefault(name, -1);

        public virtual LabelListGroup Group => LabelListGroup.FromDepth(Depth);

        public override string ToString() => Name;

        public virtual IEnumerable<ILabelInput> Inputs => Enumerable.Empty<ILabelInput>();

        public virtual ILabelInput AddInput(string child)
        {
            throw new NotImplementedException();
        }

        public virtual void MoveInput(int oldIndex, int newIndex)
        {
            throw new NotImplementedException();
        }

        public virtual LabelList AsList => this;

        public virtual int Index { get => 0; set => throw new NotImplementedException(); }
        public virtual int MinIndex { get => 0; set => throw new NotImplementedException(); }

        public INumberFormatter2 IndexFormatter => NegativeIntFormatter.Instance;

        public virtual double Weight { get => 1; set => throw new NotImplementedException(); }

        public virtual bool IsInput => false;
    }
}

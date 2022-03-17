using System;
using System.ComponentModel;
using Windows.Globalization.NumberFormatting;

namespace MouseJumpSettings
{
    public abstract class LabelInput : ILabelInput
    {
        public readonly string parent;
        public readonly LabelList list;

        public abstract event PropertyChangedEventHandler PropertyChanged;

        public LabelList AsList => list;

        public double Weight { get => 1; set => throw new NotImplementedException(); }

        public virtual int Index { get => 0; set => throw new NotImplementedException(); }

        public virtual int MinIndex
        {
            get => -list.settings.CountLabelListChildren(parent) + 1;
            set { }
        }

        public INumberFormatter2 IndexFormatter => NegativeIntFormatter.Instance;

        public virtual bool IsInput => true;

        public LabelInput(string parent, LabelList list)
        {
            this.parent = parent;
            this.list = list;
        }
    }
}

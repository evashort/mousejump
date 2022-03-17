using System.ComponentModel;
using Windows.Globalization.NumberFormatting;

namespace MouseJumpSettings
{
    public interface ILabelInput : INotifyPropertyChanged
    {
        public LabelList AsList { get; }
        public int Index { get; set; }
        public int MinIndex { get; set; }
        public INumberFormatter2 IndexFormatter { get; }
        public double Weight { get; set; }
        public bool IsInput { get; }
    }
}

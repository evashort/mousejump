using System.ComponentModel;

namespace MouseJumpSettings
{
    public class BasicList : LabelList
    {
        public override event PropertyChangedEventHandler PropertyChanged;

        public BasicList(Settings settings, string name) : base(settings, name)
        { }

        public override LabelOperation Operation => LabelOperation.Split;
    }
}

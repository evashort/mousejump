using System.ComponentModel;

namespace MouseJumpSettings
{
    public class InterleaveList : LabelList
    {
        public override event PropertyChangedEventHandler PropertyChanged;

        public InterleaveList(Settings settings, string name) : base(settings, name)
        { }

        public override LabelOperation Operation => LabelOperation.Interleave;
    }
}

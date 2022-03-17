using System.ComponentModel;

namespace MouseJumpSettings
{
    public class UnionList : LabelList
    {
        public override event PropertyChangedEventHandler PropertyChanged;

        public UnionList(Settings settings, string name) : base(settings, name)
        { }

        public override LabelOperation Operation => LabelOperation.Union;
    }
}

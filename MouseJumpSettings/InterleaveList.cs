namespace MouseJumpSettings
{
    public class InterleaveList : LabelList
    {
        public InterleaveList(Settings settings, string name) : base(settings, name)
        { }

        public override LabelOperation Operation => LabelOperation.Interleave;
    }
}

namespace MouseJumpSettings
{
    public class BasicList : LabelList
    {
        public BasicList(Settings settings, string name) : base(settings, name)
        { }

        public override LabelOperation Operation => LabelOperation.Split;
    }
}

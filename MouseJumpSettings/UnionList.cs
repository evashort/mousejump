namespace MouseJumpSettings
{
    public class UnionList : LabelList
    {
        public UnionList(Settings settings, string name) : base(settings, name)
        { }

        public override LabelOperation Operation => LabelOperation.Union;
    }
}

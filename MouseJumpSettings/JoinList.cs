namespace MouseJumpSettings
{
    public class JoinList : LabelList
    {
        public JoinList(Settings settings, string name) : base(settings, name)
        { }

        public override LabelOperation Operation => LabelOperation.Join;
    }
}

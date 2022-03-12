namespace MouseJumpSettings
{
    public class BasicList : LabelList
    {
        public BasicList(Settings settings, string name) : base(settings, name)
        { }

        public override string IconPath => IconPaths.Basic;
    }
}

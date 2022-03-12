namespace MouseJumpSettings
{
    internal class JoinList : LabelList
    {
        public JoinList(Settings settings, string name) : base(settings, name)
        { }

        public override string IconPath => IconPaths.Join;
    }
}

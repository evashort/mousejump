namespace MouseJumpSettings
{
    public class InterleaveList : LabelList
    {
        public InterleaveList(Settings settings, string name) : base(settings, name)
        { }

        public override string IconPath => IconPaths.Interleave;
    }
}

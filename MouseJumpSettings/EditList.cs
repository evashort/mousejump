namespace MouseJumpSettings
{
    public class EditList : LabelList
    {
        public EditList(Settings settings, string name) : base(settings, name)
        { }

        public override string IconPath => IconPaths.Edit;
    }
}

using System;

namespace MouseJumpSettings
{
    internal class NewList : LabelList
    {
        public NewList(Settings settings) : base(settings, null)
        { }

        public override string Name
        {
            get => "New...";
            set => throw new InvalidOperationException();
        }

        public override string IconPath => IconPaths.New;

        public override bool IsNew => true;

        public override int Depth => 0;

        public override LabelListGroup Group => LabelListGroup.Unused;
    }
}

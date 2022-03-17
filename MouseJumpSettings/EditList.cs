using System.ComponentModel;

namespace MouseJumpSettings
{
    public class EditList : LabelList
    {
        public override event PropertyChangedEventHandler PropertyChanged;

        public EditList(Settings settings, string name) : base(settings, name)
        { }

        public override LabelOperation Operation => LabelOperation.Edit;
    }
}

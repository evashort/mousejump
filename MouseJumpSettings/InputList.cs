using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;

namespace MouseJumpSettings
{
    public class InputList : NewList
    {
        public InputList(Settings settings, string name) : base(settings, name)
        { }

        public LabelList Parent { get; set; }

        public override IEnumerable<LabelList> Parents
            => Parent == null ? Enumerable.Empty<LabelList>() : Enumerable.Repeat(Parent, 1);
    }
}

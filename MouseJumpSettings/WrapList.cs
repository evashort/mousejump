using System.Collections.Generic;
using System.Linq;

namespace MouseJumpSettings
{
    internal class WrapList : NewList
    {
        public WrapList(Settings settings, string name, LabelOperation operation)
            : base(settings, name, operation)
        { }

        public LabelList Wrapped { get; set; }

        public List<KeyValuePair<LabelList, bool>> ParentsSelected { get; set; }

        public override IEnumerable<LabelList> Parents
            => ParentsSelected == null
            ? Enumerable.Empty<LabelList>()
            : from pair in ParentsSelected where pair.Value select pair.Key;
    }
}

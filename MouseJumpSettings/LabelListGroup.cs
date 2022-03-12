using System;
using System.Collections.Generic;

namespace MouseJumpSettings
{
    public class LabelListGroup : IComparable<LabelListGroup>
    {
        private readonly int index;

        public static readonly LabelListGroup Primary = new(0, "Primary");
        public static readonly LabelListGroup Secondary = new(1, "Secondary");
        public static readonly LabelListGroup Other = new(3, "Other");
        public static readonly LabelListGroup Unused = new(4, "Unused");
        public static LabelListGroup FromDepth(int depth) => depth switch
        {
            -1 => Unused,
            0 => Primary,
            1 => Secondary,
            _ => Other,
        };

        private LabelListGroup(int index, string name)
        {
            this.index = index;
            Name = name;
        }

        public string Name { get; private set; }

        public int CompareTo(LabelListGroup other)
        {
            return Comparer<int>.Default.Compare(index, other.index);
        }
    }
}

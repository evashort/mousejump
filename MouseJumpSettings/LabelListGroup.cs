using System;
using System.Collections.Generic;

namespace MouseJumpSettings
{
    public class LabelListGroup : IComparable<LabelListGroup>
    {
        private readonly int index;

        public static readonly LabelListGroup Parent = new(0, "Parent");
        public static readonly LabelListGroup Children = new(1, "Children");
        public static readonly LabelListGroup Grandchildren = new(2, "Grandchildren");
        public static readonly LabelListGroup Descendants = new(3, "Descendants");
        public static readonly LabelListGroup Unused = new(4, "Unused");
        public static LabelListGroup FromDepth(int depth) => depth switch
        {
            -1 => Unused,
            0 => Parent,
            1 => Children,
            2 => Grandchildren,
            _ => Descendants,
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

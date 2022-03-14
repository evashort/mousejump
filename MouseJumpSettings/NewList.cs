using System.Collections.Generic;

namespace MouseJumpSettings
{
    public abstract class NewList : LabelList
    {
        public NewList(Settings settings, string name, LabelOperation operation = LabelOperation.Split)
            : base(settings, name)
        {
            Operation = operation;
        }

        public abstract IEnumerable<LabelList> Parents { get; }

        public override LabelOperation Operation { get; set; }

        public override string Name
        {
            get => name;
            set => name = value;
        }

        public override string Title => $"*{Name}";

        public override string IconPath => IconPaths.FromOperation(Operation);

        public override int Depth
        {
            get
            {
                IEnumerator<LabelList> parents = Parents.GetEnumerator();
                if (!parents.MoveNext())
                {
                    return -1;
                }

                Dictionary<string, int> depths = settings.GetLabelListDepths(settings.LabelSource);
                int depth = -1;
                do
                {
                    if (depths.TryGetValue(parents.Current.Name, out int parentDepth))
                    {
                        depth = depth < 0 || parentDepth < depth ? parentDepth + 1 : depth;
                    }
                } while (depth != 1 && parents.MoveNext());

                return depth;
            }
        }
    }
}

using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;

namespace MouseJumpSettings
{
    public class JoinList : LabelList
    {
        private List<JoinInput> inputs;

        public override event PropertyChangedEventHandler PropertyChanged;

        public JoinList(Settings settings, string name) : base(settings, name)
        { }

        public override LabelOperation Operation => LabelOperation.Join;

        public override IEnumerable<ILabelInput> Inputs
        {
            get
            {
                if (inputs == null)
                {
                    // we can't do this in the constructor because settings.LabelLists would be null
                    inputs = new();
                    int index = 0;
                    foreach (string childName in settings.GetLabelListChildren(Name))
                    {
                        inputs.Add(new JoinInput(Name, settings.LabelLists[childName], index));
                        index++;
                    }
                }

                HashSet<string> excluded = settings.GetLableListAncestors(Name);
                foreach (LabelInput input in inputs) { excluded.Add(input.AsList.Name); }
                return inputs.Concat<ILabelInput>(settings.LabelLists.Values.Where(
                    nonInput => !excluded.Contains(nonInput.Name)));
            }
        }

        public override ILabelInput AddInput(string name)
        {
            JoinInput newInput = new(Name, settings.LabelLists[name], inputs.Count);
            inputs.Add(newInput);
            foreach(JoinInput input in inputs)
            {
                input.MinIndexChanged();
            }

            return newInput;
        }

        public override void MoveInput(int oldIndex, int newIndex)
        {
            JoinInput input = inputs[oldIndex];
            inputs.RemoveAt(oldIndex);
            inputs.Insert(newIndex, input);

            for (int i = 0; i < inputs.Count; i++)
            {
                inputs[i].SetIndex(i);
            }
        }
    }
}

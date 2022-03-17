using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace MouseJumpSettings
{
    public class JoinInput : LabelInput
    {
        public override event PropertyChangedEventHandler PropertyChanged;

        private int index;
        public override int Index {
            get => -index;
            set {
                list.settings.MoveLabelListChild(parent, index, -value);
                index = -value;
            }
        }

        public JoinInput(string parent, LabelList list, int index) : base(parent, list)
        {
            this.index = index;
        }

        public void SetIndex(int newIndex)
        {
            if (newIndex != index)
            {
                index = newIndex;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Index)));
            }
        }
        public void MinIndexChanged()
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(MinIndex)));
        }
    }
}

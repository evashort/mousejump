using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using Windows.Data.Json;

namespace MouseJumpSettings
{
    internal class ChildEnumerator : IEnumerator<Views.LabelList>
    {
        private readonly Settings settings;
        private readonly string parent;
        private int index;
        private readonly IEnumerator<IJsonValue> items;
        private IEnumerator<KeyValuePair<string, IJsonValue>> pending;

        public ChildEnumerator(Settings settings, string parent, JsonValue input)
        {
            this.settings = settings;
            this.parent = parent;
            index = -1;
            if (input.ValueType == JsonValueType.Array)
            {
                items = input.GetArray().GetEnumerator();
            }
            else
            {
                items = Enumerable.Repeat(input, 1).GetEnumerator();
            }
        }

        public Views.LabelList Current { get; private set; }

        object IEnumerator.Current => Current;

        public void Dispose()
        { }

        public bool MoveNext()
        {
            if (pending != null && pending.MoveNext())
            {
                Current = new Views.LabelList(settings, pending.Current.Key, parent, index);
                return true;
            }

            while (items.MoveNext())
            {
                index++;
                if (items.Current.ValueType == JsonValueType.Object)
                {
                    pending = items.Current.GetObject().OrderByDescending(GetWeight).ThenBy(GetName).GetEnumerator();
                    if (pending.MoveNext())
                    {
                        Current = new Views.LabelList(settings, pending.Current.Key, parent, index);
                        return true;
                    }
                }
                else
                {
                    pending = null;
                    Current = new Views.LabelList(settings, items.Current.GetString(), parent, index);
                    return true;
                }
            }

            return false;
        }

        private static double GetWeight(KeyValuePair<string, IJsonValue> pair)
        {
            return pair.Value.GetNumber();
        }

        private static string GetName(KeyValuePair<string, IJsonValue> pair)
        {
            return pair.Key;
        }

        public void Reset()
        {
            throw new NotImplementedException();
        }
    }
}

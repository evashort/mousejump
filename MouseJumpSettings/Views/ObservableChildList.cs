using System;
using System.Collections;
using System.Collections.Specialized;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace MouseJumpSettings.Views
{
    internal class ObservableChildList : IList, INotifyCollectionChanged
    {
        private readonly Settings settings;
        private readonly string parent;

        public object this[int index] {
            get => settings.GetChildAtIndex(parent, index);
            set => throw new InvalidOperationException();
        }

        public bool IsFixedSize => false;

        public bool IsReadOnly => true;

        public int Count => settings.CountChildren(parent);

        public bool IsSynchronized => false;

        public object SyncRoot => null;

        public event NotifyCollectionChangedEventHandler CollectionChanged;

        public ObservableChildList(Settings settings, string parent)
        {
            this.settings = settings;
            this.parent = parent;
        }

        public int Add(object value)
        {
            throw new InvalidOperationException();
        }

        public void Clear()
        {
            throw new InvalidOperationException();
        }

        public bool Contains(object value)
        {
            throw new NotImplementedException();
        }

        public void CopyTo(Array array, int index)
        {
            foreach (object item in this)
            {
                array.SetValue(item, index);
                index++;
            }
        }

        public IEnumerator GetEnumerator()
        {
            return (IEnumerator)settings.EnumerateChildren(parent);
        }

        public int IndexOf(object value)
        {
            throw new NotImplementedException();
        }

        public void Insert(int index, object value)
        {
            throw new InvalidOperationException();
        }

        public void Remove(object value)
        {
            throw new InvalidOperationException();
        }

        public void RemoveAt(int index)
        {
            throw new InvalidOperationException();
        }
    }
}

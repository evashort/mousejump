using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.Specialized;
using Windows.Foundation.Collections;

namespace MouseJumpSettings.Views
{
    public class ObservableSortedList<T> : IList, IList<T>, INotifyCollectionChanged
    {
        private readonly IComparer<T> comparer;
        private readonly List<T> list;

        public ObservableSortedList(IComparer<T> comparer)
        {
            if (comparer == null)
            {
                throw new ArgumentNullException(nameof(comparer));
            }

            this.comparer = comparer;
            list = new();
        }

        public event NotifyCollectionChangedEventHandler CollectionChanged;

        public int Count => list.Count;

        public bool IsReadOnly => false;

        public bool IsFixedSize => ((IList)list).IsFixedSize;

        public bool IsSynchronized => ((ICollection)list).IsSynchronized;

        public object SyncRoot => ((ICollection)list).SyncRoot;

        object IList.this[int index] { get => ((IList)list)[index]; set => throw new NotImplementedException(); }
        public T this[int index] { get => list[index]; set => throw new NotImplementedException(); }

        public int ChangedAt(int index)
        {
            T item = list[index];
            if (
                (index > 0 && comparer.Compare(list[index - 1], item) >= 0)
                    || (index < Count - 1 && comparer.Compare(list[index + 1], item) < 0))
            {
                // https://github.com/microsoft/microsoft-ui-xaml/issues/3119
                RemoveAt(index);
                return AddAndGetIndex(item);
            }

            return index;
        }

        public int Changed(T item)
        {
            int index = list.IndexOf(item);
            return index < 0 ? index : ChangedAt(index);
        }

        public int IndexOf(T item)
        {
            int index = BinarySearchLeft(item);
            return index < 0 ? -1 : index;
        }

        public void AddRange(IEnumerable<T> collection)
        {
            list.AddRange(collection);
            list.Sort(comparer);
            CollectionChanged?.Invoke(
                this,
                new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
        }

        public void RemoveAt(int index)
        {
            T item = this[index];
            list.RemoveAt(index);
            CollectionChanged?.Invoke(
                this,
                new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Remove, item, index));
        }

        public void Add(T item)
        {
            AddAndGetIndex(item);
        }

        public int AddAndGetIndex(T item)
        {
            int index = BinarySearchLeft(item);
            index = index < 0 ? ~index : index;
            list.Insert(index, item);
            CollectionChanged?.Invoke(
                this,
                new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Add, item, index));
            return index;
        }

        public void Clear()
        {
            list.Clear();
            CollectionChanged?.Invoke(
                this,
                new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
        }

        public bool Contains(T item)
        {
            return BinarySearch(item) >= 0;
        }

        public void CopyTo(T[] array, int arrayIndex)
        {
            list.CopyTo(array, arrayIndex);
        }

        public bool Remove(T item)
        {
            int index = BinarySearchLeft(item);
            if (index < 0)
            {
                return false;
            }

            RemoveAt(index);
            return true;
        }

        public IEnumerator<T> GetEnumerator()
        {
            return list.GetEnumerator();
        }

        IEnumerator IEnumerable.GetEnumerator()
        {
            return list.GetEnumerator();
        }

        public int BinarySearchLeft(T item)
        {
            int index = BinarySearch(item);
            while (index > 0 && comparer.Compare(list[index - 1], item) >= 0)
            {
                index--;
            }

            return index;
        }

        public int BinarySearch(T item)
        {
            return list.BinarySearch(item, comparer);
        }

        public void Insert(int index, T item)
        {
            throw new NotImplementedException();
        }

        public int Add(object value)
        {
            throw new NotImplementedException();
        }

        public bool Contains(object value)
        {
            throw new NotImplementedException();
        }

        public int IndexOf(object value)
        {
            throw new NotImplementedException();
        }

        public void Insert(int index, object value)
        {
            throw new NotImplementedException();
        }

        public void Remove(object value)
        {
            throw new NotImplementedException();
        }

        public void CopyTo(Array array, int index)
        {
            ((ICollection)list).CopyTo(array, index);
        }
    }
}

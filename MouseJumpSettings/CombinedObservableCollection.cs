using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Linq;

namespace MouseJumpSettings
{
    public class CombinedObservableCollection<T> : INotifyCollectionChanged, INotifyPropertyChanged, ICollection<T>, IEnumerable<T>, IList<T>, IReadOnlyCollection<T>, IReadOnlyList<T>, IList
    {
        public ReadOnlyCollection<ObservableCollection<T>> Sections { get; private set; }
        public CombinedObservableCollection(IEnumerable<ObservableCollection<T>> sections)
        {
            Sections = sections.ToList().AsReadOnly();
            foreach (ObservableCollection<T> section in Sections)
            {
                section.CollectionChanged += Section_CollectionChanged;
                ((INotifyPropertyChanged)section).PropertyChanged += Section_PropertyChanged;
            }
        }

        public int Count => FlattenIndex(Sections.Count, 0);
        public T this[int index]
        {
            get
            {
                int innerIndex = index;
                foreach (ObservableCollection<T> section in Sections)
                {
                    if (innerIndex < section.Count)
                    {
                        return section[innerIndex];
                    }

                    innerIndex -= section.Count;
                }

                throw new ArgumentOutOfRangeException(nameof(index), index, "greater than Count");
            }
            set => throw new NotSupportedException();
        }

        public bool Contains(T value) => Sections.Select(section => section.Contains(value)).Any();
        public void CopyTo(T[] array, int arrayIndex)
        {
            foreach (ICollection<T> section in Sections)
            {
                section.CopyTo(array, arrayIndex);
                arrayIndex += section.Count;
            }
        }

        public IEnumerator<T> GetEnumerator() => Sections.SelectMany(section => section).GetEnumerator();
        public int IndexOf(T item)
        {
            int index = 0;
            foreach (IList<T> section in Sections)
            {
                int innerIndex = section.IndexOf(item);
                if (innerIndex >= 0)
                {
                    return index + innerIndex;
                }

                index += section.Count;
            }

            return -1;
        }

        public event NotifyCollectionChangedEventHandler CollectionChanged;
        public event PropertyChangedEventHandler PropertyChanged;
        public void CopyTo(Array array, int index)
        {
            foreach (ICollection section in Sections)
            {
                section.CopyTo(array, index);
                index += section.Count;
            }
        }

        public bool IsSynchronized => false;
        public object SyncRoot => this;
        public void Add(T item) => throw new NotSupportedException();
        public void Clear() => throw new NotSupportedException();
        public bool IsReadOnly => true;
        public bool Remove(T value) => throw new NotSupportedException();
        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
        public int Add(object value) => throw new NotSupportedException();
        public bool Contains(object value) => Sections.Select(section => ((IList)section).Contains(value)).Any();
        public int IndexOf(object value)
        {
            int index = 0;
            foreach (IList section in Sections)
            {
                int innerIndex = section.IndexOf(value);
                if (innerIndex >= 0)
                {
                    return index + innerIndex;
                }

                index += section.Count;
            }

            return -1;
        }

        public void Insert(int index, object value) => throw new NotSupportedException();
        public bool IsFixedSize => false;
        object IList.this[int index]
        {
            get => this[index];
            set => throw new NotSupportedException();
        }

        public void Remove(object value) => throw new NotSupportedException();
        public void RemoveAt(int index) => throw new NotSupportedException();
        public void Insert(int index, T item) => throw new NotSupportedException();

        public void MoveBetweenSections(ObservableCollection<T> oldSection, int oldInnerIndex, ObservableCollection<T> newSection, int newInnerIndex)
        {
            int oldOuterIndex = Sections.IndexOf(oldSection);
            if (oldOuterIndex < 0)
            {
                throw new ArgumentException("section not found", nameof(oldSection));
            }

            int newOuterIndex = Sections.IndexOf(newSection);
            if (newOuterIndex < 0)
            {
                throw new ArgumentException("section not found", nameof(newSection));
            }

            int oldIndex = FlattenIndex(oldOuterIndex, oldInnerIndex);
            T value = oldSection[oldInnerIndex];

            oldSection.CollectionChanged -= Section_CollectionChanged;
            ((INotifyPropertyChanged)oldSection).PropertyChanged -= Section_PropertyChanged;
            oldSection.RemoveAt(oldInnerIndex);
            oldSection.CollectionChanged += Section_CollectionChanged;
            ((INotifyPropertyChanged)oldSection).PropertyChanged += Section_PropertyChanged;

            newSection.CollectionChanged -= Section_CollectionChanged;
            ((INotifyPropertyChanged)newSection).PropertyChanged -= Section_PropertyChanged;
            newSection.Insert(newInnerIndex, value);
            newSection.CollectionChanged += Section_CollectionChanged;
            ((INotifyPropertyChanged)newSection).PropertyChanged += Section_PropertyChanged;

            int newIndex = FlattenIndex(newOuterIndex, newInnerIndex);
            CollectionChanged?.Invoke(
                this,
                new NotifyCollectionChangedEventArgs(
                    NotifyCollectionChangedAction.Move,
                    value,
                    newIndex,
                    oldIndex));
        }

        private void Section_CollectionChanged(object sender, NotifyCollectionChangedEventArgs e)
        {
            if (!(sender is ObservableCollection<T> section))
            {
                return;
            }

            int outerIndex = Sections.IndexOf(section);
            if (outerIndex < 0)
            {
                return;
            }

            int offset = FlattenIndex(outerIndex, 0);
            int newStartingIndex = e.NewStartingIndex + (e.NewStartingIndex < 0 ? 0 : offset);
            int oldStartingIndex = e.OldStartingIndex + (e.OldStartingIndex < 0 ? 0 : offset);
            NotifyCollectionChangedEventArgs newArgs;
            int newCount = e.NewItems?.Count ?? 0;
            int oldCount = e.OldItems?.Count ?? 0;
            if (newCount > 1 || oldCount > 1) {
                newArgs = e.Action switch
                {
                    NotifyCollectionChangedAction.Add or NotifyCollectionChangedAction.Reset
                        => new(e.Action, e.NewItems, newStartingIndex),
                    NotifyCollectionChangedAction.Move
                        => new(e.Action, e.OldItems, newStartingIndex, oldStartingIndex),
                    NotifyCollectionChangedAction.Remove
                        => new(e.Action, e.OldItems, oldStartingIndex),
                    NotifyCollectionChangedAction.Replace
                        => new(e.Action, e.NewItems, e.OldItems, oldStartingIndex),
                    _ => throw new ArgumentException($"unknown collection changed action {e.Action}", nameof(e.Action)),
                };
            }
            else
            {
                object newItem = newCount == 1 ? e.NewItems[0] : null;
                object oldItem = oldCount == 1 ? e.OldItems[0] : null;
                newArgs = e.Action switch
                {
                    NotifyCollectionChangedAction.Add or NotifyCollectionChangedAction.Reset
                        => new(e.Action, newItem, newStartingIndex),
                    NotifyCollectionChangedAction.Move
                        => new(e.Action, oldItem, newStartingIndex, oldStartingIndex),
                    NotifyCollectionChangedAction.Remove
                        => new(e.Action, oldItem, oldStartingIndex),
                    NotifyCollectionChangedAction.Replace
                        => new(e.Action, newItem, oldItem, oldStartingIndex),
                    _ => throw new ArgumentException($"unknown collection changed action {e.Action}", nameof(e.Action)),
                };
            }

            if (newArgs != null)
            {
                CollectionChanged?.Invoke(this, newArgs);
            }
        }

        private void Section_PropertyChanged(object sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(Count))
            {
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(e.PropertyName));
            }
        }

        private int FlattenIndex(int outer, int inner)
        {
            while (outer > 0)
            {
                outer--;
                inner += Sections[outer].Count;
            }

            return inner;
        }
    }
}

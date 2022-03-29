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
        public ReadOnlyCollection<ReadOnlyObservableCollection<T>> Sections { get; private set; }
        public CombinedObservableCollection(IEnumerable<ReadOnlyObservableCollection<T>> sections) {
            Sections = sections.ToList();
            foreach (ReadOnlyObservableCollection<T> section in Sections)
            {
                section.CollectionChanged += Section_CollectionChanged;
                section.PropertyChanged += Section_PropertyChanged;
            }
        }

        public int Count => FlattenIndex(Sections.Count, 0);
        public T this[int index]
        {
            get
            {
                int innerIndex = index;
                foreach (ReadOnlyObservableCollection<T> section in Sections)
                {
                    if (innerIndex < section.Count)
                    {
                        return section[innerIndex];
                    }

                    innerIndex -= section.Count;
                }

                throw new ArgumentOutOfRangeException(nameof(index), index, "greater than Count");
            }
        }

        public IList<T> Items => this;
        bool Contains(T value) => Sections.Select(section => section.Contains(value)).Any();
        void CopyTo(T[] array, int index)
        {
            foreach (ICollection<T> section in Sections)
            {
                section.CopyTo(array, index);
                index += section.Count;
            }
        }

        public IEnumerator<T> GetEnumerator() => Sections.SelectMany(section => section).GetEnumerator();
        public IndexOf(T value)
        {
            int index = 0;
            foreach (IList<T> section in Sections) {
                int innerIndex = section.IndexOf(value);
                if (innerIndex >= 0)
                {
                    return index + innerIndex;
                }

                index += section.Count;
            }

            return -1;
        }

        protected virtual void OnCollectionChanged(NotifyCollectionChangedEventArgs args) => CollectionChanged?.Invoke(this, args);
        protected virtual void OnPropertyChanged(PropertyChangedEventArgs args) => PropertyChanged?.Invoke(this, args);
        protected virtual event NotifyCollectionChangedEventHandler? CollectionChanged;
        protected virtual event PropertyChangedEventHandler? PropertyChanged;
        public void ICollection.CopyTo(Array array, int index)
        {
            foreach (ICollection section in Sections)
            {
                section.CopyTo(array, index);
                index += section.Count;
            }
        }

        public bool ICollection.IsSynchronized => false;
        public object ICollection.SyncRoot => this;
        public void ICollection<T>.Add(T value) => throw new NotSupportedException();
        public void ICollection<T>.Clear() => throw new NotSupportedException();
        public bool ICollection<T>.IsReadOnly => true;
        public bool ICollection<T>.Remove(T value) => throw new NotSupportedException();
        public IEnumerator IEnumerable.GetEnumerator() => (IEnumerator)GetEnumerator();
        public int IList.Add(object value) => throw new NotSupportedException();
        public void IList.Clear() => throw new NotSupportedException();
        public bool IList.Contains(object value) => Sections.Select(section => section.Contains(value)).Any();
        public int IList.IndexOf(object value)
        {
            int index = 0;
            foreach (IList section in Sections) {
                int innerIndex = section.IndexOf(value);
                if (innerIndex >= 0)
                {
                    return index + innerIndex;
                }

                index += section.Count;
            }

            return -1;
        }

        public int IList.Insert(int index, object value) => throw new NotSupportedException();

        public bool IList.IsFixedSize => false;
        public bool IList.IsReadOnly => true;
        public object? IList.this[int index]
        {
            get => this[index];
            set => throw new NotSupportedException();
        }

        public void IList.Remove(object value) => throw new NotSupportedException();
        public void IList.RemoveAt(int index) => throw new NotSupportedException();
        public void IList<T>.Insert(int index, object value) => throw new NotSupportedException();
        public T IList<T>.this[int index]
        {
            get => this[index];
            set => throw new NotSupportedException();
        }

        public void IList<T>.RemoveAt(int index) => throw new NotSupportedException();

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
            oldSection.PropertyChanged -= Section_PropertyChanged;
            oldSection.RemoveAt(oldInnerIndex);
            oldSection.CollectionChanged += Section_CollectionChanged;
            oldSection.PropertyChanged += Section_PropertyChanged;

            newSection.CollectionChanged -= Section_CollectionChanged;
            newSection.PropertyChanged -= Section_PropertyChanged;
            newSection.Insert(newInnerIndex, value);
            newSection.CollectionChanged += Section_CollectionChanged;
            newSection.PropertyChanged += Section_PropertyChanged;

            int newIndex = FlattenIndex(newOuterIndex, newInnerIndex);
            OnCollectionChanged(
                new NotifyCollectionChangedEventArgs(
                    NotifyCollectionChangedAction.Move,
                    value,
                    newIndex,
                    oldIndex));
            OnPropertyChanged(new PropertyChangedEventArgs(this, nameof(Items)));
        }

        private void Section_CollectionChanged(object sender, NotifyCollectionChangedEventArgs e)
        {
            if (!(sender is ReadOnlyObservableCollection<T> section))
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
            if (e.NewItems?.Count ?? 0 > 1 || e.NewItems?.Count ?? 0 > 1) {
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
                };
            }
            else
            {
                newArgs = e.Action switch
                {
                    NotifyCollectionChangedAction.Add or NotifyCollectionChangedAction.Reset
                        => new(e.Action, e.NewItems?.FirstOrDefault(), newStartingIndex),
                    NotifyCollectionChangedAction.Move
                        => new(e.Action, e.OldItems?.FirstOrDefault(), newStartingIndex, oldStartingIndex),
                    NotifyCollectionChangedAction.Remove
                        => new(e.Action, e.OldItems?.FirstOrDefault(), oldStartingIndex),
                    NotifyCollectionChangedAction.Replace
                        => new(e.Action, e.NewItems?.FirstOrDefault(), e.OldItems?.FirstOrDefault(), oldStartingIndex),
                };
            }

            if (newArgs != null)
            {
                OnCollectionChanged(newArgs);
            }
        }

        private void Section_PropertyChanged(object sender, PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(Count) || e.PropertyName == nameof(Items))
            {
                OnPropertyChanged(new PropertyChangedEventArgs(e.PropertyName));
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

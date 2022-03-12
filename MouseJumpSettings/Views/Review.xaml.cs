using Microsoft.UI.Xaml.Controls;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using Windows.Globalization.NumberFormatting;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace MouseJumpSettings.Views
{
    /// <summary>
    /// An empty page that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class Review : Page
    {
        public Review()
        {
            this.InitializeComponent();
            indexBox.NumberFormatter = new NegativeIntFormatter();
        }

        public int Index { get; set; }

        public List<TestDataType> TestData = new()
        {
            new() { Group = "group1", Name = "abc" },
            new() { Group = "group1", Name = "def" },
            new() { Group = "group2", Name = "ghi" },
        };

        //public IOrderedEnumerable<IGrouping<string, TestDataType>> GroupedTestData
        //{
        //    get
        //    {
        //        return from item in TestData
        //               group item by item.Group into grp
        //               orderby grp.Key
        //               select grp;
        //    }
        //}

        public IOrderedEnumerable<IGrouping<string, TestDataType>> GroupedTestData = new List<IGrouping<string, TestDataType>>
        {
            new Grouping<string, TestDataType>() {
                Key = "group1",
                Values = new List<TestDataType>() {
                    new() { Name = "abc"},
                    new() { Name = "def"},
                },
            },
            new Grouping<string, TestDataType>() {
                Key = "group2",
                Values = new List<TestDataType>() {
                    new() { Name = "ghi"},
                },
            },
        }.OrderBy(group => group.Key);
    }

    public class TestDataType
    {
        public string Group { get; set; }
        public string Name { get; set; }
    }

    public class Grouping<T1, T2> : IGrouping<T1, T2>
    {
        public T1 Key { get; set; }
        public IEnumerable<T2> Values { get; set; }

        public IEnumerator<T2> GetEnumerator()
        {
            return Values.GetEnumerator();
        }

        IEnumerator IEnumerable.GetEnumerator()
        {
            return GetEnumerator();
        }
    }

    public class NegativeIntFormatter : INumberFormatter2, INumberParser
    {
        private readonly DecimalFormatter formatter;
        public NegativeIntFormatter()
        {
            formatter = new DecimalFormatter
            {
                FractionDigits = 0
            };
        }

        public string FormatDouble(double value)
        {
            return formatter.FormatDouble(-value);
        }

        public string FormatInt(long value)
        {
            return formatter.FormatInt(-value);
        }

        public string FormatUInt(ulong value)
        {
            throw new System.NotImplementedException();
        }

        public double? ParseDouble(string text)
        {
            double? result = formatter.ParseDouble(text);
            if (result is double value)
            {
                return -value;
            }

            return result;
        }

        public long? ParseInt(string text)
        {
            long? result = formatter.ParseInt(text);
            if (result is long value)
            {
                return value == long.MinValue ? null : -value;
            }

            return result;
        }

        public ulong? ParseUInt(string text)
        {
            throw new System.NotImplementedException();
        }
    }
}

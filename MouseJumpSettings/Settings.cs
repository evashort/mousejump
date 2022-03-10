using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Windows.Data.Json;
using Windows.UI;

namespace MouseJumpSettings
{
    public class Settings : IDisposable
    {
        private JsonObject json;
        private readonly string path;
        private Task saveTask;
        private bool savePending;
        public Color LabelColor
        {
            get
            {
                return ParseColor(json.GetNamedString("labelColor"));
            }
            set
            {
                if (value != LabelColor)
                {
                    lock (this)
                    {
                        json.SetNamedValue("labelColor", JsonValue.CreateStringValue(FormatColor(value)));
                        Save();
                    }
                }
            }
        }

        private double defaultFontSize = 0;
        public double FontSize
        {
            get
            {
                try
                {
                    return json.GetNamedNumber("fontSize");
                }
                catch (COMException)
                {
                    return defaultFontSize;
                }
            }
            set
            {
                if (value != FontSize)
                {
                    lock (this)
                    {
                        json.SetNamedValue("fontSize", JsonValue.CreateNumberValue(value));
                        Save();
                    }
                }
            }
        }

        private string defaultFont = "";
        public string Font
        {
            get
            {
                try
                {
                    return json.GetNamedString("font");
                }
                catch (COMException)
                {
                    return defaultFont;
                }
            }
            set
            {
                if (value != Font)
                {
                    lock (this)
                    {
                        json.SetNamedValue("font", JsonValue.CreateStringValue(value));
                        Save();
                    }
                }
            }
        }

        private JsonObject defaultDefinitions = new JsonObject();
        private JsonObject Definitions
        {
            get
            {
                try
                {
                    return json.GetNamedObject("labelLists");
                }
                catch (COMException)
                {
                    return defaultDefinitions;
                }
            }
        }

        public Views.LabelOperation GetOperation(string name)
        {
            if (name == null)
            {
                return Views.LabelOperation.New;
            }

            JsonValue definition = Definitions.GetNamedValue(name);
            if (definition.ValueType == JsonValueType.Array)
            {
                return Views.LabelOperation.Basic;
            }

            return definition.GetObject().GetNamedString("operation") switch
            {
                "split" => Views.LabelOperation.Basic,
                "edit" => Views.LabelOperation.Edit,
                "merge" => Views.LabelOperation.Merge,
                "join" => Views.LabelOperation.Join,
                _ => Views.LabelOperation.New,
            };
        }

        public double? GetWeight(string name, string parent, int index)
        {
            if (GetOperation(parent) != Views.LabelOperation.Merge)
            {
                return null;
            }

            IJsonValue group = Definitions.GetNamedObject(parent).GetNamedValue("input");
            if (group.ValueType == JsonValueType.Array)
            {
                group = group.GetArray()[index];
            }

            if (group.ValueType == JsonValueType.Object)
            {
                return group.GetObject().GetNamedNumber(name);
            }

            return 1;
        }

        private Views.ObservableSortedList<Views.LabelList> rootChildren;
        private Dictionary<string, Views.ObservableSortedList<Views.LabelList>> childrenCache = new();
        public Views.ObservableSortedList<Views.LabelList> GetChildren(string parent)
        {
            if (parent == null)
            {
                if (rootChildren == null)
                {
                    HashSet<string> resultKeys = new(Definitions.Keys);
                    foreach (string parent2 in Definitions.Keys)
                    {
                        foreach (Views.LabelList child in GetChildren(parent2))
                        {
                            resultKeys.Remove(child.Name);
                        }
                    }

                    rootChildren = new(new Views.LabelListComparer());
                    rootChildren.AddRange(
                        from name in resultKeys
                        select new Views.LabelList(this, name, null));
                }

                return rootChildren;
            }

            if (childrenCache.TryGetValue(parent, out Views.ObservableSortedList<Views.LabelList> result))
            {
                return result;
            }

            Views.LabelOperation operation = GetOperation(parent);
            if (operation == Views.LabelOperation.Merge || operation == Views.LabelOperation.Join)
            {
                JsonValue input = Definitions.GetNamedObject(parent).GetNamedValue("input");
                JsonArray groups = input.ValueType == JsonValueType.Array
                    ? input.GetArray()
                    : new() { input };
                result = new(new Views.LabelListComparer());
                for (int i = 0; i < groups.Count; i++)
                {
                    IJsonValue group = groups[i];
                    if (group.ValueType == JsonValueType.String)
                    {
                        result.Add(new(this, group.GetString(), parent, i));
                    }
                    else
                    {
                        IOrderedEnumerable<KeyValuePair<string, IJsonValue>> sortedPairs
                            = group.GetObject().OrderBy(pair => -pair.Value.GetNumber()).ThenBy(pair => pair.Key);
                        result.AddRange(
                            from pair in sortedPairs
                            select new Views.LabelList(this, pair.Key, parent, i));
                    }
                }
            }
            else if (operation == Views.LabelOperation.Edit)
            {
                string name = Definitions.GetNamedObject(parent).GetNamedString("input");
                result = new(new Views.LabelListComparer()) { new(this, name, parent) };
            }
            else
            {
                result = new(new Views.LabelListComparer());
            }

            childrenCache[parent] = result;
            return result;
        }

        public bool RemoveChildAt(string name, string parent, int index, out double weight)
        {
            JsonObject definition = Definitions.GetNamedObject(parent);
            JsonValue groupsValue = definition.GetNamedValue("input");
            if (groupsValue.ValueType == JsonValueType.Object)
            {
                if (index != 0)
                {
                    throw new ArgumentOutOfRangeException(nameof(index), index, null);
                }

                JsonObject onlyGroup = groupsValue.GetObject();
                weight = onlyGroup.GetNamedNumber(name);
                onlyGroup.Remove(name);
                return onlyGroup.Count <= 0;
            }

            JsonArray groups = groupsValue.GetArray();
            IJsonValue groupValue = groups[index];
            if (groupValue.ValueType == JsonValueType.String)
            {
                weight = 1;
                groups.RemoveAt(index);
                if (groups.Count <= 1 && definition.GetNamedString("operation") == "merge")
                {
                    definition["input"] = CastMergeGroupToObject(groups[0]);
                }

                return true;
            }

            JsonObject group = groupValue.GetObject();
            weight = group.GetNamedNumber(name);
            if (group.Count <= 1)
            {
                groups.RemoveAt(index);
                if (groups.Count <= 1 && definition.GetNamedString("operation") == "merge")
                {
                    definition["input"] = CastMergeGroupToObject(groups[0]);
                }

                return true;
            }

            group.Remove(name);
            if (group.Count <= 1 && group.Values.First().GetNumber() == 1)
            {
                groups[index] = JsonValue.CreateStringValue(group.Keys.First());
            }

            return false;
        }

        private static JsonObject CastMergeGroupToObject(IJsonValue group)
        {
            if (group.ValueType == JsonValueType.Object)
            {
                return group.GetObject();
            }

            JsonObject result = new();
            result[group.GetString()] = JsonValue.CreateNumberValue(1);
            return result;
        }

        public void InsertChild(string name, string parent, int index, double weight = 1)
        {
            JsonObject definition = Definitions.GetNamedObject(parent);
            JsonValue groupsValue = definition.GetNamedValue("input");
            JsonArray groups;
            if (groupsValue.ValueType == JsonValueType.Array)
            {
                groups = groupsValue.GetArray();
            }
            else
            {
                JsonObject group = groupsValue.GetObject();
                groups = new();
                if (group.Count > 0)
                {
                    groups.Add(group);
                }

                definition["input"] = groups;
            }

            if (weight == 1)
            {
                groups.Insert(index, JsonValue.CreateStringValue(name));
            }
            else
            {
                JsonObject group = new();
                group[name] = JsonValue.CreateNumberValue(weight);
                groups.Insert(index, group);
            }
        }

        public void AddChildToGroup(string name, string parent, int index, double weight)
        {
            JsonObject definition = Definitions.GetNamedObject(parent);
            JsonValue groupsValue = definition.GetNamedValue("input");
            if (groupsValue.ValueType == JsonValueType.Object)
            {
                if (index != 0)
                {
                    throw new ArgumentOutOfRangeException(nameof(index), index, null);
                }

                groupsValue.GetObject()[name] = JsonValue.CreateNumberValue(weight);
                return;
            }

            JsonArray groups = groupsValue.GetArray();
            IJsonValue groupValue = groups[index];
            if (groupValue.ValueType == JsonValueType.Object)
            {
                JsonObject group = groupValue.GetObject();
                if (group.Count == 1 && weight == 1 && group.ContainsKey(name))
                {
                    groups[index] = JsonValue.CreateStringValue(name);
                }
                else
                {
                    groupValue.GetObject()[name] = JsonValue.CreateNumberValue(weight);
                }
            }
            else
            {
                string existingName = groupValue.GetString();
                if (weight != 1 || name != existingName)
                {
                    JsonObject group = new();
                    group[existingName] = JsonValue.CreateNumberValue(1);
                    group[name] = JsonValue.CreateNumberValue(weight);
                    groups[index] = group;
                }
            }
        }

        public bool IsSingletonGroup(string parent, int index)
        {
            IJsonValue group = Definitions.GetNamedObject(parent).GetNamedValue("input");
            if (group.ValueType == JsonValueType.Array)
            {
                group = group.GetArray()[index];
            }

            return group.ValueType == JsonValueType.String || group.GetObject().Count == 1;
        }

        public int CountGroups(string parent)
        {
            IJsonValue groups = Definitions.GetNamedObject(parent).GetNamedValue("input");
            return groups.ValueType == JsonValueType.Array ? groups.GetArray().Count : 1;
        }

        public Settings(string path)
        {
            this.path = path;
            saveTask = Task.CompletedTask;
            savePending = false;
            NONCLIENTMETRICSW metrics = new NONCLIENTMETRICSW();
            metrics.cbSize = (uint)Marshal.SizeOf(metrics);
            Win32.SystemParametersInfoW(
                0x29, // SPI_GETNONCLIENTMETRICS
                (uint)Marshal.SizeOf(metrics),
                ref metrics,
                0
            );
            defaultFontSize = -0.75 * metrics.lfMessageFont.lfHeight;
            defaultFont = metrics.lfMessageFont.lfFaceName;
        }

        public void Load()
        {
            string text = File.ReadAllText(path, Encoding.UTF8);
            json = JsonObject.Parse(text);
        }

        private static Color ParseColor(string text)
        {
            return Color.FromArgb(
                0xff,
                ParseChannel(text, 0),
                ParseChannel(text, 1),
                ParseChannel(text, 2)
            );
        }

        private static byte ParseChannel(string text, int i)
        {
            int sliceLength = (text.Length - 1) / 3;
            return (byte)(
                (sliceLength == 1 ? 0x11 : 1) * int.Parse(
                    text.AsSpan(1 + i * sliceLength, sliceLength),
                    System.Globalization.NumberStyles.HexNumber,
                    System.Globalization.NumberFormatInfo.InvariantInfo
                )
            );
        }

        private static string FormatColor(Color color)
        {
            return $"#{color.R:x2}{color.G:x2}{color.B:x2}";
        }

        private void Save()
        {
            if (savePending) { return; }
            savePending = true;
            saveTask = saveTask.ContinueWith(new Action<Task>(SaveHelp));
        }

        private void SaveHelp(Task task)
        {
            string text;
            lock (this)
            {
                text = PrettyPrint(json, 0);
                savePending = false;
            }

            File.WriteAllText(path, text + "\n");
            Thread.Sleep(100);
        }

        public static string PrettyPrint(IJsonValue val, int indentation)
        {
            int oldIndentation = indentation;
            if (val.ValueType == JsonValueType.Object)
            {
                StringBuilder result = new StringBuilder("{");
                indentation += 4;
                string separator = "\n";
                foreach (
                    KeyValuePair<string, IJsonValue> item
                        in val.GetObject().OrderBy(item => item.Key)
                )
                {
                    result.Append(separator);
                    separator = ",\n";
                    result.Append("".PadLeft(indentation));
                    result.Append(JsonValue.CreateStringValue(item.Key).ToString());
                    result.Append(": ");
                    result.Append(PrettyPrint(item.Value, indentation));
                }

                result.Append("\n");
                indentation = oldIndentation;
                result.Append("".PadLeft(indentation));
                result.Append("}");
                return result.ToString();
            }

            if (val.ValueType == JsonValueType.Array)
            {
                StringBuilder result = new StringBuilder("[");
                indentation += 4;
                bool singleLine = val.GetArray().All(
                    item => item.ValueType != JsonValueType.Array && item.ValueType != JsonValueType.Object
                );
                string separator = singleLine ? "" : "\n" + "".PadLeft(indentation);
                string nextSeparator = singleLine ? ", " : ",\n" + "".PadLeft(indentation);
                foreach (IJsonValue item in val.GetArray())
                {
                    result.Append(separator);
                    separator = nextSeparator;
                    result.Append(PrettyPrint(item, indentation));
                }

                indentation = oldIndentation;
                result.Append(singleLine ? "" : "\n" + "".PadLeft(indentation));
                result.Append("]");
                return result.ToString();
            }

            return val.Stringify();
        }

        public void Dispose()
        {
            GC.SuppressFinalize(this);
            saveTask.Dispose();
        }
    }
}

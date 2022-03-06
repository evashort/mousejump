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

        private ObservableCollection<Views.LabelList> rootChildren;
        private Dictionary<string, ObservableCollection<Views.LabelList>> childrenCache = new();
        public ObservableCollection<Views.LabelList> GetChildren(string parent)
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

                    rootChildren = new(from name in resultKeys select new Views.LabelList(this, name, null));
                }

                return rootChildren;
            }

            if (childrenCache.TryGetValue(parent, out ObservableCollection<Views.LabelList> result))
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
                result = new();
                for (int i = 0; i < groups.Count; i++)
                {
                    IJsonValue group = groups[i];
                    if (group.ValueType == JsonValueType.String)
                    {
                        result.Add(new(this, group.GetString(), parent, i));
                    }
                    else
                    {
                        foreach (string name in group.GetObject().Keys)
                        {
                            result.Add(new(this, name, parent, i));
                        }
                    }
                }
            }
            else if (operation == Views.LabelOperation.Edit)
            {
                string name = Definitions.GetNamedObject(parent).GetNamedString("input");
                result = new() { new(this, name, parent) };
            }
            else
            {
                result = new();
            }

            childrenCache[parent] = result;
            return result;
        }

        public void SetIndex(string name, string parent, int oldIndex, int newIndex)
        {
            if (newIndex == oldIndex)
            {
                return;
            }

            JsonObject definition = Definitions.GetNamedObject(parent);
            JsonValue groupsValue = definition.GetNamedValue("input");
            JsonArray groups;
            if (groupsValue.ValueType == JsonValueType.Array)
            {
                groups = groupsValue.GetArray();
            }
            else
            {
                groups = new() { groupsValue };
                definition["input"] = groups;
            }

            IJsonValue oldGroupValue = groups[oldIndex];
            if (oldGroupValue.ValueType == JsonValueType.Object)
            {
                JsonObject oldGroup = oldGroupValue.GetObject();
                if (oldGroup.Count == 1)
                {
                    groups.RemoveAt(oldIndex);
                }
                else
                {
                    oldGroupValue = new JsonObject { new(name, oldGroup.GetNamedValue(name)) };
                    oldGroup.Remove(name);
                }
            } else
            {
                groups.RemoveAt(oldIndex);
            }

            groups.Insert(newIndex, oldGroupValue);
        }

        public bool SetGroupIndex(string name, string parent, int oldIndex, int newIndex)
        {
            bool shifted = false;
            if (newIndex == oldIndex)
            {
                return shifted;
            }

            JsonArray groups = Definitions.GetNamedObject(parent).GetNamedArray("input");
            IJsonValue oldGroupValue = groups[oldIndex];
            JsonValue weight;
            if (oldGroupValue.ValueType == JsonValueType.Object)
            {
                JsonObject oldGroup = oldGroupValue.GetObject();
                weight = oldGroup.GetNamedValue(name);
                if (oldGroup.Count == 1)
                {
                    groups.RemoveAt(oldIndex);
                    shifted = true;
                }
                else
                {
                    oldGroup.Remove(name);
                }
            }
            else
            {
                weight = JsonValue.CreateNumberValue(1);
                groups.RemoveAt(oldIndex);
                shifted = true;
            }

            IJsonValue newGroupValue = groups[newIndex];
            if (newGroupValue.ValueType == JsonValueType.Object)
            {
                newGroupValue.GetObject().Add(name, weight);
            }
            else
            {
                groups[newIndex] = new JsonObject {
                    new(newGroupValue.GetString(), JsonValue.CreateNumberValue(1)),
                    new(name, weight),
                };
            }

            return shifted;
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
                        in val.GetObject().OrderBy(item=>item.Key)
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

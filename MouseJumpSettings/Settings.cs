using System;
using System.Collections.Generic;
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
        // Encoding.UTF8 adds a byte order mark so we have to make our own
        private static readonly Encoding encoding = new UTF8Encoding();
        private JsonObject json;
        private readonly string path;
        private Task saveTask;
        private bool savePending;
        private Dictionary<string, LabelList> labelLists;
        public LabelList selectedList;

        public IReadOnlyDictionary<string, LabelList> LabelLists => labelLists;

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

        private readonly double defaultFontSize = 10;
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

        private readonly string defaultFont = "Segoe UI";
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

        private readonly JsonObject defaultDefinitions = new JsonObject();
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

        private const string FieldOperation = "operation";
        private const string FieldInput = "input";
        private const string OperationSplit = "split";
        private const string OperationEdit = "edit";
        private const string OperationUnion = "union";
        private const string OperationInterleave = "interleave";
        private const string OperationJoin = "join";
        public LabelOperation GetLabelListOperation(string name)
            => Definitions.GetNamedObject(name).GetNamedString(FieldOperation) switch
            {
                OperationSplit => LabelOperation.Split,
                OperationEdit => LabelOperation.Edit,
                OperationUnion => LabelOperation.Union,
                OperationInterleave => LabelOperation.Interleave,
                OperationJoin => LabelOperation.Join,
                _ => throw new ArgumentException("unknown operation"),
            };

        public bool RenameLabelList(string oldName, string newName)
        {
            if (oldName == newName)
            {
                return true;
            }

            JsonObject definitions = Definitions;
            if (definitions.ContainsKey(newName))
            {
                return false;
            }

            lock (this)
            {
                definitions.Remove(oldName, out IJsonValue definitionValue);
                JsonObject definition = definitionValue.GetObject();
                definitions.Add(newName, definition);
                JsonValue newNameValue = JsonValue.CreateStringValue(newName);
                foreach (IJsonValue definitionValue2 in definitions.Values)
                {
                    definition = definitionValue2.GetObject();
                    switch (definition.GetNamedString(FieldOperation))
                    {
                        case OperationEdit:
                            if (definition.GetNamedString(FieldInput) == oldName)
                            {
                                definition.SetNamedValue(FieldInput, newNameValue);
                            }

                            break;
                        case OperationUnion:
                        case OperationJoin:
                            JsonArray inputs = definition.GetNamedArray(FieldInput);
                            for (int i = 0; i < inputs.Count; i++)
                            {
                                if (inputs.GetStringAt((uint)i) == oldName)
                                {
                                    inputs[i] = newNameValue;
                                }
                            }

                            break;
                        case OperationInterleave:
                            JsonObject inputWeights = definition.GetNamedObject(FieldInput);
                            if (inputWeights.Remove(oldName, out IJsonValue weightValue))
                            {
                                inputWeights.Add(newName, weightValue);
                            }

                            break;
                    }
                }

                if (LabelSource == oldName)
                {
                    json.SetNamedValue("labelSource", newNameValue);
                }

                Save();
            }

            if (labelLists.Remove(oldName, out LabelList labelList))
            {
                labelLists.Add(newName, labelList);
            }

            return true;
        }

        public IEnumerable<string> GetLabelListChildren(string parent)
        {
            JsonObject definition = Definitions.GetNamedObject(parent);
            return definition.GetNamedString(FieldOperation) switch
            {
                OperationEdit => Enumerable.Repeat(definition.GetNamedString(FieldInput), 1),
                OperationUnion or OperationJoin => from inputValue
                                                   in definition.GetNamedArray(FieldInput)
                                                   select inputValue.GetString(),
                OperationInterleave => definition.GetNamedObject(FieldInput).Keys,
                _ => Enumerable.Empty<string>(),
            };
        }

        public void SetLabelListChildren(string parent, IEnumerable<string> children)
        {
            JsonObject definition = Definitions.GetNamedObject(parent);
            lock (this) {
                switch (definition.GetNamedString(FieldOperation))
                {
                    case OperationEdit:
                        definition.SetNamedValue(FieldInput, JsonValue.CreateStringValue(children.First()));
                        break;
                    case OperationUnion or OperationJoin:
                        JsonArray inputs = definition.GetNamedArray(FieldInput);
                        inputs.Clear();
                        foreach (string child in children)
                        {
                            inputs.Add(JsonValue.CreateStringValue(child));
                        }

                        break;
                    case OperationInterleave:
                        JsonObject inputWeights = definition.GetNamedObject(FieldInput);
                        List<string> toRemove = inputWeights.Keys.Except(children).ToList();
                        foreach (string child in toRemove)
                        {
                            inputWeights.Remove(child);
                        }

                        JsonValue defaultWeightValue = JsonValue.CreateNumberValue(1);
                        foreach (string child in children)
                        {
                            inputWeights.TryAdd(child, defaultWeightValue);
                        }

                        break;
                }

                Save();
            }
        }

        public IEnumerable<string> GetLabelListParents(string child)
        {
            return from pair in Definitions
            where pair.Value.GetObject().GetNamedString(FieldOperation) switch
            {
                OperationEdit => pair.Value.GetObject().GetNamedString(FieldInput) == child,
                OperationUnion or OperationJoin
                    => pair.Value.GetObject().GetNamedArray(FieldInput).Select(input => input.GetString()).Contains(child),
                OperationInterleave => pair.Value.GetObject().GetNamedObject(FieldInput).ContainsKey(child),
                _ => false,
            }
            select pair.Key;
        }

        public Dictionary<string, int> GetLabelListDepths(string root)
        {
            Dictionary<string, int> depths = new();
            Queue<KeyValuePair<string, int>> fringe = new();
            fringe.Enqueue(new(root, 0));
            while (fringe.TryDequeue(out KeyValuePair<string, int> pair))
            {
                if (depths.TryAdd(pair.Key, pair.Value))
                {
                    foreach (string child in GetLabelListChildren(pair.Key))
                    {
                        fringe.Enqueue(new(child, pair.Value + 1));
                    }
                }
            }

            return depths;
        }

        public string LabelSource
        {
            get
            {
                try
                {
                    return json.GetNamedString("labelSource");
                }
                catch (COMException)
                {
                    return "default";
                }
            }
            set
            {
                if (value != LabelSource)
                {
                    lock (this)
                    {
                        json.SetNamedValue("labelSource", JsonValue.CreateStringValue(value));
                        Save();
                    }
                }
            }
        }

        public Settings(string path)
        {
            this.path = path;
            saveTask = Task.CompletedTask;
            savePending = false;
            NONCLIENTMETRICSW metrics = new();
            metrics.cbSize = (uint)Marshal.SizeOf(metrics);
            int hResult = Win32.SystemParametersInfoW(
                0x29, // SPI_GETNONCLIENTMETRICS
                (uint)Marshal.SizeOf(metrics),
                ref metrics,
                0
            );
            if (hResult == 1)
            {
                defaultFontSize = -0.75 * metrics.lfMessageFont.lfHeight;
                defaultFont = metrics.lfMessageFont.lfFaceName;
            }
        }

        public void Load()
        {
            string text = File.ReadAllText(path, encoding);
            json = JsonObject.Parse(text);
            labelLists = Definitions.Keys.ToDictionary(name => name, name => LabelList.Create(this, name));
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
            StringBuilder text = new();
            lock (this)
            {
                PrettyPrint(json, text, 0);
                savePending = false;
            }

            text.Append('\n');
            File.WriteAllText(path, text.ToString(), encoding);
            Thread.Sleep(100);
        }

        public static void PrettyPrint(IJsonValue val, StringBuilder result, int indentation)
        {
            int oldIndentation = indentation;
            if (val.ValueType == JsonValueType.Object)
            {
                result.Append('{');
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
                    PrettyPrint(item.Value, result, indentation);
                }

                result.Append('\n');
                indentation = oldIndentation;
                result.Append("".PadLeft(indentation));
                result.Append('}');
            }
            else if (val.ValueType == JsonValueType.Array)
            {
                result.Append('[');
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
                    PrettyPrint(item, result, indentation);
                }

                indentation = oldIndentation;
                result.Append(singleLine ? "" : "\n" + "".PadLeft(indentation));
                result.Append(']');
            }
            else
            {
                result.Append(val.Stringify());
            }
        }

        public void Dispose()
        {
            GC.SuppressFinalize(this);
            saveTask.Dispose();
        }
    }
}

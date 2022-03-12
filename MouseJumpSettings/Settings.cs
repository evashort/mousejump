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

            definitions.Remove(oldName, out IJsonValue definitionValue);
            JsonObject definition = definitionValue.GetObject();
            definitions.Add(newName, definition);
            JsonValue newNameValue = JsonValue.CreateStringValue(newName);
            foreach (IJsonValue definitionValue2 in definitions.Values) {
                definition = definitionValue2.GetObject();
                switch (definition.GetNamedString(FieldOperation)) {
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

            if (labelLists.Remove(oldName, out LabelList labelList))
            {
                labelLists.Add(newName, labelList);
            }

            return true;
        }

        public IEnumerable<string> LabelListNames => Definitions.Keys;

        private readonly Dictionary<string, LabelList> labelLists = new();
        public LabelList GetLabelList(string name)
        {
            LabelList labelList;
            if (!labelLists.TryGetValue(name, out labelList))
            {
                labelList = LabelList.Create(this, name);
                labelLists.Add(name, labelList);
            }

            return labelList;
        }

        public IEnumerable<string> GetLabelListChildren(string parent)
        {
            JsonObject definition = Definitions.GetNamedObject(parent);
            switch (definition.GetNamedString(FieldOperation))
            {
                case OperationEdit:
                    return Enumerable.Repeat(definition.GetNamedString(FieldInput), 1);
                case OperationUnion:
                case OperationJoin:
                    return from inputValue in definition.GetNamedArray(FieldInput)
                           select inputValue.GetString();
                case OperationInterleave:
                    return definition.GetNamedObject(FieldInput).Keys;
                default:
                    return Enumerable.Empty<string>();
            }
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

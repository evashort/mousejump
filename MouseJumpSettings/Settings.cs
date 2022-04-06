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
        // Encoding.UTF8 adds a byte order mark so we have to make our own
        private static readonly Encoding encoding = new UTF8Encoding();
        private JsonObject json;
        private readonly string path;
        private Task saveTask;
        private bool savePending;
        private Dictionary<string, LabelList> labelLists;
        private LabelList selectedList;
        private readonly ObservableCollection<LabelList> inputs;
        private readonly ObservableCollection<LabelList> nonInputs;
        public readonly CombinedObservableCollection<LabelList> possibleInputs;

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
        private LabelOperation OperationFromField(string field) => field switch
            {
                OperationSplit => LabelOperation.Split,
                OperationEdit => LabelOperation.Edit,
                OperationUnion => LabelOperation.Union,
                OperationInterleave => LabelOperation.Interleave,
                OperationJoin => LabelOperation.Join,
                _ => throw new ArgumentException($"unknown operation {field}"),
            };

        public LabelOperation GetLabelListOperation(string name)
            => OperationFromField(Definitions.GetNamedObject(name).GetNamedString(FieldOperation));

        public bool RenameLabelList(string oldName, string newName)
        {
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
                            JsonArray inputArray = definition.GetNamedArray(FieldInput);
                            for (int i = 0; i < inputArray.Count; i++)
                            {
                                if (inputArray.GetStringAt((uint)i) == oldName)
                                {
                                    inputArray[i] = newNameValue;
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

        public int CountLabelListChildren(string parent)
        {
            JsonObject definition = Definitions.GetNamedObject(parent);
            return definition.GetNamedString(FieldOperation) switch
            {
                OperationEdit => 1,
                OperationUnion or OperationJoin => definition.GetNamedArray(FieldInput).Count,
                OperationInterleave => definition.GetNamedObject(FieldInput).Count,
                _ => 0,
            };
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
                OperationInterleave => from pair in definition.GetNamedObject(FieldInput)
                                       orderby pair.Value.GetNumber() descending, pair.Key
                                       select pair.Key,
                _ => Enumerable.Empty<string>(),
            };
        }

        public void AddLabelListInput(LabelList input)
        {
            int oldIndex = NonInputs.IndexOf(input);
            if (oldIndex < 0)
            {
                throw new InvalidOperationException($"could not add input {input.Name} to {SelectedList.Name}");
            }

            LabelOperation operation = AddLabelListChild(SelectedList.Name, input.Name);

            int newIndex = Inputs.Count;
            if (operation == LabelOperation.Interleave)
            {
                newIndex = GetLabelListChildren(SelectedList.Name).TakeWhile(sibling => sibling != input.Name).Count();
            }

            possibleInputs.MoveBetweenSections(nonInputs, oldIndex, inputs, newIndex);
            if (operation == LabelOperation.Edit)
            {
                LabelList oldInput = Inputs[0];
                int outIndex = 0;
                foreach (LabelList nonInput in NonInputs)
                {
                    if (string.Compare(nonInput.Name, input.Name) >= 0) { break; }
                    outIndex++;
                }

                // subtract 1 because the whole array shifts when we remove
                // the old input from index zero
                possibleInputs.MoveBetweenSections(inputs, 0, nonInputs, outIndex);
                oldInput.IsInputChanged();
            }
            else
            {
                for (int i = 0; i < Inputs.Count; i++)
                {
                    if (i != newIndex)
                    {
                        Inputs[i].MinIndexChanged();
                    }
                }

                for (int i = newIndex + 1; i < inputs.Count; i++)
                {
                    Inputs[i].IndexChanged();
                }

                if (operation == LabelOperation.Join)
                {
                    nonInputs.Insert(oldIndex, new LabelList(this, input.Name));
                }
            }
        }

        private LabelOperation AddLabelListChild(string parent, string name)
        {
            JsonObject definition = Definitions.GetNamedObject(parent);
            LabelOperation operation = OperationFromField(definition.GetNamedString(FieldOperation));
            lock (this)
            {
                switch (operation)
                {
                    case LabelOperation.Edit:
                        definition.SetNamedValue(FieldInput, JsonValue.CreateStringValue(name));
                        break;
                    case LabelOperation.Join:
                    case LabelOperation.Union:
                        definition.GetNamedArray(FieldInput).Add(JsonValue.CreateStringValue(name));
                        break;
                    case LabelOperation.Interleave:
                        definition.GetNamedObject(FieldInput).Add(name, JsonValue.CreateNumberValue(1));
                        break;
                }

                Save();
            }

            return operation;
        }

        public void MoveLabelListInput(LabelList input, int newIndex)
        {
            int oldIndex = Inputs.IndexOf(input);
            if (oldIndex < 0)
            {
                throw new InvalidOperationException($"{input.Name} is not an input to {SelectedList.Name}");
            }

            if (oldIndex == newIndex)
            {
                return;
            }

            MoveLabelListChild(SelectedList.Name, oldIndex, newIndex);

            inputs.Move(oldIndex, newIndex);
            // don't include newIndex because that LabelList initiated the
            // change. it already knows its new index.
            int minIndex = oldIndex < newIndex ? oldIndex : newIndex + 1;
            int maxIndex = oldIndex > newIndex ? oldIndex : newIndex - 1;
            for (int i = minIndex; i <= maxIndex; i++)
            {
                Inputs[i].IndexChanged();
            }
        }

        private void MoveLabelListChild(string parent, int oldIndex, int newIndex)
        {
            JsonArray inputArray = Definitions.GetNamedObject(parent).GetNamedArray(FieldInput);
            IJsonValue inputValue = inputArray[oldIndex];
            lock (this)
            {
                inputArray.RemoveAt(oldIndex);
                inputArray.Insert(newIndex, inputValue);
                Save();
            }
        }

        public void RemoveLabelListInput(LabelList input)
        {
            int oldIndex = Inputs.IndexOf(input);
            if (oldIndex < 0)
            {
                throw new InvalidOperationException($"{input.Name} is not an input to {SelectedList.Name}");
            }

            LabelOperation operation = RemoveLabelListChild(SelectedList.Name, input.Name, oldIndex);

            int newIndex = 0;
            foreach (LabelList nonInput in NonInputs)
            {
                if (string.Compare(nonInput.Name, input.Name) >= 0) { break; }
                newIndex++;
            }

            possibleInputs.MoveBetweenSections(inputs, oldIndex, nonInputs, newIndex);
            for (int i = oldIndex; i < Inputs.Count; i++)
            {
                Inputs[i].IndexChanged();
            }

            foreach (LabelList sibling in Inputs)
            {
                sibling.MinIndexChanged();
            }

            if (operation == LabelOperation.Join)
            {
                nonInputs.RemoveAt(newIndex + 1);
            }
        }

        private LabelOperation RemoveLabelListChild(string parent, string name, int index)
        {
            JsonObject definition = Definitions.GetNamedObject(parent);
            LabelOperation operation = OperationFromField(definition.GetNamedString(FieldOperation));
            lock (this)
            {
                switch (operation)
                {
                    case LabelOperation.Join:
                    case LabelOperation.Union:
                        definition.GetNamedArray(FieldInput).RemoveAt(index);
                        break;
                    case LabelOperation.Interleave:
                        definition.GetNamedObject(FieldInput).Remove(name);
                        break;
                }

                Save();
            }

            return operation;
        }

        public double GetLabelListInputWeight(LabelList input)
        {
            return GetLabelListChildWeight(SelectedList.Name, input.Name);
        }

        private double GetLabelListChildWeight(string parent, string name)
        {
            JsonObject definition = Definitions.GetNamedObject(parent);
            LabelOperation operation = OperationFromField(definition.GetNamedString(FieldOperation));
            if (operation != LabelOperation.Interleave)
            {
                return 1;
            }

            return definition.GetNamedObject(FieldInput).GetNamedNumber(name);
        }

        public void SetLabelListInputWeight(LabelList input, double weight)
        {
            if (!SetLabelListChildWeight(SelectedList.Name, input.Name, weight))
            {
                return;
            }

            int oldIndex = Inputs.IndexOf(input);
            int newIndex = GetLabelListChildren(SelectedList.Name).TakeWhile(sibling => sibling != input.Name).Count();
            if (newIndex == oldIndex)
            {
                return;
            }

            int minIndex = Math.Min(oldIndex, newIndex);
            int maxIndex = Math.Max(oldIndex, newIndex);
            for (int i = minIndex; i <= maxIndex; i++)
            {
                Inputs[i].IndexChanged();
            }
        }

        private bool SetLabelListChildWeight(string parent, string name, double weight)
        {
            JsonObject inputWeights = Definitions.GetNamedObject(parent).GetNamedObject(FieldInput);
            if (inputWeights.GetNamedNumber(name) == weight)
            {
                return false;
            }

            lock (this)
            {
                inputWeights[name] = JsonValue.CreateNumberValue(weight);
                Save();
            }

            return true;
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

        public HashSet<string> GetLableListAncestors(string root)
        {
            HashSet<string> ancestors = new();
            Queue<string> fringe = new();
            fringe.Enqueue(root);
            while (fringe.TryPeek(out string child))
            {
                fringe.Dequeue();
                if (ancestors.Add(child))
                {
                    foreach (string parent in GetLabelListParents(child))
                    {
                        fringe.Enqueue(parent);
                    }
                }
            }

            return ancestors;
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

        public ObservableCollection<LabelList> Inputs => inputs;
        public ObservableCollection<LabelList> NonInputs => nonInputs;

        public LabelList SelectedList {
            get => selectedList;
            set {
                if (value == null)
                {
                    ClearInputs();
                    nonInputs.Clear();
                    selectedList = value;
                    return;
                }

                if (selectedList != null && value.Name == selectedList.Name)
                {
                    return;
                }

                ClearInputs();
                nonInputs.Clear();
                selectedList = value;
                HashSet<string> inputNames = new();
                foreach (string name in GetLabelListChildren(value.Name))
                {
                    if (inputNames.Add(name))
                    {
                        inputs.Add(LabelLists[name]);
                        LabelLists[name].IsInputChanged();
                    }
                    else
                    {
                        inputs.Add(new(this, name));
                    }
                }

                bool addAnyway = GetLabelListOperation(value.Name) == LabelOperation.Join;
                foreach (LabelList input in LabelLists.Values.OrderBy(labelList => labelList.Name))
                {
                    if (!inputNames.Contains(input.Name))
                    {
                        nonInputs.Add(input);
                    }
                    else if (addAnyway)
                    {
                        nonInputs.Add(new(this, input.Name));
                    }
                }
            }
        }

        private void ClearInputs()
        {
            IEnumerable<LabelList> oldInputs = inputs.ToArray();
            inputs.Clear();
            foreach (LabelList input in oldInputs)
            {
                input.IsInputChanged();
            }
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

            inputs = new();
            nonInputs = new();
            possibleInputs = new(new ObservableCollection<LabelList>[] { Inputs, NonInputs });
        }

        public void Load()
        {
            string text = File.ReadAllText(path, encoding);
            json = JsonObject.Parse(text);
            labelLists = Definitions.Keys.ToDictionary(name => name, name => new LabelList(this, name));
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

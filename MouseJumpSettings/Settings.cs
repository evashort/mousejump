﻿using System;
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
                    text.Substring(1 + i * sliceLength, sliceLength),
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

                indentation = val.GetArray().Count < 10 ? indentation + 4 : 0;
                string separator = "\n";
                foreach (IJsonValue item in val.GetArray())
                {
                    result.Append(separator);
                    separator = ",\n";
                    result.Append("".PadLeft(indentation));
                    result.Append(PrettyPrint(item, indentation));
                }

                result.Append("\n");
                indentation = oldIndentation;
                result.Append("".PadLeft(indentation));
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

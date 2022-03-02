using System;
using System.Collections.Generic;
using Windows.Data.Json;

namespace MouseJumpSettings
{
    public class LabelList
    {
        public JsonObject definitions;
        public string name;
        public string parentName;
        public JsonValue Json => definitions.GetNamedValue(name);
        public string Name => name;
        public LabelOperation Operation {
            get
            {
                if (name == null)
                {
                    return LabelOperation.New;
                }

                if (Json.ValueType == JsonValueType.Array) {
                    return LabelOperation.Basic;
                }

                string operationName = Json.GetObject().GetNamedValue("operator").GetString();
                switch (operationName)
                {
                    case "split":
                        return LabelOperation.Basic;
                    case "merge":
                        return LabelOperation.Merge;
                    case "join":
                        return LabelOperation.Join;
                    case "edit":
                        return LabelOperation.Edit;
                    default:
                        throw new InvalidOperationException($"unknown label operation {operationName}");
                }
            }
        }
        public string IconPath => Operation switch
        {
            LabelOperation.Basic => IconPaths.Basic,
            LabelOperation.Merge => IconPaths.Merge,
            LabelOperation.Join => IconPaths.Join,
            LabelOperation.Edit => IconPaths.Edit,
            LabelOperation.New => IconPaths.New,
            _ => throw new NotImplementedException(),
        };
    }

    public enum LabelOperation
    {
        Basic,
        Merge,
        Join,
        Edit,
        New,
    }
}

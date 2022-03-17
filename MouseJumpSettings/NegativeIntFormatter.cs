using Windows.Globalization.NumberFormatting;

namespace MouseJumpSettings
{
    public class NegativeIntFormatter : INumberFormatter2, INumberParser
    {
        public static readonly INumberFormatter2 Instance = new NegativeIntFormatter();

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

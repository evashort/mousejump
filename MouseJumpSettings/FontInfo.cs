using Windows.UI.Text;

namespace MouseJumpSettings
{
    public class FontInfo
    {
        public string Name { get; set; }
        public FontWeight Weight { get; set; }
        public override string ToString()
        {
            return Name;
        }
    }
}

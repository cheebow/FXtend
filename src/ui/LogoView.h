#pragma once

#include "Theme.h"

namespace fxui
{

/** The FXtend logo for the header: the mark (a sound dying away meets a sound
    building up, and together they draw an X) and the name beside it.
    The same drawing as assets/logo/fxtend-logo-on-dark.svg. */
class LogoView final : public juce::Component
{
    // Outlines in font units (1000 per em), y downwards, baseline at 0.
    static constexpr float capHeight = 706.0f;
    static constexpr float outlineWidth = 3234.0f;
    static constexpr const char* heavyOutline = "M67.5 0V-706H225V0ZM188 -276.5V-412H541V-276.5ZM188 -570.5V-706H557V-570.5ZM1081.5 0 884.5 -314.5H865.5L593.5 -706H780.5L975 -409.5H994L1266 0ZM581 0 842 -377.5 962.5 -310 760 0ZM999.5 -333 879 -400.5 1074.5 -706H1253.5Z";
    static constexpr const char* lightOutline = "M1370.5 0V-668H1441V0ZM1252 -406.5V-470H1559V-406.5ZM1854.5 10Q1784 10 1728 -22Q1672 -54 1639.5 -109.8Q1607 -165.5 1607 -235.5Q1607 -305 1638.8 -360.2Q1670.5 -415.5 1725.5 -447.8Q1780.5 -480 1848.5 -480Q1913 -480 1962.5 -450.8Q2012 -421.5 2040.2 -370Q2068.5 -318.5 2068.5 -252Q2068.5 -244 2067.5 -233.5Q2066.5 -223 2064 -209H1656.5V-268.5H2025.5L2000.5 -247Q2001.5 -298.5 1983 -336Q1964.5 -373.5 1930 -394.2Q1895.5 -415 1847 -415Q1796 -415 1757.8 -392.8Q1719.5 -370.5 1698.2 -331Q1677 -291.5 1677 -238Q1677 -183.5 1699.2 -142.5Q1721.5 -101.5 1762 -78.8Q1802.5 -56 1856 -56Q1898.5 -56 1934.5 -71.5Q1970.5 -87 1995.5 -117.5L2041 -71.5Q2008 -32.5 1959.2 -11.2Q1910.5 10 1854.5 10ZM2518 0V-276Q2518 -337.5 2481.2 -376Q2444.5 -414.5 2384.6 -414.5Q2343.9 -414.5 2312.7 -396.8Q2281.5 -379 2263.8 -348Q2246 -317 2246 -276.5L2214 -294.5Q2214 -348 2238.8 -389.8Q2263.5 -431.5 2306.4 -455.8Q2349.4 -480 2403.7 -480Q2458 -480 2499.8 -453.8Q2541.5 -427.5 2565 -383.8Q2588.5 -340 2588.5 -288V0ZM2175.5 0V-470H2246V0ZM2920 10Q2854.5 10 2802.5 -22.2Q2750.6 -54.5 2720.3 -109.7Q2690 -165 2690 -234.1Q2690 -304.2 2720.2 -359.6Q2750.5 -415 2802.7 -447.5Q2854.8 -480 2920 -480Q2972.5 -480 3015.8 -458.8Q3059 -437.5 3087.2 -399Q3115.5 -360.5 3121 -309V-161.5Q3115 -111 3087.5 -72.2Q3060 -33.5 3016.8 -11.8Q2973.5 10 2920 10ZM2932 -56.5Q2981 -56.5 3017.9 -79.3Q3054.9 -102.2 3075.7 -142.3Q3096.5 -182.5 3096.5 -234.5Q3096.5 -287.7 3075.4 -327.8Q3054.3 -367.9 3017.1 -390.7Q2980 -413.5 2930.8 -413.5Q2881.5 -413.5 2843.8 -390.7Q2806.1 -367.8 2784.3 -327.6Q2762.5 -287.5 2762.5 -235Q2762.5 -183 2784.2 -142.5Q2805.9 -102 2844.3 -79.3Q2882.7 -56.5 2932 -56.5ZM3163 0H3092.5V-126L3107 -241.5L3092.5 -355V-708H3163Z";

public:
    LogoView()   { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();

        // Mark: nine bars on a 154 x 100 grid.
        static constexpr float bars[9][3] = { { 0, 0, 100 }, { 18, 12, 76 }, { 36, 24, 52 }, { 54, 35, 30 }, { 72, 43, 14 },
                                              { 90, 35, 30 }, { 108, 24, 52 }, { 126, 12, 76 }, { 144, 0, 100 } };

        const float markHeight = juce::jmin (17.0f, area.getHeight());
        const float scale = markHeight / 100.0f;
        const float markWidth = 154.0f * scale;
        const auto mark = area.removeFromLeft (markWidth).withSizeKeepingCentre (markWidth, markHeight);

        juce::ColourGradient gradient (juce::Colour (0xffffa53d), mark.getX(), 0.0f, juce::Colour (0xff5eb1ff), mark.getRight(), 0.0f, false);
        gradient.addColour (0.5, juce::Colour (0xffff6b5e));
        g.setGradientFill (gradient);

        for (const auto& bar : bars)
        {
            const float width = juce::jmax (1.6f, 10.0f * scale);
            g.fillRoundedRectangle (mark.getX() + bar[0] * scale, mark.getY() + bar[1] * scale, width, bar[2] * scale, width * 0.5f);
        }

        // Name: the same outlines as the logo files ("FX" bold, "tend" light,
        // from Outfit, SIL Open Font License), so it looks the same everywhere.
        area.removeFromLeft (7.0f);
        static const juce::Path heavy = juce::DrawableImage::parseSVGPath (heavyOutline);
        static const juce::Path light = juce::DrawableImage::parseSVGPath (lightOutline);

        const float nameScale = juce::jmin (markHeight * 0.86f / capHeight, area.getWidth() / outlineWidth);
        const auto place = juce::AffineTransform::scale (nameScale)
                               .translated (area.getX(), area.getCentreY() + capHeight * nameScale * 0.5f);

        g.setColour (Colours::text);
        g.fillPath (heavy, place);
        g.fillPath (light, place);
    }
};

} // namespace fxui

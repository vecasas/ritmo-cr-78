#pragma once
#include <JuceHeader.h>

// Estética propia "1978": madera, panel negro mate, teclas crema y de color,
// mandos negros con faldón metálico y LEDs rojos. Todo se dibuja por código.
namespace vintage
{
const juce::Colour panel   (0xff1b1a18);   // negro mate del panel
const juce::Colour panel2  (0xff26241f);   // zonas en relieve
const juce::Colour cream   (0xffece2c6);   // serigrafía y teclas crema
const juce::Colour orange  (0xffe0782a);   // acento principal
const juce::Colour mustard (0xffd9a93a);
const juce::Colour brick   (0xffb5442c);
const juce::Colour olive   (0xff7d8a3a);
const juce::Colour ledOn   (0xffff3b2f);
const juce::Colour ledOff  (0xff4a1d19);

// Mayúsculas que también convierten vocales acentuadas y eñe.
inline juce::String upper (const juce::String& in)
{
    juce::String out;
    for (auto ch : in)
    {
        switch (ch)
        {
            case 0xE1: ch = 0xC1; break; case 0xE9: ch = 0xC9; break; case 0xED: ch = 0xCD; break;
            case 0xF3: ch = 0xD3; break; case 0xFA: ch = 0xDA; break; case 0xFC: ch = 0xDC; break;
            case 0xF1: ch = 0xD1; break;
            default: ch = juce::CharacterFunctions::toUpperCase (ch); break;
        }
        out += juce::String::charToString (ch);
    }
    return out;
}

// Propiedades que las teclas pueden llevar (getProperties()):
//   "keyColour" -> color del cuerpo de la tecla (int ARGB)
//   "led"       -> true para dibujar un LED encima de la tecla
inline juce::Colour keyColourOf (juce::Component& c, juce::Colour fallback)
{
    auto v = c.getProperties()["keyColour"];
    return v.isVoid() ? fallback : juce::Colour ((juce::uint32) (juce::int64) v);
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::PopupMenu::backgroundColourId, panel2);
        setColour (juce::PopupMenu::textColourId, cream);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, orange);
        setColour (juce::ComboBox::backgroundColourId, panel);
        setColour (juce::ComboBox::textColourId, cream);
        setColour (juce::ComboBox::outlineColourId, cream.withAlpha (0.3f));
        setColour (juce::ComboBox::arrowColourId, orange);
        setColour (juce::BubbleComponent::backgroundColourId, cream);
        setColour (juce::BubbleComponent::outlineColourId, panel);
        setColour (juce::TooltipWindow::backgroundColourId, cream);
        setColour (juce::TooltipWindow::textColourId, panel);
        setColour (juce::Slider::textBoxTextColourId, panel);
    }

    // Mando: faldón metálico con estrías, tapa negra y raya crema.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider& s) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float ang = start + pos * (end - start);

        // Escala de puntos
        g.setColour (cream.withAlpha (0.55f));
        for (int i = 0; i <= 10; ++i)
        {
            const float a = start + (end - start) * (float) i / 10.0f;
            const float rr = r * 0.98f;
            g.fillEllipse (c.x + std::sin (a) * rr - 1.0f, c.y - std::cos (a) * rr - 1.0f, 2.0f, 2.0f);
        }
        // Faldón
        const float skirt = r * 0.86f;
        juce::ColourGradient metal (juce::Colour (0xffd8d8d4), c.x - skirt, c.y - skirt,
                                    juce::Colour (0xff6c6c68), c.x + skirt, c.y + skirt, false);
        g.setGradientFill (metal);
        g.fillEllipse (c.x - skirt, c.y - skirt, skirt * 2, skirt * 2);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        for (int i = 0; i < 36; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 36.0f;
            g.drawLine (c.x + std::sin (a) * skirt * 0.8f, c.y - std::cos (a) * skirt * 0.8f,
                        c.x + std::sin (a) * skirt, c.y - std::cos (a) * skirt, 0.8f);
        }
        // Tapa
        const float cap = r * 0.62f;
        juce::ColourGradient capG (juce::Colour (0xff3a3835), c.x, c.y - cap, juce::Colour (0xff0d0c0b), c.x, c.y + cap, false);
        g.setGradientFill (capG);
        g.fillEllipse (c.x - cap, c.y - cap, cap * 2, cap * 2);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawEllipse (c.x - cap, c.y - cap, cap * 2, cap * 2, 1.0f);
        // Indicador
        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
        juce::Path p;
        p.addRoundedRectangle (-1.6f, -skirt, 3.2f, skirt * 0.62f, 1.4f);
        g.setColour (accent == juce::Colour() ? cream : accent);
        g.fillPath (p, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
    }

    // Fader vertical para el EQ.
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearVertical) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s); return; }
        const float cx = (float) x + (float) w * 0.5f;
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (cx - 2.5f, (float) y, 5.0f, (float) h, 2.0f);
        g.setColour (cream.withAlpha (0.4f));
        g.drawHorizontalLine (y + h / 2, cx - 9, cx + 9);
        auto cap = juce::Rectangle<float> (cx - 11.0f, pos - 6.0f, 22.0f, 12.0f);
        juce::ColourGradient cg (cream, cap.getX(), cap.getY(), cream.darker (0.5f), cap.getX(), cap.getBottom(), false);
        g.setGradientFill (cg);
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (panel);
        g.drawHorizontalLine ((int) pos, cap.getX() + 2, cap.getRight() - 2);
    }

    // Teclas: cuerpo con bisel; LED opcional encima.
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.5f);
        const bool led = b.getProperties()["led"];
        const bool on = b.getToggleState();
        if (led)
        {
            const float d = 7.0f;
            auto l = juce::Rectangle<float> (r.getCentreX() - d * 0.5f, r.getY(), d, d);
            g.setColour (on ? ledOn : ledOff);
            g.fillEllipse (l);
            if (on)
            {
                g.setColour (ledOn.withAlpha (0.35f));
                g.fillEllipse (l.expanded (3.0f));
            }
            r.removeFromTop (d + 3.0f);
        }
        auto body = keyColourOf (b, cream);
        if (! led && on) body = body.brighter (0.25f);
        if (down) r = r.translated (0, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (r.translated (0, 2.0f), 3.0f);
        juce::ColourGradient grad (body.brighter (over ? 0.15f : 0.05f), r.getX(), r.getY(),
                                   body.darker (0.35f), r.getX(), r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 3, r.getRight() - 3);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        auto r = b.getLocalBounds().reduced (3);
        if (b.getProperties()["led"]) r.removeFromTop (10);
        const auto body = keyColourOf (b, cream);
        g.setColour (body.getPerceivedBrightness() > 0.55f ? panel : cream);
        g.setFont (juce::FontOptions (juce::jmin (12.0f, (float) r.getHeight() * 0.7f), juce::Font::bold));
        g.drawFittedText (upper (b.getButtonText()), r, juce::Justification::centred, 2, 0.8f);
    }

    // Interruptores pequeños (Mute, FX, Sync...): LED cuadrado + texto.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        auto led = r.removeFromLeft (14.0f).withSizeKeepingCentre (9.0f, 9.0f);
        const auto onCol = b.findColour (juce::ToggleButton::tickColourId) == juce::Colour() ? ledOn
                                                                                            : b.findColour (juce::ToggleButton::tickColourId);
        g.setColour (b.getToggleState() ? onCol : ledOff);
        g.fillRoundedRectangle (led, 1.5f);
        if (b.getToggleState()) { g.setColour (onCol.withAlpha (0.3f)); g.fillRoundedRectangle (led.expanded (2.5f), 3.0f); }
        g.setColour (cream.withAlpha (over ? 1.0f : 0.8f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (upper (b.getButtonText()), r.withTrimmedLeft (3.0f), juce::Justification::centredLeft);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (12.0f, juce::Font::bold)); }
};

// Madera de los laterales, dibujada con vetas procedurales.
inline void drawWood (juce::Graphics& g, juce::Rectangle<int> area, int seed)
{
    juce::ColourGradient base (juce::Colour (0xff6b3e22), (float) area.getX(), 0.0f,
                               juce::Colour (0xff4a2914), (float) area.getRight(), 0.0f, false);
    base.addColour (0.5, juce::Colour (0xff7a4a2a));
    g.setGradientFill (base);
    g.fillRect (area);
    juce::Random rnd (seed);
    for (int i = 0; i < 26; ++i)
    {
        const float x0 = (float) area.getX() + rnd.nextFloat() * (float) area.getWidth();
        juce::Path p;
        p.startNewSubPath (x0, (float) area.getY());
        for (int y = area.getY(); y <= area.getBottom(); y += 24)
            p.lineTo (x0 + std::sin ((float) y * 0.013f + (float) i) * 2.5f + rnd.nextFloat() * 0.8f, (float) y);
        g.setColour (juce::Colour (0xff2e170a).withAlpha (0.12f + rnd.nextFloat() * 0.18f));
        g.strokePath (p, juce::PathStrokeType (0.6f + rnd.nextFloat() * 1.2f));
    }
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRect (area, 1);
}

// Rótulo de sección con línea, estilo serigrafía.
inline void drawSectionLabel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text)
{
    g.setColour (cream.withAlpha (0.85f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 12;
    g.drawText (text, r.getX(), r.getY(), tw, r.getHeight(), juce::Justification::centredLeft);
    g.setColour (cream.withAlpha (0.25f));
    g.fillRect (r.getX() + tw, r.getCentreY(), r.getWidth() - tw, 1);
}
} // namespace vintage

#include "xap_slider.h"

void XapSlider::paintKnob(juce::Graphics &g)
{
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    float texth = 25.0;
    if (!m_mousedown)
    {
        g.drawFittedText(m_pardesc.name, 0, 0, getWidth(), texth, juce::Justification::centred, 2);
        // g.drawText(m_pardesc.name, 0, 0, getWidth(), 20, juce::Justification::centred);
    }

    else
    {
        g.drawText(getFormattedParamText(), 0, 0, getWidth(), texth, juce::Justification::centred);
    }

    if (hasKeyboardFocus(false))
        g.setColour(juce::Colours::cyan);
    else
        g.setColour(juce::Colours::lightgrey);
    float circleCentX = getWidth() / 2.0;
    float circleCentY = (getHeight() - texth) / 2.0 + texth;
    float circleH = getHeight() - texth - 5.0;
    g.fillEllipse(circleCentX - (circleH / 2.0), texth, circleH, circleH);
    float anglerange = 140.0;
    float angle =
        juce::jmap<float>(m_value, m_min_value, m_max_value, -anglerange, anglerange) - 90.0;
    float rads = juce::degreesToRadians(angle);
    float x = circleCentX + (circleH / 2.0) * std::cos(rads);
    float y = circleCentY + (circleH / 2.0) * std::sin(rads);
    g.setColour(juce::Colours::green);
    g.fillEllipse(x - 4.0, y - 4.0, 8.0, 8.0);
}
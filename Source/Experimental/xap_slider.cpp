#include "xap_slider.h"

void XapSlider::paintKnob(juce::Graphics &g)
{
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    if (!m_mousedown)
        g.drawText(m_pardesc.name, 0, 0, getWidth(), 20, juce::Justification::centred);
    else
    {
        g.drawText(getFormattedParamText(), 0, 0, getWidth(), 20, juce::Justification::centred);
    }
    g.setColour(juce::Colours::lightgrey);
    float circleCentX = getWidth() / 2.0;
    float circleCentY = (getHeight() - 20.0) / 2.0 + 20.0f;
    float circleH = getHeight() - 20.0f;
    g.fillEllipse(circleCentX - (circleH / 2.0), 20.0f, circleH, circleH);
    float anglerange = 140.0;
    float angle =
        juce::jmap<float>(m_value, m_min_value, m_max_value, -anglerange, anglerange) - 90.0;
    float rads = juce::degreesToRadians(angle);
    float x = circleCentX + (circleH / 2.0) * std::cos(rads);
    float y = circleCentY + (circleH / 2.0) * std::sin(rads);
    g.setColour(juce::Colours::green);
    g.fillEllipse(x - 6.0, y - 6.0, 12.0, 12.0);
}
#pragma once

#include "JuceHeader.h"

class XapSlider : public juce::Component
{
    double m_value = 0.0;
    double m_modulation_amt = 0.0;
    double m_min_value = 0.0;
    double m_max_value = 1.0;
    double m_default_value = 0.0;
    juce::String m_labeltxt;
    bool m_mousedown = false;
    double m_drag_start_pos = 0.0;

  public:
    XapSlider(juce::String label, bool isHorizontal, double minval, double maxval,
              double defaultval)
    {
        m_value = defaultval;
        m_default_value = m_value;
        m_min_value = minval;
        m_max_value = maxval;
        m_labeltxt = label;
        m_modulation_amt = 0.0;
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colours::black);

        double xcor = juce::jmap<double>(m_value, m_min_value, m_max_value, 2.0, getWidth() - 4.0);
        g.setColour(juce::Colours::darkgrey);
        g.fillRect(2.0f, 0.0, xcor, getWidth() - 2.0);
        g.setColour(juce::Colours::lightgrey);
        g.drawRect(2, 0, getWidth() - 4, getHeight());
        g.setColour(juce::Colours::white);
        g.drawText(m_labeltxt, 5, 0, getWidth() - 10, getHeight(),
                   juce::Justification::centredLeft);
        g.drawText(juce::String(m_value), 5, 0, getWidth() - 10, getHeight(),
                   juce::Justification::centredRight);
    }
    void mouseDown(const juce::MouseEvent &ev) override
    {
        //
        m_drag_start_pos = m_value;
        repaint();
    }
    void mouseDrag(const juce::MouseEvent &ev) override
    {
        double diff = juce::jmap<double>(ev.getDistanceFromDragStartX(), 2, getWidth() - 4,
                                         m_min_value, m_max_value);
        m_value = m_drag_start_pos + diff;
        m_value = juce::jlimit(m_min_value, m_max_value, m_value);
        repaint();
    }
    void mouseUp(const juce::MouseEvent &ev) override
    {
        m_mousedown = false;
        repaint();
    }
    void setValue(double v)
    {
        m_value = v;
        repaint();
    }
    void setModulationAmount(double amt)
    {
        m_modulation_amt = amt;
        repaint();
    }
};

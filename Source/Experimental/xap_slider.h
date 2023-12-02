#pragma once

#include "JuceHeader.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

using ParamDesc = sst::basic_blocks::params::ParamMetaData;

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
    ParamDesc m_pardesc;
    bool m_is_bipolar = false;
    std::vector<double> m_snap_positions;

  public:
    XapSlider(bool isHorizontal, ParamDesc pdesc) : m_pardesc(pdesc)
    {
        m_value = pdesc.defaultVal;
        m_default_value = m_value;
        m_min_value = pdesc.minVal;
        m_max_value = pdesc.maxVal;
        if (m_min_value < 0.0)
            m_is_bipolar = true;
        m_labeltxt = pdesc.name;
        m_modulation_amt = 0.0;
        m_snap_positions.resize(9);
        for (int i = 0; i < 9; ++i)
            m_snap_positions[i] = m_min_value + (m_max_value - m_min_value) / 8 * i;
        setWantsKeyboardFocus(true);
    }
    bool keyPressed(const juce::KeyPress &key) override
    {
        auto c = key.getTextCharacter();
        if (c >= '1' && c <= '9')
        {
            int slot = c - 49;
            setValue(m_snap_positions[slot], true);
            return true;
        }
        return false;
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::darkgrey);
        double xforvalue =
            juce::jmap<double>(m_value, m_min_value, m_max_value, 2.0, getWidth() - 4.0);
        if (!m_is_bipolar)
        {
            // g.fillRect(2.0, 0.0, xforvalue, getHeight());
        }
        else
        {
            double xforzero =
                juce::jmap<double>(0.0, m_min_value, m_max_value, 2.0, getWidth() - 4.0);

            // if (xforvalue < xforzero)
            //     g.fillRect(xforvalue, 0.0, xforzero - xforvalue, getHeight());
            // else
            //     g.fillRect(xforzero, 0.0, xforvalue - xforzero, getHeight());
        }
        if (hasKeyboardFocus(false))
            g.setColour(juce::Colours::cyan);
        else
            g.setColour(juce::Colours::lightgrey);
        double h = getHeight() - 4;
        g.fillEllipse(xforvalue - h / 2, 2, h, h);
        g.setColour(juce::Colours::lightgrey);
        g.drawRect(2, 0, getWidth() - 4, getHeight());
        g.setColour(juce::Colours::white);
        g.drawText(m_labeltxt, 5, 0, getWidth() - 10, getHeight(),
                   juce::Justification::centredLeft);
        auto partext = m_pardesc.valueToString(m_value);
        if (partext)
        {
            g.drawText(*partext, 5, 0, getWidth() - 10, getHeight(),
                       juce::Justification::centredRight);
        }
    }
    void mouseDoubleClick(const MouseEvent &event) override
    {
        m_value = m_default_value;
        if (OnValueChanged)
            OnValueChanged();
        repaint();
    }
    void mouseDown(const juce::MouseEvent &ev) override
    {
        if (ev.mods.isRightButtonDown())
        {
            juce::PopupMenu menu;
            juce::PopupMenu storemenu;
            for (int i = 0; i < m_snap_positions.size(); ++i)
            {
                storemenu.addItem(juce::String(i),
                                  [i, this]() { m_snap_positions[i] = getValue(); });
            }
            menu.addSubMenu("Store to", storemenu);
            juce::PopupMenu loadmenu;
            for (int i = 0; i < m_snap_positions.size(); ++i)
            {
                loadmenu.addItem(juce::String(i),
                                 [i, this]() { setValue(m_snap_positions[i], true); });
            }
            menu.addSubMenu("Load from", loadmenu);
            menu.showMenuAsync({});
            return;
        }

        m_drag_start_pos = m_value;
        repaint();
    }
    void mouseDrag(const juce::MouseEvent &ev) override
    {
        m_value = juce::jmap<double>(ev.x, 2, getWidth() - 4, m_min_value, m_max_value);
        m_value = juce::jlimit(m_min_value, m_max_value, m_value);
        if (OnValueChanged)
            OnValueChanged();
        repaint();
        return;
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
    void setValue(double v, bool notify = false)
    {
        m_value = v;
        if (notify && OnValueChanged)
            OnValueChanged();
        repaint();
    }
    double getValue() { return m_value; }
    void setModulationAmount(double amt)
    {
        m_modulation_amt = amt;
        repaint();
    }
    std::function<void()> OnValueChanged;
};

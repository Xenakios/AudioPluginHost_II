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
    std::vector<std::pair<juce::KeyPress, double>> keypress_to_step;
    double m_param_step = 0.0;
    ParamDesc::FeatureState *m_fstate = nullptr;

  public:
    XapSlider(bool isHorizontal, ParamDesc pdesc, ParamDesc::FeatureState *fstate = nullptr)
        : m_pardesc(pdesc), m_fstate(fstate)
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
        m_param_step = (m_max_value - m_min_value) / 50;
        if (m_pardesc.type == ParamDesc::BOOL)
            m_param_step = 1;
        keypress_to_step.emplace_back(
            juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::noModifiers, 0),
            -m_param_step);
        keypress_to_step.emplace_back(
            juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::noModifiers, 0),
            m_param_step);
        keypress_to_step.emplace_back(
            juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::shiftModifier, 0),
            -m_param_step * 0.1);
        keypress_to_step.emplace_back(
            juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::shiftModifier, 0),
            m_param_step * 0.1);
        setWantsKeyboardFocus(true);
        addChildComponent(m_ed);
    }
    void mouseWheelMove(const juce::MouseEvent &event,
                        const juce::MouseWheelDetails &wheel) override
    {
        double delta = 0.0;
        if (wheel.deltaY < 0)
            delta = -m_param_step;
        else
            delta = m_param_step;
        if (event.mods.isShiftDown())
        {
            if (event.mods.isCommandDown())
                delta *= 2.0;
            else
                delta *= 0.1;
        }
        setValue(m_value + delta, true);
    }
    bool keyPressed(const juce::KeyPress &key) override
    {
        auto c = key.getTextCharacter();
        std::optional<double> val;
        if (c >= '1' && c <= '9')
        {
            int slot = c - 49;
            val = m_snap_positions[slot];
        }
        if (c == '0')
            val = m_default_value;
        if (val)
        {
            setValue(*val, true);
            return true;
        }

        for (auto &e : keypress_to_step)
        {
            if (e.first == key)
            {
                double val = m_value + e.second;
                setValue(val, true);
                return true;
            }
        }

        return false;
    }
    juce::Font m_font;
    juce::TextEditor m_ed;
    void focusGained(juce::Component::FocusChangeType cause) override { repaint(); }
    void focusLost(juce::Component::FocusChangeType cause) override { repaint(); }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::darkgrey);
        g.setFont(m_font);
        g.setFont(getHeight() * 0.5);
        if (!m_err_msg.isEmpty())
        {

            g.setColour(juce::Colours::red);
            g.drawText(m_err_msg, 0, 0, getWidth(), getHeight(), juce::Justification::centred);
            return;
        }
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
        auto partext = valueToString(m_value);
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
    std::optional<std::string> valueToString(float v)
    {
        ParamDesc::FeatureState fs;
        if (m_fstate)
            fs = *m_fstate;
        return m_pardesc.valueToString(v, fs);
    }
    void showTextEditor()
    {
        m_ed.setVisible(true);
        m_ed.grabKeyboardFocus();
        m_ed.setBounds(getWidth() / 2, 0, 80, getHeight());
        auto txt = valueToString(m_value);
        if (txt)
            m_ed.setText(*txt);
        else
            m_ed.setText(juce::String(m_value));
        m_ed.selectAll();
        m_ed.onReturnKey = [this]() {
            std::string err;
            auto v = m_pardesc.valueFromString(m_ed.getText().toStdString(), err);
            if (v)
            {
                setValue(*v, true);
            }
            else
            {
                m_err_msg = err;
                repaint();
                juce::Timer::callAfterDelay(3000, [this]() {
                    m_err_msg = "";
                    repaint();
                });
            }
            m_ed.setVisible(false);
        };
    }
    juce::String m_err_msg;
    void mouseDown(const juce::MouseEvent &ev) override
    {
        if (ev.mods.isRightButtonDown())
        {
            juce::PopupMenu menu;
            menu.addItem("Edit value...", [this]() { showTextEditor(); });
            if (m_fstate)
            {
                menu.addItem("Extend range", true, m_fstate->isExtended, [this]() {
                    m_fstate->isExtended = !m_fstate->isExtended;
                    repaint();
                });
            }
            /*
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
            */
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
        // m_pdesc.
        if (v == m_value)
            return;
        m_value = juce::jlimit(m_min_value, m_max_value, v);
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

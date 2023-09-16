#pragma once

#include "xaudioprocessor.h"
#include "JuceHeader.h"

namespace xenakios
{

class GenericEditor : public juce::Component
{
  public:
    GenericEditor(xenakios::XAudioProcessor &proc) : m_proc(proc)
    {
        for (int i = 0; i < m_proc.paramsCount(); ++i)
        {
            clap_param_info pinfo;
            m_proc.paramsInfo(i, &pinfo);
            auto comps = std::make_unique<ParamComponents>();
            comps->label.setText(pinfo.name, juce::dontSendNotification);
            if (pinfo.flags & CLAP_PARAM_IS_STEPPED)
                comps->slider.setRange(pinfo.min_value, pinfo.max_value, 1.0);
            else
                comps->slider.setRange(pinfo.min_value, pinfo.max_value);
            comps->slider.setDoubleClickReturnValue(true, pinfo.default_value);
            comps->slider.setValue(pinfo.default_value, juce::dontSendNotification);

            comps->slider.onDragStart = [this, pid = pinfo.id, &slid = comps->slider]() {
                m_proc.enqueueParameterChange(
                    {pid, CLAP_EVENT_PARAM_GESTURE_BEGIN, slid.getValue()});
            };
            comps->slider.onValueChange = [this, pid = pinfo.id, &slid = comps->slider]() {
                m_proc.enqueueParameterChange({pid, CLAP_EVENT_PARAM_VALUE, slid.getValue()});
            };
            comps->slider.onDragEnd = [this, pid = pinfo.id, &slid = comps->slider]() {
                m_proc.enqueueParameterChange({pid, CLAP_EVENT_PARAM_GESTURE_END, slid.getValue()});
            };

            addAndMakeVisible(comps->label);
            addAndMakeVisible(comps->slider);
            m_param_comps.push_back(std::move(comps));
        }
        int defaultH = m_proc.paramsCount() * 50;
        setSize(500,defaultH);
    }
    void resized() override
    {
        juce::FlexBox mainflex;
        mainflex.flexDirection = juce::FlexBox::Direction::column;

        for (int i = 0; i < m_param_comps.size(); ++i)
        {
            mainflex.items.add(juce::FlexItem(m_param_comps[i]->label).withFlex(1.0));
            mainflex.items.add(juce::FlexItem(m_param_comps[i]->slider).withFlex(1.0));
        }
        mainflex.performLayout(getLocalBounds());
    }

  private:
    xenakios::XAudioProcessor &m_proc;
    struct ParamComponents
    {
        juce::Slider slider;
        juce::Label label;
    };
    std::vector<std::unique_ptr<ParamComponents>> m_param_comps;
};
}

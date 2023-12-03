#pragma once

#include "xaudioprocessor.h"
#include "JuceHeader.h"
#include "xap_utils.h"
#include "xap_slider.h"

namespace xenakios
{

class GenericEditor : public juce::Component, public juce::Timer
{
  public:
    GenericEditor(xenakios::XAudioProcessor &proc) : m_proc(proc)
    {

        for (int i = 0; i < m_proc.paramDescriptions.size(); ++i)
        {
            auto &pdesc = m_proc.paramDescriptions[i];
            auto comp = std::make_unique<XapSlider>(true, pdesc);
            clap_id pid = pdesc.id;
            comp->OnValueChanged = [pid, this, rslider = comp.get()]() {
                m_proc.enqueueParameterChange({pid, CLAP_EVENT_PARAM_VALUE, rslider->getValue()});
            };
            addAndMakeVisible(comp.get());
            m_id_to_comps[pdesc.id] = comp.get();
            m_param_comps.push_back(std::move(comp));
        }

        int defaultH = m_proc.paramsCount() * 25;
        setSize(500, defaultH);
        startTimer(100);
    }
    void resized() override
    {
        juce::FlexBox mainflex;
        mainflex.flexDirection = juce::FlexBox::Direction::column;
        // mainflex.flexWrap = juce::FlexBox::Wrap::wrap;
        for (int i = 0; i < m_param_comps.size(); ++i)
        {
            mainflex.items.add(juce::FlexItem(*m_param_comps[i]).withFlex(1.0));
        }
        mainflex.performLayout(getLocalBounds());
    }
    void timerCallback() override
    {
        xenakios::CrossThreadMessage msg;
        while (m_proc.dequeueEventForGUI(msg))
        {
            auto it = m_id_to_comps.find(msg.paramId);
            if (it != m_id_to_comps.end())
            {
                it->second->setValue(msg.value);
            }
        }
    }

  private:
    xenakios::XAudioProcessor &m_proc;

    std::vector<std::unique_ptr<XapSlider>> m_param_comps;
    std::unordered_map<clap_id, XapSlider *> m_id_to_comps;
};
} // namespace xenakios

#pragma once

#include "xaudioprocessor.h"
#include "JuceHeader.h"

namespace xenakios
{

class XapSlider : public juce::Slider
{
    ParamDesc m_pardesc;

  public:
    XapSlider(xenakios::ParamDesc desc) : juce::Slider(), m_pardesc(desc) {}
    juce::String getTextFromValue(double value) override
    {
        auto r = m_pardesc.valueToString(value);
        if (r)
            return *r;
        return juce::String(value);
    }
    double getValueFromText(const juce::String &text) override
    {

        std::string err;
        auto r = m_pardesc.valueFromString(text.toStdString(), err);
        if (r)
            return *r;
        std::cout << "slider valueFromText error : " << err << "\n";
        return text.getDoubleValue();
    }
};

class GenericEditor : public juce::Component, public juce::Timer
{
  public:
    GenericEditor(xenakios::XAudioProcessor &proc) : m_proc(proc)
    {
        if (proc.paramDescriptions.size() == 0)
        {
            for (int i = 0; i < m_proc.paramsCount(); ++i)
            {
                clap_param_info pinfo;
                m_proc.paramsInfo(i, &pinfo);
                auto comps = std::make_unique<ParamComponents>();
                m_id_to_comps[pinfo.id] = comps.get();
                comps->paramId = pinfo.id;
                comps->label.setText(pinfo.name, juce::dontSendNotification);
                comps->slider = std::make_unique<juce::Slider>();
                if (pinfo.flags & CLAP_PARAM_IS_STEPPED)
                    comps->slider->setRange(pinfo.min_value, pinfo.max_value, 1.0);
                else
                    comps->slider->setRange(pinfo.min_value, pinfo.max_value);
                comps->slider->setDoubleClickReturnValue(true, pinfo.default_value);
                comps->slider->setValue(pinfo.default_value, juce::dontSendNotification);
                comps->slider->onDragStart = [this, pid = pinfo.id, slid = comps->slider.get()]() {
                    m_proc.enqueueParameterChange(
                        {pid, CLAP_EVENT_PARAM_GESTURE_BEGIN, slid->getValue()});
                };
                comps->slider->onValueChange = [this, pid = pinfo.id,
                                                slid = comps->slider.get()]() {
                    m_proc.enqueueParameterChange({pid, CLAP_EVENT_PARAM_VALUE, slid->getValue()});
                };
                comps->slider->onDragEnd = [this, pid = pinfo.id, slid = comps->slider.get()]() {
                    m_proc.enqueueParameterChange(
                        {pid, CLAP_EVENT_PARAM_GESTURE_END, slid->getValue()});
                };

                addAndMakeVisible(comps->label);
                addAndMakeVisible(*comps->slider);
                m_param_comps.push_back(std::move(comps));
            }
        }
        else
        {
            for (int i = 0; i < m_proc.paramDescriptions.size(); ++i)
            {
                auto &pdesc = m_proc.paramDescriptions[i];
                auto comps = std::make_unique<ParamComponents>();
                m_id_to_comps[pdesc.id] = comps.get();
                comps->label.setText(pdesc.name, juce::dontSendNotification);
                if (pdesc.displayScale != ParamDesc::DisplayScale::UNORDERED_MAP)
                {
                    comps->slider = std::make_unique<XapSlider>(pdesc);
                    addAndMakeVisible(*comps->slider);
                    comps->paramId = pdesc.id;

                    if (pdesc.flags & CLAP_PARAM_IS_STEPPED)
                        comps->slider->setRange(pdesc.minVal, pdesc.maxVal, 1.0);
                    else
                        comps->slider->setRange(pdesc.minVal, pdesc.maxVal);
                    comps->slider->setDoubleClickReturnValue(true, pdesc.defaultVal);
                    // comps->slider->setValue(pdesc.defaultVal, juce::dontSendNotification);
                    comps->slider->onDragStart = [this, pid = pdesc.id,
                                                  slid = comps->slider.get()]() {
                        m_proc.enqueueParameterChange(
                            {pid, CLAP_EVENT_PARAM_GESTURE_BEGIN, slid->getValue()});
                    };
                    comps->slider->onValueChange = [this, pid = pdesc.id,
                                                    slid = comps->slider.get()]() {
                        m_proc.enqueueParameterChange(
                            {pid, CLAP_EVENT_PARAM_VALUE, slid->getValue()});
                    };
                    comps->slider->onDragEnd = [this, pid = pdesc.id,
                                                slid = comps->slider.get()]() {
                        m_proc.enqueueParameterChange(
                            {pid, CLAP_EVENT_PARAM_GESTURE_END, slid->getValue()});
                    };
                }
                if (pdesc.displayScale == ParamDesc::DisplayScale::UNORDERED_MAP)
                {
                    comps->combo = std::make_unique<juce::ComboBox>();
                    for (auto &e : pdesc.discreteValues)
                    {
                        comps->combo->addItem(e.second, e.first + 1);
                    }
                    // comps->combo->setSelectedId((int)pdesc.defaultVal + 1,
                    //                             juce::dontSendNotification);
                    addAndMakeVisible(*comps->combo);
                    comps->paramId = pdesc.id;
                }
                addAndMakeVisible(comps->label);

                m_param_comps.push_back(std::move(comps));
            }
        }

        int defaultH = m_proc.paramsCount() * 50;
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
            mainflex.items.add(juce::FlexItem(m_param_comps[i]->label).withFlex(1.0));
            if (m_param_comps[i]->slider)
                mainflex.items.add(juce::FlexItem(*m_param_comps[i]->slider).withFlex(1.0));
            if (m_param_comps[i]->combo)
                mainflex.items.add(juce::FlexItem(*m_param_comps[i]->combo).withFlex(1.0));
        }
        mainflex.performLayout(getLocalBounds());
    }
    void timerCallback() override
    {
        xenakios::CrossThreadMessage msg;
        while (m_proc.dequeueEventForGUI(msg))
        {
            if (m_id_to_comps.contains(msg.paramId))
            {
                auto comps = m_id_to_comps[msg.paramId];
                if (comps->slider)
                    comps->slider->setValue(msg.value, juce::dontSendNotification);
                if (comps->combo)
                    comps->combo->setSelectedId((int)msg.value + 1, juce::dontSendNotification);
            }
        }
    }

  private:
    xenakios::XAudioProcessor &m_proc;
    struct ParamComponents
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        juce::Label label;
        clap_id paramId = 0;
    };
    std::vector<std::unique_ptr<ParamComponents>> m_param_comps;
    std::unordered_map<clap_id, ParamComponents *> m_id_to_comps;
};
} // namespace xenakios

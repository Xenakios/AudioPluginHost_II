#pragma once

#include "JuceHeader.h"

class TestExtension
{
public:
    juce::TextEditor& m_ed;
    TestExtension(juce::TextEditor& ed) : m_ed(ed) {}
    void SayHello()
    {
        m_ed.insertTextAtCaret("Hello from TestExtension");
    }
    void log(const char* msg)
    {
        m_ed.insertTextAtCaret(msg);
    }
};

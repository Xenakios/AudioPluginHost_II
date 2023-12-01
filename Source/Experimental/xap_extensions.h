#pragma once

#include "JuceHeader.h"

class IHostExtension
{
public:
    virtual ~IHostExtension() {}
    virtual void SayHello() = 0;
    virtual void log(const char* msg) = 0;
    virtual void setNodeCanvasProperty(int propIndex, juce::var v) = 0;
    
};

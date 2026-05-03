#include "dashboardcomponent.h"

void DashBoardComponent::paintAmbisonicFieldHammerProjection(juce::Graphics &g)
{
    g.saveState();
    haGrid.paint(g);
    for (auto &e : persisted_events)
    {
        if (e.visualfade > 0.01)
        {
            auto ptcor = haGrid.anglesToPoint(e.azimuth0degrees, -e.elevationdegrees);
            float x = ptcor.getX();
            float y = ptcor.getY();
            haGrid.toArea.transformPoint(x, y);
            g.setColour(juce::Colours::cyan.withAlpha(e.visualfade));
            e.visualfade *= visualfadecoefficient;
            g.fillEllipse(x - 6.0, y - 6.0, 12.0f, 12.0f);
        }
    }
    g.restoreState();
    return;
    g.setColour(juce::Colours::white);
    float ellipW = 400.0f;
    float h = ellipW / 1.5;
    float halfW = ellipW / 2.0f;
    float halfH = h / 2.0;
    float centerX = halfW;
    float centerY = getHeight() / 2.0;
    g.drawEllipse(0.0f, centerY - halfH, ellipW, h, 2.0f);
    g.drawLine(centerX, centerY - halfH, centerX, centerY + halfH, 2.0f);
    g.drawLine(centerX - halfW, centerY, centerX + halfW, centerY, 2.0f);
    float lineX = centerX - halfW / 2.0f;
    float dx = (lineX - centerX) / halfW;
    float dy = halfH * std::sqrt(1.0f - dx * dx);
    g.drawLine(lineX, centerY - dy, lineX, centerY + dy, 2.0f);
    g.drawText("LEFT", juce::Rectangle<float>(lineX + 2.0f, centerY, 70.0f, 25.0f),
               juce::Justification::centredLeft);
    lineX = centerX + halfW / 2.0f;
    g.drawLine(lineX, centerY - dy, lineX, centerY + dy, 2.0f);
    g.drawText("RIGHT", juce::Rectangle<float>(lineX + 2.0f, centerY, 70.0f, 25.0f),
               juce::Justification::centredLeft);

    g.drawText("FRONT", juce::Rectangle<float>(centerX + 2.0, centerY, 70.0f, 25.0f),
               juce::Justification::centredLeft);
    g.drawText("BACK", juce::Rectangle<float>(centerX - halfW + 2.0, centerY, 70.0f, 25.0f),
               juce::Justification::centredLeft);
    g.drawText("BACK", juce::Rectangle<float>(centerX + halfW - 70.0, centerY, 70.0f, 25.0f),
               juce::Justification::centredRight);
    g.drawText("TOP", juce::Rectangle<float>(centerX - 20.0, centerY - halfH - 20.0f, 70.0f, 25.0f),
               juce::Justification::centredTop);
    g.drawText("BOTTOM", juce::Rectangle<float>(centerX - 20.0, centerY + halfH, 70.0f, 25.0f),
               juce::Justification::centredBottom);
    for (auto &e : persisted_events)
    {
        if (e.visualfade > 0.01)
        {
            auto drawgrain = [this, halfW, halfH, centerX, centerY, &g, &e](int chan) {
                const float sqrt2 = 1.41421356f;
                float radAzi = juce::degreesToRadians(-e.azimuth0degrees);
                if (chan == 1)
                    radAzi = juce::degreesToRadians(-e.azimuth1degrees);
                float radElev = juce::degreesToRadians(e.elevationdegrees);
                float cosElev = std::cos(radElev);
                float cosHalfAzi = std::cos(radAzi * 0.5f);

                // The term inside the sqrt can theoretically be slightly negative
                // due to float precision errors at the very edge (180 deg).
                float inner = 1.0f + cosElev * cosHalfAzi;
                float d = std::sqrt(std::max(0.0f, inner));

                // Safety: prevent division by zero at the singularity
                if (d < 0.001f)
                    d = 0.001f;

                float x = (2.0f * sqrt2 * cosElev * std::sin(radAzi * 0.5f)) / d;
                float y = (sqrt2 * std::sin(radElev)) / d;

                // Mapping to your 2:1 Ellipse bounds
                // This ensures that the dot stays perfectly within the visual ellipse
                float scaleX = halfW / (2.0f * sqrt2);
                float scaleY = halfH / sqrt2;

                float pixelX = centerX + (x * scaleX);
                float pixelY = centerY - (y * scaleY);
                if (chan == 0)
                    g.setColour(juce::Colours::lightgreen.withAlpha(e.visualfade));
                else
                    g.setColour(juce::Colours::lightcyan.withAlpha(e.visualfade));
                g.fillEllipse(pixelX - 6.0f, pixelY - 6.0f, 12.0f, 12.0f);
            };
            if (std::abs(e.azimuth0degrees - e.azimuth1degrees) > 0.0f)
            {
                drawgrain(0);
                drawgrain(1);
            }
            else
            {
                drawgrain(0);
            }
            e.visualfade = e.visualfade * visualfadecoefficient;
        }
    }
}

void DashBoardComponent::drawCPUGraph(juce::Graphics &g, double enginetime,
                                      juce::Rectangle<float> area)
{
    paramHistoryPath.clear();
    for (size_t i = 0; i < paramValuesHistory.size(); ++i)
    {
        const auto &e = paramValuesHistory[i];
        float xcor =
            area.getWidth() - ((enginetime - e.timestamp) / timespantoshow * area.getWidth());
        // xcor = std::clamp<float>(xcor + xoffs, xoffs, getWidth());
        xcor = xcor + area.getX();
        float ycor = juce::jmap<float>(e.cpu_usage, 0.0, 1.0, area.getBottom(), area.getY());
        if (i == 0)
            paramHistoryPath.startNewSubPath(juce::Point<float>(xcor, ycor));
        else
            paramHistoryPath.lineTo(juce::Point<float>(xcor, ycor));
    }
    g.setColour(juce::Colours::beige.withAlpha(0.5f));
    g.strokePath(paramHistoryPath, juce::PathStrokeType(3.0f));
    g.setColour(juce::Colours::white);
    g.drawRect(area);
    area = area.reduced(2.0f);
    g.drawText("100%", area, juce::Justification::topLeft);
    g.drawText("CPU LOAD", area, juce::Justification::centredTop);
    g.drawText("0%", area, juce::Justification::bottomLeft);
    if (GetCPULoad)
    {
        g.drawText(juce::String((int)(GetCPULoad() * 100.0f)) + "%", area,
                   juce::Justification::topRight);
    }
}

void DashBoardComponent::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colours::black);

    if (showModulatorValues)
    {
        float barw = 16.0;
        g.setColour(juce::Colours::green);
        for (int i = 0; i < ToneGranulator::MIDICCEND - 32; ++i)
        {
            float xcor = i * barw;
            float v = gr->modSourceValues[i];
            if (v < 0.0)
            {
                float h = juce::jmap<float>(v, -1.0, 0.0, getHeight() / 2.0, 0.0);
                g.fillRect(xcor, getHeight() / 2.0, barw - 1.0f, h);
            }
            else
            {

                float h = juce::jmap<float>(v, 0.0, 1.0, 0.0, getHeight() / 2.0);
                g.fillRect(xcor, getHeight() / 2.0 - h, barw - 1.0f, h);
            }
        }
    }
    // g.setColour(juce::Colours::white);
    juce::Rectangle<float> cloudArea{500.0f, 0.0f, getWidth() - 500.0f, getHeight() - 123.0f};
    juce::Rectangle<float> scopeArea{cloudArea.getX(), cloudArea.getBottom() + 1.0f,
                                     cloudArea.getWidth(), 60.0f};
    juce::Rectangle<float> cpuArea{cloudArea.getX(), scopeArea.getBottom() + 1.0f,
                                   cloudArea.getWidth(), 60.0f};
    double enginetime = gr->playposframes / gr->m_sr;
    g.saveState();
    g.reduceClipRegion(juce::Rectangle<int>(cloudArea.getX(), 0, getWidth(), getHeight()));
    for (auto &e : persisted_events)
    {
        float alpha = juce::jmap<float>(e.gain, 0.0f, 1.0f, 0.0f, 1.0f);
        float normpitch = juce::jmap<float>(e.pitch, -48.0f, 64.0f, 0.0f, 1.0f);
        g.setColour(pitchGradient.getColourAtPosition(normpitch).withBrightness(alpha));
        float xcor = cloudArea.getWidth() -
                     ((enginetime - e.timepos) / timespantoshow * cloudArea.getWidth());
        float ycor = juce::jmap<float>(e.pitch, -48.0, 64.0, cloudArea.getHeight() - 5.0, 0.0);
        float gw = cloudArea.getWidth() / timespantoshow * e.duration;
        // xcor = std::clamp<float>(xcor + xoffs, xoffs, getWidth());
        xcor = xcor + cloudArea.getX();
        g.fillEllipse(xcor, ycor, gw, 5.0);
    }

    paramHistoryPath.clear();

    if (auto parid = gr->modulatedParamToStore.load())
    {
        auto &pmd = gr->idtoparmetadata[parid];
        float parmin = pmd->minVal;
        float parmax = pmd->maxVal;
        if (parid == ToneGranulator::PAR_PITCH)
        {
            parmin = -48.0f;
            parmax = 64.0f;
        }
        for (size_t i = 0; i < paramValuesHistory.size(); ++i)
        {
            const auto &e = paramValuesHistory[i];
            float xcor = scopeArea.getWidth() -
                         ((enginetime - e.timestamp) / timespantoshow * scopeArea.getWidth());
            // xcor = std::clamp<float>(xcor + xoffs, xoffs, getWidth());
            xcor = xcor + scopeArea.getX();
            float ycor = juce::jmap<float>(e.value, parmin, parmax, scopeArea.getBottom() - 5.0,
                                           scopeArea.getY());
            if (i == 0)
                paramHistoryPath.startNewSubPath(juce::Point<float>(xcor, ycor));
            else
                paramHistoryPath.lineTo(juce::Point<float>(xcor, ycor));
        }
        g.setColour(juce::Colours::beige.withAlpha(0.5f));
        g.strokePath(paramHistoryPath, juce::PathStrokeType(3.0f));
        g.setColour(juce::Colours::white);
        g.drawRect(scopeArea);
        scopeArea = scopeArea.reduced(2.0f);
        g.drawText(pmd->name, scopeArea, juce::Justification::centredTop);
        g.drawText(pmd->valueToString(pmd->minVal).value_or("NO FMT"), scopeArea,
                   juce::Justification::bottomLeft);
        g.drawText(pmd->valueToString(pmd->maxVal).value_or("NO FMT"), scopeArea,
                   juce::Justification::topLeft);
    }
    else
    {
        g.setColour(juce::Colours::white);
        g.drawRect(scopeArea);
        scopeArea = scopeArea.reduced(2.0f);
        g.drawText("No parameter selected", scopeArea, juce::Justification::centredTop);
    }

    drawCPUGraph(g, enginetime, cpuArea);
    g.restoreState();
    paintAmbisonicFieldHammerProjection(g);
    g.setColour(juce::Colours::yellow);
    float h = juce::Decibels::gainToDecibels(gr->compensationgainforgui.load());
    h = std::clamp(h, -40.0f, 0.0f);
    h = juce::jmap<float>(h, -40.0, 0.0, 0.0, getHeight());
    g.fillRect(juce::Rectangle<float>{0.0f, (float)getHeight() - h, 10.0f, h});
    g.setColour(juce::Colours::white);
    int mins = static_cast<int>(enginetime / 60.0);
    int secs = static_cast<int>(std::fmod(enginetime, 60.0));
    int ms = static_cast<int>(std::fmod(enginetime * 1000.0, 1000.0));

    // 2. Format with leading zeros
    juce::String timeText = juce::String::formatted("%02d:%02d.%03d", mins, secs, ms);
    timeText += " " + juce::String(gr->missedgrains) + " missed grains";
    // timeText += " " + juce::String(persisted_events.size()) + " events in history";
    // timeText += " " + juce::String(gr->parmetadatas.size()) + " parameters";
    timeText += " current snapshot : " + juce::String(gr->currentSnapShot);
    timeText += " ambisonic order " + juce::String(gr->current_ambisonic_order);
    timeText += " engine channels " + juce::String(gr->num_out_chans);
    g.setFont(18.0f);
    g.drawText(timeText, cloudArea.getX(), 1.0f, cloudArea.getWidth(), 25,
               juce::Justification::left);
}

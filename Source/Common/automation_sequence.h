#pragma once

#include <stdexcept>
#include <vector>
#include "containers/choc_NonAllocatingStableSort.h"
#include "containers/choc_Span.h"

namespace xenakios
{
/*
This is a lighter way to deal with sequences of DSP automation events
than the ClapEventSequence. We should be using this whenever possible from now on.
*/

enum Flags
{
    ISMODULATION = 1 << 0,
    ISLINEARLAG = 1 << 1,
    ISFILTERLAG = 1 << 2
};

struct AutomationEvent
{
    double timestamp; // seconds/beats
    double value;     // parameter value, modulation amount or lag time
    uint32_t id;      // parameter id (compatible with clap uint32_t id)
    uint32_t flags;   // flags/extra data
    uint16_t target;  // target synth voice/channel/effect submodule
    bool operator<(const AutomationEvent &other) { return timestamp < other.timestamp; }
};

struct AutomationSequence
{
    std::vector<AutomationEvent> events;
    bool is_sorted = false;
    AutomationSequence() { events.reserve(128); }
    void add_event(double timestamp, uint32_t id, double value)
    {
        events.emplace_back(timestamp, value, id, 0);
        is_sorted = false;
    }
    void add_event_as_modulation(double timestamp, uint32_t id, double amount)
    {
        events.emplace_back(timestamp, amount, id, ISMODULATION);
        is_sorted = false;
    }
    size_t size() const { return events.size(); }
    void clear() { events.clear(); }
    void sort_events()
    {
        choc::sorting::stable_sort(events.begin(), events.end());
        is_sorted = true;
    }
    struct Iterator
    {
        /// Creates an iterator positioned at the start of the sequence.
        Iterator(const AutomationSequence &s, double sr) : owner(s), sampleRate(sr)
        {
            if (!owner.is_sorted)
                throw std::runtime_error("AutomationSequence is not sorted");
        }
        Iterator(const Iterator &) = default;
        Iterator(Iterator &&) = default;

        /// Seeks the iterator to the given time

        void setTime(int64_t newTimeStamp)
        {
            auto eventData = owner.events.data();

            while (nextIndex != 0 &&
                   eventData[nextIndex - 1].timestamp * sampleRate >= newTimeStamp)
                --nextIndex;

            while (nextIndex < owner.events.size() &&
                   eventData[nextIndex].timestamp * sampleRate < newTimeStamp)
                ++nextIndex;

            currentTime = newTimeStamp;
        }

        /// Returns the current iterator time
        int64_t getTime() const noexcept { return currentTime; }

        /// Returns a set of events which lie between the current time, up to (but not
        /// including) the given duration. This function then increments the iterator to
        /// set its current time to the end of this block.

        choc::span<const AutomationEvent> readNextEvents(int duration)
        {
            auto start = nextIndex;
            auto eventData = owner.events.data();
            auto end = start;
            auto total = owner.events.size();
            auto endTime = currentTime + duration;
            currentTime = endTime;

            while (end < total && eventData[end].timestamp * sampleRate < endTime)
                ++end;

            nextIndex = end;

            return {eventData + start, eventData + end};
        }

      private:
        const AutomationSequence &owner;
        int64_t currentTime = 0;
        size_t nextIndex = 0;
        double sampleRate = 0.0;
    };
};
} // namespace xenakios

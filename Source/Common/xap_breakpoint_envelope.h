#pragma once

#include <vector>
#include <optional>
#include <functional>
#include "xap_utils.h"
#include "containers/choc_SmallVector.h"

namespace xenakios
{
class EnvelopePoint
{
  public:
    enum class Shape
    {
        Linear,
        Hold,
        Abrupt,
        Last
    };
    EnvelopePoint() {}
    EnvelopePoint(double x, double y, Shape s = Shape::Linear, double p0 = 0.0, double p1 = 0.0)
        : m_x(x), m_y(y), m_shape(s), m_p0(p0), m_p1(p1)
    {
    }
    double getX() const { return m_x; }
    double getY() const { return m_y; }
    Shape getShape() const { return m_shape; }

  private:
    double m_x = 0.0;
    double m_y = 0.0;
    double m_p0 = 0.0;
    double m_p1 = 0.0;
    Shape m_shape = Shape::Linear;
};

// Simple breakpoint envelope class, modelled after the SST LFO.
// Output is always calculated into the outputBlock array.
// This aims to be as simple as possible, to allow composing
// more complicated things elsewhere.
class Envelope
{
  public:
    // float outputBlock[BLOCK_SIZE];
    choc::SmallVector<float, 128> outputBlock;
    void clearOutputBlock()
    {
        for (size_t i = 0; i < outputBlock.size(); ++i)
            outputBlock[i] = 0.0f;
    }
    Envelope(std::optional<double> defaultPointValue = {})
    {
        m_points.reserve(16);
        if (defaultPointValue)
            addPoint({0.0, *defaultPointValue});
        outputBlock.resize(1);
        clearOutputBlock();
    }
    Envelope(std::vector<EnvelopePoint> points) : m_points(std::move(points))
    {
        sortPoints();
        clearOutputBlock();
    }
    auto begin() { return m_points.begin(); }
    auto end() { return m_points.end(); }
    void addPoint(EnvelopePoint pt)
    {
        m_points.push_back(pt);
        m_sorted = false;
    }

    void removeEnvelopePointAtIndex(int index)
    {
        if (index < 0 || index >= m_points.size())
            throw std::runtime_error(std::format("Index {} out of range", index));
        m_points.erase(m_points.begin() + index);
    }
    void removeEnvelopePoints(std::function<bool(EnvelopePoint)> func)
    {
        std::erase_if(m_points, func);
    }
    // use carefully, only when you are going to add at least one point right after this
    void clearAllPoints()
    {
        m_points.clear();
        m_sorted = false;
    }
    // value = normalized 0..1 position in the envelope segment
    static double getShapedValue(double value, EnvelopePoint::Shape shape, double p0, double p1)
    {
        if (shape == EnvelopePoint::Shape::Linear)
            return value;
        // holds the value for 99% the segment length, then ramps to the next value
        // literal sudden jump is almost never useful, but we might want to support that too...
        if (shape == EnvelopePoint::Shape::Hold)
        {
            // we might want to somehow make this work based on samples/time,
            // but percentage will have to work for now
            if (value < 0.99)
                return 0.0;
            return xenakios::mapvalue(value, 0.99, 1.0, 0.0, 1.0);
        }
        if (shape == EnvelopePoint::Shape::Abrupt)
        {
            return 0.0;
        }
        return value;
    }
    size_t getNumPoints() const { return m_points.size(); }
    // int because we want to allow negative index...
    const EnvelopePoint &getPointSafe(int index) const
    {
        if (index < 0)
            return m_points.front();
        if (index >= m_points.size())
            return m_points.back();
        return m_points[index];
    }
    void setPoint(int index, EnvelopePoint pt)
    {
        if (index < 0 || index >= m_points.size())
            throw std::runtime_error(std::format("Index {} out of range", index));
        m_points[index] = pt;
        m_sorted = false;
    }
    void sortPoints()
    {
        choc::sorting::stable_sort(
            m_points.begin(), m_points.end(),
            [](const EnvelopePoint &a, const EnvelopePoint &b) { return a.getX() < b.getX(); });
        m_sorted = true;
    }
    int currentPointIndex = -1;
    void updateCurrentPointIndex(double t)
    {
        int newIndex = currentPointIndex;
        if (t < m_points.front().getX())
            newIndex = 0;
        else if (t > m_points.back().getX())
            newIndex = m_points.size() - 1;
        else
        {
            for (int i = 0; i < m_points.size(); ++i)
            {
                if (t >= m_points[i].getX())
                {
                    newIndex = i;
                }
            }
        }

        if (newIndex != currentPointIndex)
        {
            currentPointIndex = newIndex;
            // std::cout << "update current point index to " << currentPointIndex << " at tpos " <<
            // t
            //           << "\n";
        }
    }
    double getValueAtPosition(double pos)
    {
        if (!m_sorted)
            sortPoints();
        processBlock(pos, 0.0, 2, 1);
        return outputBlock[0];
    }
    // interpolate_mode :
    // 0 : sample accurately interpolates into the outputBlock
    // 1 : fills the output block with the same sampled value from the envelope at the timepos
    // 2 : sets only the first outputBlock element into the sampled value from the envelope at the
    // timepos, useful if you really know you are never going to care about about the other array
    // elements
    void processBlock(double timepos, double samplerate, int interpolate_mode, size_t blockSize)
    {
        outputBlock.resize(blockSize);
        
// behavior would be undefined if the envelope points are not sorted or if no points
#if XENPYTHONBINDINGS
        if (!m_sorted)
            throw std::runtime_error("Envelope points are not sorted");
        if (m_points.size() == 0)
            throw std::runtime_error("Envelope has no points to evaluate");
#else
        assert(m_sorted && m_points.size() > 0);
#endif
        if (currentPointIndex == -1 || timepos < m_points[currentPointIndex].getX() ||
            timepos >= getPointSafe(currentPointIndex + 1).getX())
        {
            updateCurrentPointIndex(timepos);
        }

        int index0 = currentPointIndex;
        assert(index0 >= 0);
        auto &pt0 = getPointSafe(index0);
        auto &pt1 = getPointSafe(index0 + 1);
        double x0 = pt0.getX();
        double x1 = pt1.getX();
        double y0 = pt0.getY();
        double y1 = pt1.getY();
        if (interpolate_mode > 0)
        {
            double outvalue = x0;
            double xdiff = x1 - x0;
            if (xdiff < 0.00001)
                outvalue = y1;
            else
            {
                double ydiff = y1 - y0;
                double normpos = ((1.0 / xdiff * (timepos - x0)));
                outvalue = y0 + ydiff * getShapedValue(normpos, pt0.getShape(), 0.0, 0.0);
            }
            if (interpolate_mode == 1)
            {
                for (int i = 0; i < outputBlock.size(); ++i)
                {
                    outputBlock[i] = outvalue;
                }
            }
            else
                outputBlock[0] = outvalue;

            return;
        }
        assert(samplerate > 0.0);
        const double invsr = 1.0 / samplerate;
        auto shape = pt0.getShape();
        for (int i = 0; i < outputBlock.size(); ++i)
        {
            double outvalue = x0;
            double xdiff = x1 - x0;
            if (xdiff < 0.00001)
                outvalue = y1;
            else
            {
                double ydiff = y1 - y0;
                double normpos = ((1.0 / xdiff * (timepos - x0)));
                normpos = Envelope::getShapedValue(normpos, shape, 0.0, 0.0);
                outvalue = y0 + ydiff * normpos;
            }
            outputBlock[i] = outvalue;
            timepos += invsr;
            // we may get to the next envelope point within the block, so
            // advance and update as needed
            if (timepos >= x1)
            {
                ++index0;
                auto &tpt0 = getPointSafe(index0);
                auto &tpt1 = getPointSafe(index0 + 1);
                x0 = tpt0.getX();
                x1 = tpt1.getX();
                y0 = tpt0.getY();
                y1 = tpt1.getY();
                shape = tpt0.getShape();
            }
        }
    }

  private:
    std::vector<EnvelopePoint> m_points;
    bool m_sorted = false;
};
} // namespace xenakios

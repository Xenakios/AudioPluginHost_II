#include "grainfx.h"

#include "plugins/BezEQ.h"
#include "plugins/HipCrush.h"
#include "plugins/kWoodRoom.h"
#include "plugins/RingModulator.h"
#include "plugins/PrimeFIR.h"
#include "plugins/Hypersoft.h"
#include "plugins/DeRez3.h"
#include "plugins/CrunchCoat.h"
#include "plugins/BitGlitter.h"
#include "plugins/ToTape9.h"

void GrainInsertFX::setMode(ModeInfo m)
{
    assert(sr > 0);
    if (m.mainmode == 0)
    {
        std::fill(paramvalues.begin(), paramvalues.end(), 0.0f);
        mainmode = 0;
        numParams = 0;
    }
    if (m.mainmode == 1)
    {
        mainmode = 1;
        numParams = 4;
        paramvalues[0] = 1.0;
        paramvalues[1] = 0.0;
        paramvalues[2] = 0.0;
        paramvalues[3] = 0.5;
        auto reqdelaysize =
            sst::filtersplusplus::Filter::requiredDelayLinesSizes(m.sstmodel, m.sstconfig);

        if (reqdelaysize * 4 > delaylinememory.size())
            delaylinememory.resize(reqdelaysize * 4);
        sstfilter.setFilterModel(m.sstmodel);
        sstfilter.setModelConfiguration(m.sstconfig);
        sstfilter.setSampleRateAndBlockSize(sr, blockSize);
        sstfilter.setStereo();
        sstfilter.provideAllDelayLines(delaylinememory.data());
        if (!sstfilter.prepareInstance())
        {
            std::print("could not prepare filter {}\n", m.displayname);
        }
    }
    if (m.mainmode == 2)
    {
        mainmode = 0;
        numParams = 0;
        if (m.awtype == 0)
        {
            awplugin = make_aw_safe<airwinconsolidated::BezEQ::BezEQ>(0);
            numParams = airwinconsolidated::BezEQ::kNumParameters;
        }
        else if (m.awtype == 1)
        {
            awplugin = make_aw_safe<airwinconsolidated::HipCrush::HipCrush>(0);
            numParams = airwinconsolidated::HipCrush::kNumParameters;
        }
        else if (m.awtype == 2)
        {
            awplugin = make_aw_safe<airwinconsolidated::kWoodRoom::kWoodRoom>(0);
            numParams = airwinconsolidated::kWoodRoom::kNumParameters;
        }
        else if (m.awtype == 3)
        {
            awplugin = make_aw_safe<airwinconsolidated::RingModulator::RingModulator>(0);
            numParams = airwinconsolidated::RingModulator::kNumParameters;
        }
        else if (m.awtype == 4)
        {
            awplugin = make_aw_safe<airwinconsolidated::PrimeFIR::PrimeFIR>(0);
            numParams = airwinconsolidated::PrimeFIR::kNumParameters;
        }
        else if (m.awtype == 5)
        {
            awplugin = make_aw_safe<airwinconsolidated::Hypersoft::Hypersoft>(0);
            numParams = airwinconsolidated::Hypersoft::kNumParameters;
        }
        else if (m.awtype == 6)
        {
            awplugin = make_aw_safe<airwinconsolidated::DeRez3::DeRez3>(0);
            numParams = airwinconsolidated::DeRez3::kNumParameters;
        }
        else if (m.awtype == 7)
        {
            awplugin = make_aw_safe<airwinconsolidated::CrunchCoat::CrunchCoat>(0);
            numParams = airwinconsolidated::CrunchCoat::kNumParameters;
        }
        else if (m.awtype == 8)
        {
            awplugin = make_aw_safe<airwinconsolidated::BitGlitter::BitGlitter>(0);
            numParams = airwinconsolidated::BitGlitter::kNumParameters;
        }
        else if (m.awtype == 9)
        {
            awplugin = make_aw_safe<airwinconsolidated::ToTape9::ToTape9>(0);
            numParams = airwinconsolidated::ToTape9::kNumParameters;
        }
        assert(numParams < 11);
        if (awplugin)
        {
            mainmode = 2;
            awplugin->setNumInputs(2);
            awplugin->setNumOutputs(2);
            awplugin->setSampleRate(sr);
            std::fill(paramvalues.begin(), paramvalues.end(), 0.0f);
            for (size_t i = 0; i < numParams; ++i)
            {
                paramvalues[i] = awplugin->getParameter(i);
            }
        }
    }
}
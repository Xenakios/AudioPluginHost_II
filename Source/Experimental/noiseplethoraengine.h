#pragma once

#include "../Plugins/noise-plethora/plugins/NoisePlethoraPlugin.hpp"
#include "../Plugins/noise-plethora/plugins/Banks.hpp"
#include "audio/choc_AudioFileFormat_WAV.h"
#include "sst/basic-blocks/dsp/PanLaws.h"
#include "gui/choc_DesktopWindow.h"
#include "gui/choc_MessageLoop.h"
#include "gui/choc_WebView.h"
#include "text/choc_Files.h"
#include "xap_utils.h"
#include "xapdsp.h"

class SignalSmoother
{
  public:
    SignalSmoother() {}
    inline double process(double in)
    {
        double result = in + m_slope * (m_history - in);
        m_history = result;
        return result;
    }
    void setSlope(double x) { m_slope = x; }
    double getSlope() const { return m_slope; }

  private:
    float m_history = 0.0f;
    float m_slope = 0.999f;
};

constexpr size_t ENVBLOCKSIZE = 64;

inline void list_plugins()
{
    int k = 0;
    for (int i = 0; i < numBanks; ++i)
    {
        auto &bank = getBankForIndex(i);
        std::cout << "bank " << i << "\n";
        for (int j = 0; j < programsPerBank; ++j)
        {
            std::cout << "\t" << bank.getProgramName(j) << "\t\t" << k << "\n";
            // availablePlugins[k] = bank.getProgramName(j);
            ++k;
        }
    }
}

class NoisePlethoraVoice
{
  public:
    struct VoiceParams
    {
        float volume = -6.0f;
        float x = 0.5f;
        float y = 0.5f;
        float filtcutoff = 120.0f;
        float filtreson = 0.01;
        float algo = 0.0f;
        float pan = 0.5f;
        float filttype = 0.0f;
    };
    VoiceParams basevalues;
    VoiceParams modvalues;
    enum class EnvelopeState
    {
        Idle,
        Attack,
        Sustain,
        Release
    };
    EnvelopeState envstate = EnvelopeState::Idle;
    double env_attack = 0.01;
    double env_release = 1.0;
    int env_counter = 0;
    NoisePlethoraVoice()
    {
        modvalues.algo = 0;
        modvalues.filtcutoff = 0;
        modvalues.filtreson = 0;
        modvalues.pan = 0;
        modvalues.volume = 0;
        modvalues.x = 0;
        modvalues.y = 0;
        modvalues.filttype = 0.0f;
        for (int i = 0; i < numBanks; ++i)
        {
            auto &bank = getBankForIndex(i);
            for (int j = 0; j < programsPerBank; ++j)
            {
                auto progname = bank.getProgramName(j);
                auto p = MyFactory::Instance()->Create(progname);
                m_plugs.push_back(std::move(p));
            }
        }
    }
    double hipasscutoff = 12.0;
    void prepare(double sampleRate)
    {
        m_sr = sampleRate;
        dcblocker.setCoeff(hipasscutoff, 0.01, 1.0 / m_sr);
        dcblocker.init();
        filter.init();
        m_gain_smoother.setSlope(0.999);
        m_pan_smoother.setSlope(0.999);
        for (auto &p : m_plugs)
        {
            p->init();
            p->m_sr = m_sr;
        }
    }
    double velocity = 1.0;
    void activate(int port_, int chan_, int key_, int id_, double velo)
    {
        velocity = velo;
        port_id = port_;
        chan = chan_;
        key = key_;
        note_id = id_;
        envstate = EnvelopeState::Attack;
        env_counter = 0;
    }
    void deactivate()
    {
        envstate = EnvelopeState::Release;
        env_counter = 0;
    }

    // must accumulate into the buffer, precleared by the synth before processing the first voice
    void process(choc::buffer::ChannelArrayView<float> destBuf)
    {
        if (envstate == EnvelopeState::Idle)
            return;
        int safealgo =
            wrap_value<float>(0, basevalues.algo + modvalues.algo, (int)m_plugs.size() - 1);
        assert(safealgo >= 0 && safealgo < m_plugs.size() - 1);
        auto plug = m_plugs[safealgo].get();
        // set params, doesn't actually process audio
        plug->process(basevalues.x, basevalues.y);
        double totalvol = std::clamp(basevalues.volume + modvalues.volume, -96.0f, 0.0f);
        double gain = xenakios::decibelsToGain(totalvol);
        sst::basic_blocks::dsp::pan_laws::panmatrix_t panmat;
        double totalcutoff = std::clamp(basevalues.filtcutoff + modvalues.filtcutoff, 0.0f, 127.0f);
        double totalreson = std::clamp(basevalues.filtreson + modvalues.filtreson, 0.01f, 0.99f);
        filter.setCoeff(totalcutoff, totalreson, 1.0 / m_sr);

        double totalpan = reflect_value(0.0f, basevalues.pan + modvalues.pan, 1.0f);
        int attlensamples = env_attack * m_sr;
        int rellensamples = env_release * m_sr;
        int ftype = basevalues.filttype;
        for (size_t i = 0; i < destBuf.size.numFrames; ++i)
        {
            double smoothedgain = m_gain_smoother.process(gain);
            double smoothedpan = m_pan_smoother.process(totalpan);
            // does expensive calculation, so might want to use tables or something instead
            sst::basic_blocks::dsp::pan_laws::monoEqualPower(smoothedpan, panmat);
            float envgain = 0.0;
            if (envstate == EnvelopeState::Attack)
            {
                envgain = 1.0 / attlensamples * env_counter;
                ++env_counter;
                if (env_counter == attlensamples)
                {
                    envstate = EnvelopeState::Sustain;
                    env_counter = 0;
                }
            }
            if (envstate == EnvelopeState::Sustain)
            {
                envgain = 1.0f;
            }
            if (envstate == EnvelopeState::Release)
            {
                envgain = 1.0 - (1.0 / rellensamples * env_counter);
                ++env_counter;
                if (env_counter == rellensamples)
                {
                    env_counter = 0;
                    envstate = EnvelopeState::Idle;
                }
            }

            float out = plug->processGraph() * smoothedgain * envgain;
            float outL = panmat[0] * out;
            float outR = panmat[3] * out;

            dcblocker.step<StereoSimperSVF::HP>(dcblocker, outL, outR);

            if (ftype == 0)
                filter.step<StereoSimperSVF::LP>(filter, outL, outR);
            else if (ftype == 1)
                filter.step<StereoSimperSVF::HP>(filter, outL, outR);
            else if (ftype == 2)
                filter.step<StereoSimperSVF::BP>(filter, outL, outR);
            else if (ftype == 3)
                filter.step<StereoSimperSVF::PEAK>(filter, outL, outR);
            else if (ftype == 4)
                filter.step<StereoSimperSVF::NOTCH>(filter, outL, outR);
            else
                filter.step<StereoSimperSVF::ALL>(filter, outL, outR);
            destBuf.getSample(0, i) += outL;
            destBuf.getSample(1, i) += outR;
        }
    }
    int port_id = -1;
    int chan = -1;
    int key = -1;
    int note_id = -1;

  private:
    std::vector<std::unique_ptr<NoisePlethoraPlugin>> m_plugs;
    SignalSmoother m_gain_smoother;
    SignalSmoother m_pan_smoother;
    double m_sr = 0.0;
    StereoSimperSVF dcblocker;
    StereoSimperSVF filter;
};

class NoisePlethoraSynth
{
  public:
    NoisePlethoraVoice::VoiceParams voiceparams;
    NoisePlethoraSynth()
    {
        for (int i = 0; i < 8; ++i)
        {
            auto v = std::make_unique<NoisePlethoraVoice>();
            m_voices.push_back(std::move(v));
        }
    }
    void prepare(double sampleRate, int maxBlockSize)
    {
        m_sr = sampleRate;
        m_mix_buf = choc::buffer::ChannelArrayBuffer<float>(2, (unsigned int)maxBlockSize);
        for (auto &v : m_voices)
        {
            v->prepare(sampleRate);
        }
    }
    void applyParameterModulation(int port, int ch, int key, int note_id, clap_id parid, double amt)
    {
        for (auto &v : m_voices)
        {
            if ((key == -1 || v->key == key) && (note_id == -1 || v->note_id == note_id) &&
                (port == -1 || v->port_id == port) && (ch == -1 || v->chan == ch))
            {
                if (parid == (clap_id)ParamIDs::Volume)
                    v->modvalues.volume = amt;
                if (parid == (clap_id)ParamIDs::X)
                    v->modvalues.x = amt;
                if (parid == (clap_id)ParamIDs::Y)
                    v->modvalues.y = amt;
                if (parid == (clap_id)ParamIDs::Algo)
                    v->modvalues.algo = amt;
                if (parid == (clap_id)ParamIDs::Pan)
                    v->modvalues.pan = amt;
                if (parid == (clap_id)ParamIDs::FiltCutoff)
                    v->modvalues.filtcutoff = amt;
                if (parid == (clap_id)ParamIDs::FiltResonance)
                    v->modvalues.filtreson = amt;
            }
        }
    }
    void applyParameter(int port, int ch, int key, int note_id, clap_id parid, double value)
    {
        // std::cout << "par " << parid << " " << value << "\n";
        for (auto &v : m_voices)
        {
            if ((key == -1 || v->key == key) && (note_id == -1 || v->note_id == note_id) &&
                (port == -1 || v->port_id == port) && (ch == -1 || v->chan == ch))
            {
                // std::cout << "applying par " << parid << " to " << port << " " << ch << " " <<
                // key
                //           << " " << note_id << " " << value << "\n";
                if (parid == (clap_id)ParamIDs::Volume)
                    v->basevalues.volume = value;
                if (parid == (clap_id)ParamIDs::X)
                    v->basevalues.x = value;
                if (parid == (clap_id)ParamIDs::Y)
                    v->basevalues.y = value;
                if (parid == (clap_id)ParamIDs::Algo)
                    v->basevalues.algo = value;
                if (parid == (clap_id)ParamIDs::Pan)
                    v->basevalues.pan = value;
                if (parid == (clap_id)ParamIDs::FiltCutoff)
                    v->basevalues.filtcutoff = value;
                if (parid == (clap_id)ParamIDs::FiltResonance)
                    v->basevalues.filtreson = value;
                if (parid == (clap_id)ParamIDs::FiltType)
                    v->basevalues.filttype = value;
            }
        }
    }
    void startNote(int port, int ch, int key, int note_id, double velo)
    {
        for (auto &v : m_voices)
        {
            if (v->envstate == NoisePlethoraVoice::EnvelopeState::Idle)
            {
                std::cout << "activated " << ch << " " << key << " " << note_id << "\n";
                v->activate(port, ch, key, note_id, velo);
                return;
            }
        }
        std::cout << "could not activate " << ch << " " << key << " " << note_id << "\n";
    }
    void stopNote(int port, int ch, int key, int note_id, double velo)
    {
        for (auto &v : m_voices)
        {
            if (v->envstate != NoisePlethoraVoice::EnvelopeState::Idle && v->port_id == port &&
                v->chan == ch && v->key == key && v->note_id == note_id)
            {
                std::cout << "deactivated " << v->chan << " " << v->key << " " << v->note_id
                          << "\n";
                v->deactivate();
            }
        }
    }
    void processBlock(choc::buffer::ChannelArrayView<float> destBuf)
    {
        m_mix_buf.clear();
        for (int i = 0; i < m_voices.size(); ++i)
        {
            if (m_voices[i]->envstate != NoisePlethoraVoice::EnvelopeState::Idle)
            {
                m_voices[i]->process(m_mix_buf.getView());
            }
        }
        choc::buffer::applyGain(m_mix_buf, 0.5);
        choc::buffer::copy(destBuf, m_mix_buf);
    }

    enum class ParamIDs
    {
        Volume,
        X,
        Y,
        FiltCutoff,
        FiltResonance,
        FiltType,
        Algo,
        Pan
    };
    int m_polyphony = 1;
    double m_pan_spread = 0.0;
    double m_x_spread = 0.0;
    double m_y_spread = 0.0;
    NoisePlethoraVoice::VoiceParams renderparams;
    void showGUIBlocking(std::function<void(void)> clickhandler)
    {
        choc::ui::setWindowsDPIAwareness();
        choc::ui::DesktopWindow window({100, 100, (int)600, (int)300});
        choc::ui::WebView webview;
        window.setWindowTitle("Noise Plethora");
        window.setResizable(true);
        window.setMinimumSize(300, 300);
        window.setMaximumSize(1500, 1200);
        window.windowClosed = [this, clickhandler] { choc::messageloop::stop(); };
        webview.bind("onRenderClicked",
                     [clickhandler, this,
                      &webview](const choc::value::ValueView &args) -> choc::value::Value {
                         webview.evaluateJavascript(R"(
                            updateUI("Rendering...");
                            )");
                         clickhandler();
                         webview.evaluateJavascript(R"(
                            updateUI("Finished!");
                            )");
                         return choc::value::Value{};
                     });
        webview.bind("onSliderMoved",
                     [this](const choc::value::ValueView &args) -> choc::value::Value {
                         // note that things could get messed up here because the choc functions can
                         // throw exceptions, so we should maybe have a try catch block here...but
                         // we should just know this will work, really.
                         auto parid = args[0]["id"].get<int>();
                         auto value = args[0]["value"].get<double>();
                         // std::cout << "par " << parid << " changed to " << value << std::endl;
                         if (parid == 0)
                             renderparams.volume = value;
                         if (parid == 1)
                             renderparams.x = value;
                         if (parid == 2)
                             renderparams.y = value;
                         if (parid == 3)
                             renderparams.filtcutoff = value;
                         if (parid == 4)
                             renderparams.filtreson = value;
                         if (parid == 5)
                             renderparams.algo = value;
                         if (parid == 6)
                             renderparams.pan = value;
                         if (parid == 100)
                             m_polyphony = value;
                         if (parid == 101)
                             m_pan_spread = value;
                         if (parid == 200)
                             m_x_spread = value;
                         if (parid == 201)
                             m_y_spread = value;
                         return choc::value::Value{};
                     });
        auto html = choc::file::loadFileAsString(R"(C:\develop\AudioPluginHost_mk2\htmltest.html)");
        window.setContent(webview.getViewHandle());
        webview.setHTML(html);

        window.toFront();
        choc::messageloop::run();
    }

  private:
    std::vector<std::unique_ptr<NoisePlethoraVoice>> m_voices;
    choc::buffer::ChannelArrayBuffer<float> m_mix_buf;
    double m_sr = 0;
    int m_update_counter = 0;
    int m_update_len = 32;
};

class NoisePlethoraEngine
{
  public:
    NoisePlethoraEngine()
    {
        int k = 0;
        for (int i = 0; i < numBanks; ++i)
        {
            auto &bank = getBankForIndex(i);
            std::cout << "bank " << i << "\n";
            for (int j = 0; j < programsPerBank; ++j)
            {
                auto progname = bank.getProgramName(j);
                std::cout << "\t" << progname << "\t\t" << k << "\n";
                // availablePlugins[k] = bank.getProgramName(j);
                ++k;
                auto p = MyFactory::Instance()->Create(progname);
                m_plugs.push_back(std::move(p));
            }
        }
    }

  private:
    std::vector<std::unique_ptr<NoisePlethoraPlugin>> m_plugs;
};

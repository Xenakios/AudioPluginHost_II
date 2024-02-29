#pragma once

#include "../Plugins/noise-plethora/plugins/NoisePlethoraPlugin.hpp"
#include "../Plugins/noise-plethora/plugins/Banks.hpp"
#include "audio/choc_AudioFileFormat_WAV.h"
#include "sst/basic-blocks/dsp/PanLaws.h"
#include "sst/basic-blocks/modulators/ADSREnvelope.h"
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

struct SRProviderB
{
    static constexpr int BLOCK_SIZE = 32;
    static constexpr int BLOCK_SIZE_OS = BLOCK_SIZE * 2;
    SRProviderB() { initTables(); }
    alignas(32) float table_envrate_linear[512];
    double samplerate = 44100.0;
    void initTables()
    {
        double dsamplerate_os = samplerate * 2;
        for (int i = 0; i < 512; ++i)
        {
            double k =
                dsamplerate_os * pow(2.0, (((double)i - 256.0) / 16.0)) / (double)BLOCK_SIZE_OS;
            table_envrate_linear[i] = (float)(1.f / k);
        }
    }
    float envelope_rate_linear_nowrap(float x)
    {
        x *= 16.f;
        x += 256.f;
        int e = std::clamp<int>((int)x, 0, 0x1ff - 1);

        float a = x - (float)e;

        return (1 - a) * table_envrate_linear[e & 0x1ff] +
               a * table_envrate_linear[(e + 1) & 0x1ff];
    }
};

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
        float pan = 0.0f;
        float filttype = 0.0f;
    };
    VoiceParams basevalues;
    VoiceParams modvalues;
    float note_expr_pressure = 0.0f;
    float note_expr_pan = 0.0f;
    using EnvType = sst::basic_blocks::modulators::ADSREnvelope<SRProviderB, ENVBLOCKSIZE>;
    SRProviderB m_sr_provider;
    EnvType m_vol_env{&m_sr_provider};

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
        m_sr_provider.samplerate = sampleRate;
        m_sr_provider.initTables();
        m_sr = sampleRate;
        dcblocker.setCoeff(hipasscutoff, 0.01, 1.0 / m_sr);
        dcblocker.init();
        filter.init();
        m_gain_smoother.setSlope(0.999);
        m_pan_smoother.setSlope(0.999);
        float g = xenakios::decibelsToGain(basevalues.volume);
        for (int i = 0; i < 2048; ++i)
        {
            m_pan_smoother.process(basevalues.pan);
            m_gain_smoother.process(g);
        }
        for (auto &p : m_plugs)
        {
            p->init();
            p->m_sr = m_sr;
        }
    }
    double velocity = 1.0;
    bool m_eg_gate = false;
    bool m_voice_active = false;
    void activate(int port_, int chan_, int key_, int id_, double velo)
    {
        velocity = velo;
        port_id = port_;
        chan = chan_;
        key = key_;
        note_id = id_;

        m_voice_active = true;
        m_eg_gate = true;
        m_vol_env.attackFrom(0.0f, 0.0f, 0, true);
    }
    void deactivate() { m_eg_gate = false; }
    int m_update_counter = 0;
    float eg_attack = 0.4f;
    float eg_decay = 0.4f;
    float eg_sustain = 1.0f;
    float eg_release = 0.6f;
    std::function<void(int, int, int, int)> DeativatedVoiceCallback;
    // must accumulate into the buffer, precleared by the synth before processing the first voice
    void process(choc::buffer::ChannelArrayView<float> destBuf)
    {
        if (!m_voice_active)
            return;
        // when algo is modulated, we don't want to get stuck at the first or last algo
        int safealgo =
            wrap_value<float>(0, basevalues.algo + modvalues.algo, (int)m_plugs.size() - 1);
        assert(safealgo >= 0 && safealgo < m_plugs.size() - 1);
        auto plug = m_plugs[safealgo].get();

        float expr_x = xenakios::mapvalue(note_expr_pressure, 0.0f, 1.0f, -0.5f, 0.5f);
        float totalx = std::clamp(basevalues.x + modvalues.x + expr_x, 0.0f, 1.0f);
        float totaly = std::clamp(basevalues.y + modvalues.y, 0.0f, 1.0f);
        plug->process(totalx, totaly);
        float velodb = -18.0 + 18.0 * velocity;
        double totalvol = std::clamp(basevalues.volume + modvalues.volume + velodb, -96.0f, 0.0f);
        double gain = xenakios::decibelsToGain(totalvol);
        sst::basic_blocks::dsp::pan_laws::panmatrix_t panmat;
        double totalcutoff = std::clamp(basevalues.filtcutoff + modvalues.filtcutoff, 0.0f, 127.0f);
        double totalreson = std::clamp(basevalues.filtreson + modvalues.filtreson, 0.01f, 0.99f);
        filter.setCoeff(totalcutoff, totalreson, 1.0 / m_sr);
        float basepan = xenakios::mapvalue(basevalues.pan, -1.0f, 1.0f, 0.0f, 1.0f);
        float expr_pan = xenakios::mapvalue(note_expr_pan, 0.0f, 1.0f, -0.5f, 0.5f);
        double totalpan = reflect_value(0.0f, basepan + modvalues.pan + expr_pan, 1.0f);

        int ftype = basevalues.filttype;

        for (size_t i = 0; i < destBuf.size.numFrames; ++i)
        {
            if (m_update_counter == 0)
            {
                m_vol_env.processBlock(eg_attack, eg_decay, eg_sustain, eg_release, 1, 1, 1,
                                       m_eg_gate);
                if (m_vol_env.stage == EnvType::s_eoc)
                {
                    m_voice_active = false;
                    if (DeativatedVoiceCallback)
                        DeativatedVoiceCallback(port_id, chan, key, note_id);
                }
            }
            float envgain = m_vol_env.outputCache[m_update_counter];
            ++m_update_counter;
            if (m_update_counter == ENVBLOCKSIZE)
                m_update_counter = 0;
            double smoothedgain = m_gain_smoother.process(gain);
            double smoothedpan = m_pan_smoother.process(totalpan);
            // does expensive calculation, so might want to use tables or something instead
            sst::basic_blocks::dsp::pan_laws::monoEqualPower(smoothedpan, panmat);

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
    int port_id = 0;
    int chan = 0;
    int key = 0;
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
    static constexpr size_t maxNumVoices = 16;
    NoisePlethoraSynth()
    {
        deactivatedNotes.reserve(1024);
        for (size_t i = 0; i < maxNumVoices; ++i)
        {
            auto v = std::make_unique<NoisePlethoraVoice>();
            v->DeativatedVoiceCallback = [this](int port, int chan, int key, int noteid) {
                deactivatedNotes.emplace_back(port, chan, key, noteid);
            };
            m_voices.push_back(std::move(v));
        }
    }
    ~NoisePlethoraSynth() { std::cout << "voices left at synth dtor " << voicecount << "\n"; }

    void prepare(double sampleRate, int maxBlockSize)
    {
        m_sr = sampleRate;
        m_mix_buf = choc::buffer::ChannelArrayBuffer<float>(2, (unsigned int)maxBlockSize);
        for (auto &v : m_voices)
        {
            v->prepare(sampleRate);
        }
    }
    void applyNoteExpression(int port, int ch, int key, int note_id, int net, double amt)
    {
        for (auto &v : m_voices)
        {
            if ((key == -1 || v->key == key) && (note_id == -1 || v->note_id == note_id) &&
                (port == -1 || v->port_id == port) && (ch == -1 || v->chan == ch))
            {
                if (net == 6)
                    v->note_expr_pressure = amt;
                if (net == 1)
                    v->note_expr_pan = amt;
            }
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
                if (parid == (clap_id)ParamIDs::EGAttack)
                    v->eg_attack = value;
                if (parid == (clap_id)ParamIDs::EGDecay)
                    v->eg_decay = value;
                if (parid == (clap_id)ParamIDs::EGSustain)
                    v->eg_sustain = value;
                if (parid == (clap_id)ParamIDs::EGRelease)
                    v->eg_release = value;
            }
        }
    }
    std::vector<std::tuple<int, int, int, int>> deactivatedNotes;
    int voicecount = 0;
    void startNote(int port, int ch, int key, int note_id, double velo)
    {
        for (auto &v : m_voices)
        {
            if (!v->m_voice_active)
            {
                ++voicecount;
                // std::cout << "activated " << ch << " " << key << " " << note_id << "\n";
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
            if (v->port_id == port && v->chan == ch && v->key == key &&
                (note_id == -1 || v->note_id == note_id))
            {
                // std::cout << "deactivated " << v->chan << " " << v->key << " " << v->note_id
                //          << "\n";
                --voicecount;
                v->deactivate();
            }
        }
    }
    void processBlock(choc::buffer::ChannelArrayView<float> destBuf)
    {
        deactivatedNotes.clear();
        m_mix_buf.clear();
        for (size_t i = 0; i < m_voices.size(); ++i)
        {
            if (m_voices[i]->m_voice_active)
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
        Pan,
        EGAttack,
        EGDecay,
        EGSustain,
        EGRelease
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
    std::vector<std::unique_ptr<NoisePlethoraVoice>> m_voices;

  private:
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

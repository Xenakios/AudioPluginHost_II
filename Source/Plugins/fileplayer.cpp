#include "clap/clap.h"
#include "clap/helpers/plugin.hh"
#include "clap/helpers/plugin.hxx"
#include "clap/helpers/host-proxy.hh"
#include "clap/helpers/host-proxy.hxx"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "gui/choc_WebView.h"
#include "containers/choc_SingleReaderSingleWriterFIFO.h"
#include "signalsmith-stretch.h"
#include "sst/basic-blocks/dsp/LanczosResampler.h"
#include "audio/choc_AudioFileFormat_WAV.h"
#include "audio/choc_AudioFileFormat_FLAC.h"
#include "audio/choc_AudioFileFormat_MP3.h"
#include "audio/choc_AudioFileFormat_Ogg.h"
#include "../Experimental/xap_utils.h"

using ParamDesc = sst::basic_blocks::params::ParamMetaData;

struct xen_fileplayer : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                                     clap::helpers::CheckingLevel::Maximal>
{
    enum class ParamIDs
    {
        Volume = 90,
        Playrate = 1001,
        Pitch = 44,
        StretchMode = 2022,
        LoopStart = 9999,
        LoopEnd = 8888
    };
    static constexpr size_t numParams = 6;
    float paramValues[numParams];
    std::unordered_map<clap_id, float *> idToParPtrMap;
    std::vector<ParamDesc> paramDescs;
    choc::audio::AudioFileFormatList fmtList;
    choc::audio::AudioFileProperties fileProps;
    choc::buffer::ChannelArrayBuffer<float> fileBuffer;
    sst::basic_blocks::dsp::LanczosResampler<128> m_lanczos{44100.0f, 44100.0f};
    void loadAudioFile(std::string path)
    {
        auto reader = fmtList.createReader(path);
        if (reader)
        {
            auto props = reader->getProperties();
            choc::buffer::ChannelArrayBuffer<float> readbuf(
                choc::buffer::Size(props.numChannels, props.numFrames));
            if (reader->readFrames(0, readbuf.getView()))
            {
                // only for initial testing, this is not thread safe etc
                fileProps = props;
                fileBuffer = readbuf;
            }
        }
    }
    xen_fileplayer(const clap_host *host, const clap_plugin_descriptor *desc)
        : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                clap::helpers::CheckingLevel::Maximal>(desc, host)
    {
        fmtList.addFormat(std::make_unique<choc::audio::WAVAudioFileFormat<false>>());
        loadAudioFile(R"(C:\MusicAudio\sourcesamples\count.wav)");
        paramDescs.push_back(ParamDesc()
                                 .asDecibel()
                                 .withRange(-48.0, 6.0)
                                 .withDefault(-6.0)
                                 .withName("Volume")
                                 .withFlags(CLAP_PARAM_IS_AUTOMATABLE)
                                 .withID((clap_id)ParamIDs::Volume));
        paramDescs.push_back(
            ParamDesc()
                .withUnorderedMapFormatting({{0, "Resample"}, {1, "Spectral"}, {2, "Granular"}},
                                            true)
                .withDefault(0.0)

                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE |
                           CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID | CLAP_PARAM_IS_STEPPED)
                .withName("Playback mode")
                .withID((clap_id)ParamIDs::StretchMode));
        paramDescs.push_back(ParamDesc()
                                 .asFloat()
                                 .withRange(-3.0f, 2.0f)
                                 .withDefault(0.0)
                                 .withATwoToTheBFormatting(1, 1, "x")
                                 .withDecimalPlaces(3)
                                 .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                                 .withName("Playrate")
                                 .withID((clap_id)ParamIDs::Playrate));
        for (size_t i = 0; i < numParams; ++i)
            paramValues[i] = 0.0f;
        for (size_t i = 0; i < paramDescs.size(); ++i)
        {
            idToParPtrMap[paramDescs[i].id] = &paramValues[i];
            *idToParPtrMap[paramDescs[i].id] = paramDescs[i].defaultVal;
        }
    }
    double outSr = 44100.0;
    bool activate(double sampleRate_, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        outSr = sampleRate_;
        return true;
    }
    bool implementsParams() const noexcept override { return true; }
    bool isValidParamId(clap_id paramId) const noexcept override
    {
        if (idToParPtrMap.count(paramId))
            return true;
        return false;
    }
    uint32_t paramsCount() const noexcept override { return paramDescs.size(); }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        if (paramIndex >= paramDescs.size())
            return false;
        paramDescs[paramIndex].toClapParamInfo<CLAP_NAME_SIZE>(info);
        return true;
    }
    bool paramsValue(clap_id paramId, double *value) noexcept override
    {
        if (value)
        {
            auto it = idToParPtrMap.find(paramId);
            if (it != idToParPtrMap.end())
            {
                *value = *it->second;
                return true;
            }
        }
        return false;
    }
    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override
    {
        for (auto &e : paramDescs)
        {
            if (e.id == paramId)
            {
                auto s = e.valueToString(value);
                if (s)
                {
                    strcpy(display, s->c_str());
                    return true;
                }
            }
        }
        return false;
    }
    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool isInput) const noexcept override
    {
        if (!isInput)
            return 1;
        return 0;
    }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override
    {
        // port id can be a "random" number
        info->id = isInput ? 2112 : 90210;
        strncpy(info->name, "main", sizeof(info->name));
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        return true;
    }
    clap_process_status process(const clap_process *process) noexcept override
    {
        auto frameCount = process->frames_count;
        float *op[2];
        op[0] = &process->audio_outputs->data32[0][0];
        op[1] = &process->audio_outputs->data32[1][0];
        if (fileProps.numChannels == 0)
        {
            for (int i = 0; i < frameCount; ++i)
            {
                op[0][i] = 0.0f;
                op[1][i] = 0.0f;
            }
            return CLAP_PROCESS_CONTINUE;
        }
        auto inEvents = process->in_events;
        auto inEventsSize = inEvents->size(inEvents);
        double m_loop_start = 0.0;
        double m_loop_end = 1.0;
        int loop_start_samples = m_loop_start * fileProps.numFrames;
        int loop_end_samples = m_loop_end * fileProps.numFrames;
        if (loop_start_samples > loop_end_samples)
            std::swap(loop_start_samples, loop_end_samples);
        if (loop_start_samples == loop_end_samples)
        {
            loop_end_samples += 4100;
            if (loop_end_samples >= fileProps.numFrames)
            {
                loop_start_samples = fileProps.numFrames - 4100;
                loop_end_samples = fileProps.numFrames - 1;
            }
        }
        if (m_buf_playpos < loop_start_samples)
            m_buf_playpos = loop_start_samples;
        if (m_buf_playpos >= loop_end_samples)
            m_buf_playpos = loop_start_samples;
        float compensrate = fileProps.sampleRate / outSr;

        // time octaves
        double rate = *idToParPtrMap[(clap_id)ParamIDs::Playrate];
        double m_rate_mod = 0.0;
        rate += m_rate_mod;
        // we could allow modulation to make it go a bit over these limits...
        rate = std::clamp(rate, -3.0, 2.0);
        // then convert to actual playback ratio
        rate = std::pow(2.0, rate);
        int xfadelensamples = 2048;
        auto getxfadedsample = [](const float *srcbuf, int index, int start, int end,
                                  int xfadelen) {
            // not within xfade region so just return original sample
            int xfadestart = end - xfadelen;
            if (index >= start && index < xfadestart)
                return srcbuf[index];

            float xfadegain = xenakios::mapvalue<float>(index, xfadestart, end - 1, 1.0f, 0.0f);
            assert(xfadegain >= 0.0f && xfadegain <= 1.0);
            float s0 = srcbuf[index];
            int temp = index - xfadestart + (start - xfadelen);
            if (temp < 0)
                return s0 * xfadegain;
            assert(temp >= 0 && temp < end);
            float s1 = srcbuf[temp];
            return s0 * xfadegain + s1 * (1.0f - xfadegain);
        };
        int playmode = *idToParPtrMap[(clap_id)ParamIDs::StretchMode];
        if (playmode == 0)
        {
            
        }
        return CLAP_PROCESS_CONTINUE;
    }
    int64_t m_buf_playpos = 0;
};

const char *features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, nullptr};
clap_plugin_descriptor desc = {CLAP_VERSION,
                               "com.xenakios.fileplayer",
                               "xenakios fileplayer",
                               "Unsupported",
                               "https://xenakios.com",
                               "",
                               "",
                               "0.0.0",
                               "xenakios filerplayer",
                               features};

static const clap_plugin *clap_create_plugin(const clap_plugin_factory *f, const clap_host *host,
                                             const char *plugin_id)
{
    // I know it looks like a leak right? but the clap-plugin-helpers basically
    // take ownership and destroy the wrapper when the host destroys the
    // underlying plugin (look at Plugin<h, l>::clapDestroy if you don't believe me!)
    auto wr = new xen_fileplayer(host, &desc);
    return wr->clapPlugin();
}

uint32_t get_plugin_count(const struct clap_plugin_factory *factory) { return 1; }

const clap_plugin_descriptor *get_plugin_descriptor(const clap_plugin_factory *f, uint32_t w)
{
    return &desc;
}

const CLAP_EXPORT struct clap_plugin_factory the_factory = {
    get_plugin_count,
    get_plugin_descriptor,
    clap_create_plugin,
};

static const void *get_factory(const char *factory_id) { return &the_factory; }

// clap_init and clap_deinit are required to be fast, but we have nothing we need to do here
bool clap_init(const char *p) { return true; }

void clap_deinit() {}

extern "C"
{

    // clang-format off
const CLAP_EXPORT struct clap_plugin_entry clap_entry = {
        CLAP_VERSION,
        clap_init,
        clap_deinit,
        get_factory
};
    // clang-format on
}

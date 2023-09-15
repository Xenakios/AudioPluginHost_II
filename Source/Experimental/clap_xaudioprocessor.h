#pragma once

#include "xaudioprocessor.h"
#include "platform/choc_DynamicLibrary.h"

inline const void *my_get_extension(const struct clap_host *host, const char *eid)
{
    return nullptr;
}

inline void my_host_par_rescan(const clap_host_t *host, clap_param_rescan_flags flags)
{
    // auto cp = (ClapProcessor*)host->host_data;
    // cp->updateParameterInfos();
    std::cout << "plugin parameters changed, host should rescan\n";
}

inline void my_host_par_clear(const clap_host_t *host, clap_id param_id,
                              clap_param_clear_flags flags)
{
}

inline void my_host_par_request_flush(const clap_host_t *host) {}

inline void request_restart(const struct clap_host *) {}

inline void request_process(const struct clap_host *) {}

inline void request_callback_nop(const struct clap_host *)
{
    std::cout << "nop request_callback\n";
}

class ClapPluginFormatProcessor : public xenakios::XAudioProcessor
{
    choc::file::DynamicLibrary m_plugdll;
    clap_plugin_entry_t *m_entry = nullptr;
    const clap_plugin_t *m_plug = nullptr;
    clap_host xen_host_info{CLAP_VERSION_INIT, nullptr, "XAPHOST", "Xenakios", "no website",
                            "0.0.0",           nullptr, nullptr,   nullptr,    nullptr};
    std::atomic<bool> m_inited{false};
    clap_plugin_params_t *m_ext_params = nullptr;
    bool m_processingStarted = false;
    std::atomic<bool> m_activated{false};

  public:
    std::vector<clap_param_info> m_param_infos;
    bool getDescriptor(clap_plugin_descriptor *desc) const override
    { 
        if (!m_plug)
            return false; 
        *desc = *m_plug->desc;
        return true;
    }
    ClapPluginFormatProcessor(std::string plugfilename, int plugindex) : m_plugdll(plugfilename)
    {
        xen_host_info.host_data = this;
        xen_host_info.get_extension = my_get_extension;
        xen_host_info.request_callback = request_callback_nop;
        xen_host_info.request_process = request_process;
        xen_host_info.request_restart = request_restart;
        if (m_plugdll.handle)
        {
            clap_plugin_entry_t *entry =
                (clap_plugin_entry_t *)m_plugdll.findFunction("clap_entry");
            if (entry)
            {
                m_entry = entry;
                entry->init(plugfilename.c_str());
                auto fac = (clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
                auto plugin_count = fac->get_plugin_count(fac);
                if (plugin_count <= 0)
                {
                    std::cout << "no plugins to manufacture\n";
                    return;
                }
                auto desc = fac->get_plugin_descriptor(fac, plugindex);
                if (desc)
                {
                    std::cout << desc->name << "\n";
                }
                auto plug = fac->create_plugin(fac, &xen_host_info, desc->id);
                if (plug)
                {
                    m_plug = plug;
                    m_plug->init(m_plug);
                    m_inited = true;
                    initParamsExtension();
                }
                else
                    std::cout << "could not create clap plugin instance\n";
            }
            else
                std::cout << "could not get clap plugin entry point function\n";
        }
        else
            std::cout << "could not load clap dll " << plugfilename << "\n";
    }
    ~ClapPluginFormatProcessor() override
    {
        if (m_plug)
        {
            if (m_activated.load())
                m_plug->deactivate(m_plug);
            m_plug->destroy(m_plug);
            m_plug = nullptr;
        }
        if (m_entry)
        {
            m_entry->deinit();
        }
    }
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        if (m_plug)
        {
            if (m_plug->activate(m_plug, sampleRate, minFrameCount, maxFrameCount))
            {
                m_activated = true;
                initParamsExtension();
                m_ext_audio_ports =
                    (clap_plugin_audio_ports *)m_plug->get_extension(m_plug, CLAP_EXT_AUDIO_PORTS);
                return true;
            }
        }
        return false;
    }
    void initParamsExtension()
    {
        if (m_plug)
        {
            if (!m_ext_params)
            {
                m_ext_params =
                    (clap_plugin_params_t *)m_plug->get_extension(m_plug, CLAP_EXT_PARAMS);
            }
        }
    }
    uint32_t paramsCount() const noexcept override { return m_ext_params->count(m_plug); }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        return m_ext_params->get_info(m_plug, paramIndex, info);
    }

    clap_plugin_audio_ports *m_ext_audio_ports = nullptr;
    uint32_t audioPortsCount(bool isInput) const noexcept override
    {
        if (!m_ext_audio_ports)
            return 0;
        return m_ext_audio_ports->count(m_plug, isInput);
    }

    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override
    {
        if (!m_ext_audio_ports)
            return false;
        return m_ext_audio_ports->get(m_plug, index, isInput, info);
    }

    clap_process_status process(const clap_process *process) noexcept override
    {
        if (!m_processingStarted)
        {
            m_plug->start_processing(m_plug);
            m_processingStarted = true;
        }
        return m_plug->process(m_plug, process);
    }

    // bool hasEditor() noexcept override { return false; }
    // xenakios::XAudioProcessorEditor *createEditorIfNeeded() noexcept { return nullptr; }
    // virtual XAudioProcessorEditor *createEditor() noexcept { return nullptr; }
};

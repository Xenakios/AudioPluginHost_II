#include "clap_xaudioprocessor.h"

bool ClapPluginFormatProcessor::activate(double sampleRate, uint32_t minFrameCount,
                                         uint32_t maxFrameCount) noexcept
{
    m_host_ext =
        static_cast<IHostExtension *>(GetHostExtension("com.xenakios.xupic-test-extension"));
    if (m_plug)
    {
        if (m_plug->activate(m_plug, sampleRate, minFrameCount, maxFrameCount))
        {

            m_activated = true;
            initParamsExtension();
            m_ext_audio_ports =
                (clap_plugin_audio_ports *)m_plug->get_extension(m_plug, CLAP_EXT_AUDIO_PORTS);
            m_ext_note_ports =
                (clap_plugin_note_ports *)m_plug->get_extension(m_plug, CLAP_EXT_NOTE_PORTS);
            m_ext_state = (clap_plugin_state *)m_plug->get_extension(m_plug, CLAP_EXT_STATE);
            m_ext_plugin_tail = (clap_plugin_tail *)m_plug->get_extension(m_plug, CLAP_EXT_TAIL);
            m_ext_remote_controls = (clap_plugin_remote_controls *)m_plug->get_extension(
                m_plug, CLAP_EXT_REMOTE_CONTROLS);
            
            return true;
        }
    }
    return false;
}

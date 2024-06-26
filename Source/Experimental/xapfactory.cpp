#include "xapfactory.h"
#include <iostream>
#include "platform/choc_DynamicLibrary.h"
#include "xaps/clap_xaudioprocessor.h"
#if !NOJUCE
#include "xaps/juce_xaudioprocessor.h"
#endif

namespace xenakios
{

XapFactory::XapFactory() {}

void XapFactory::scanClapPlugin(const std::filesystem::path &path)
{
    auto pathstr = path.generic_string();
    choc::file::DynamicLibrary dll(pathstr);
    if (dll.handle)
    {
        clap_plugin_entry_t *entry = (clap_plugin_entry_t *)dll.findFunction("clap_entry");
        if (entry)
        {
            entry->init(pathstr.c_str());
            auto fac = (clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
            auto plugin_count = fac->get_plugin_count(fac);
            if (plugin_count <= 0)
            {
                std::cout << "no plugins to manufacture\n";
                return;
            }
            auto plugcount = fac->get_plugin_count(fac);
            for (int i = 0; i < plugcount; ++i)
            {
                auto desc = fac->get_plugin_descriptor(fac, i);
                if (desc)
                {
                    // std::cout << "\t" << desc->name << "\n";
                    std::string procid;
                    if (desc->id)
                        procid = desc->id;
                    m_entries.emplace_back(desc->name, procid, [pathstr, i]() {
                        return std::make_unique<ClapPluginFormatProcessor>(pathstr, i);
                    });
                    m_entries.back().manufacturer = desc->vendor;
                    m_entries.back().proctype = "CLAP";
                }
            }
            entry->deinit();
        }
        else
            std::cout << "no entry point\n";
    }
    else
        std::cout << "could not open dll for " << path.generic_string();
}

void XapFactory::scanClapPlugins()
{
    // actually there are more paths we should scan...
    std::filesystem::recursive_directory_iterator dirit(R"(C:\Program Files\Common Files\CLAP)");
    for (auto &f : dirit)
    {
        if (f.is_regular_file() && f.path().extension() == ".clap")
        {
            // std::cout << f.path() << "\n";
            scanClapPlugin(f.path());
        }
    }
}

#if !NOJUCE
void XapFactory::scanVST3Plugins()
{
    std::vector<std::string> vst3plugins;
    vst3plugins.push_back(R"(C:\Program Files\Common Files\VST3\ValhallaDelay.vst3)");
    vst3plugins.push_back(R"(C:\Program Files\Common Files\VST3\ValhallaSupermassive.vst3)");
    vst3plugins.push_back(R"(C:\Program Files\Common Files\VST3\ValhallaRoom.vst3)");
    vst3plugins.push_back(R"(C:\Program Files\Common Files\VST3\ValhallaVintageVerb.vst3)");
    vst3plugins.push_back(
        R"(C:\Program Files\Common Files\VST3\mda-vst3.vst3\Contents\x86_64-win\mda-vst3.vst3)");

    juce::AudioPluginFormatManager plugmana;
    juce::VST3PluginFormat f;
    plugmana.addDefaultFormats();
    juce::KnownPluginList klist;

    for (auto &pfile : vst3plugins)
    {
        juce::OwnedArray<juce::PluginDescription> typesFound;
        klist.scanAndAddFile(pfile, true, typesFound, f);
        for (int i = 0; i < typesFound.size(); ++i)
        {
            std::string procid = "vst3." + std::to_string(i) + "." +
                                 std::to_string(typesFound[i]->uniqueId) + "." +
                                 typesFound[i]->name.toStdString();
            m_entries.emplace_back(typesFound[i]->name.toStdString(), procid, [i, pfile] {
                return std::make_unique<JucePluginWrapper>(pfile, i);
            });
            m_entries.back().manufacturer = typesFound[i]->manufacturerName.toStdString();
            m_entries.back().proctype = "VST3";
        }
    }
}
#elif
void XapFactory::scanVST3Plugins() {}
#endif

} // namespace xenakios

#include "xapfactory.h"
#include <iostream>
#include "platform/choc_DynamicLibrary.h"
#include "clap_xaudioprocessor.h"

namespace xenakios
{

XapFactory::XapFactory() { scanClapPlugins(); }

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
                    std::cout << "\t" << desc->name << "\n";
                    m_entries.emplace_back(desc->name, "CLAP", [this, pathstr, i]() {
                        return std::make_unique<ClapPluginFormatProcessor>(pathstr, i);
                    });
                    m_entries.back().manufacturer = desc->vendor;
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
    std::filesystem::recursive_directory_iterator dirit(R"(C:\Program Files\Common Files\CLAP)");
    for (auto &f : dirit)
    {
        if (f.is_regular_file() && f.path().extension() == ".clap")
        {
            std::cout << f.path() << "\n";
            scanClapPlugin(f.path());
        }
    }
}

} // namespace xenakios

#include "xapfactory.h"
#include <filesystem>
#include <iostream>

namespace xenakios
{

XapFactory::XapFactory()
{
    scanClapPlugins();
}
void XapFactory::scanClapPlugins() 
{
    std::filesystem::recursive_directory_iterator dirit(R"(C:\Program Files\Common Files\CLAP)");
    for(auto& f : dirit)
    {
        if (f.is_regular_file() && f.path().extension() == ".clap")
        {
            std::cout << f.path() << "\n";
        }
    }
}

} // namespace xenakios

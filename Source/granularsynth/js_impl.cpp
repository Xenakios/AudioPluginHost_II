#include <vector>
#include <string>
#include "javascript/choc_javascript_QuickJS.h"
#include "javascript/choc_javascript.h"
#include <print>
#include <algorithm>

std::vector<float> generate_from_js(std::string jscode)
{
    static choc::javascript::Context ctx;
    if (!ctx)
    {
        ctx = choc::javascript::createQuickJSContext();
    }
    std::vector<float> result;
    try
    {
        ctx.run(jscode);
        auto r = ctx.invoke("generate_steps");
        if (r.isArray())
        {
            result.resize(std::min((int)r.size(), 16384));
            for (int i = 0; i < result.size(); ++i)
            {
                result[(size_t)i] = std::clamp(r[i].getWithDefault(0.0f), -1.0f, 1.0f);
            }
        }
    }
    catch (std::exception &ex)
    {
        std::print("{}\n", ex.what());
    }
    return result;
}

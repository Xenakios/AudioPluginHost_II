#include <vector>
#include <string>
#include "javascript/choc_javascript_QuickJS.h"
#include "javascript/choc_javascript.h"
#include <print>
#include <algorithm>
#include <thread>

void test_interrupt_support()
{
    try
    {
        auto ctx = choc::javascript::createQuickJSContext();
        auto t0 = std::chrono::high_resolution_clock::now();
        std::atomic<bool> finished{false};
        std::thread jsthread([&ctx, t0, &finished]() {
            try
            {
                ctx.run(R"(
            function foo(x)
            {
              s = 0.0;
              for (var i=0;i<10000000;++i)
                s = s + i * x;
              return s;
            })");
                auto r = ctx.invoke("foo", 666.0);
                auto t1 = std::chrono::high_resolution_clock::now();
                auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                std::print("script took {} ms to run\n", diff);
                std::print("{}\n", r.getWithDefault(0.0f));
                finished.store(true);
            }
            catch (std::exception &ex)
            {
                std::print("{}\n", ex.what());
            }
        });
        while (true)
        {
            if (finished.load())
                break;
            auto t1 = std::chrono::high_resolution_clock::now();
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            if (diff > 5000)
            {
                std::print("js took too long, cancelling...\n");
                ctx.cancel();
                break;
            }
            Sleep(10);
        }
        jsthread.join();
    }
    catch (std::exception &ex)
    {
        std::print("exception running js code : {}\n", ex.what());
    }
}

choc::javascript::Context g_jsctx;

void init_step_sequencer_js()
{
    if (!g_jsctx)
    {
        g_jsctx = choc::javascript::createQuickJSContext();
    }
}

void deinit_step_sequencer_js()
{
    if (g_jsctx)
    {
        g_jsctx = choc::javascript::Context{};
    }
}

const char *test_script = R"(
function generate_steps(steps, startstep, endstep)
{
    for (var i=startstep;i<endstep;i++)
    {
        steps[i] = -1.0+2.0*Math.random();
    }
    return steps;
}
)";

std::vector<float> generate_from_js(std::string jscode)
{
    assert(g_jsctx);
    std::vector<float> result;
    try
    {
        g_jsctx.run(test_script);
        auto dest_arr = choc::value::createArray(16, [](uint32_t index) { return 0.5f; });
        auto r = g_jsctx.invoke("generate_steps", dest_arr, 0, 16);

        result.resize(r.size());
        for (int i = 0; i < result.size(); ++i)
        {
            result[(size_t)i] = std::clamp(r[i].getWithDefault(0.0f), -1.0f, 1.0f);
        }
    }
    catch (std::exception &ex)
    {
        std::print("{}\n", ex.what());
    }
    return result;
}

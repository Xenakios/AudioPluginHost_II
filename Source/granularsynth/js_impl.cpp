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
        g_jsctx.registerFunction("sleep", [](choc::javascript::ArgumentList args) {
            auto ms = args.get(0, 0);
            if (ms > 0 && ms < 10001)
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            return choc::value::Value();
        });
    }
}

void deinit_step_sequencer_js()
{
    if (g_jsctx)
    {
        g_jsctx = choc::javascript::Context{};
    }
}

void cancel_js()
{
    assert(g_jsctx);
    g_jsctx.cancel();
}

std::vector<float> generate_from_js(std::string jscode, std::vector<float> currentsteps,
                                    int startstep, int endstep)
{
    assert(g_jsctx);

    // try
    {
        g_jsctx.run(jscode);
        if (startstep >= endstep)
            std::swap(startstep, endstep);
        // std::print("gnerating steps with js {}\n", startstep);
        auto dest_arr = choc::value::createArray(
            currentsteps.size(), [&currentsteps](uint32_t index) { return currentsteps[index]; });
        auto r = g_jsctx.invoke("generate_steps", dest_arr, startstep, endstep);

        for (int i = 0; i < r.size(); ++i)
        {
            currentsteps[(size_t)i] = std::clamp(r[i].getWithDefault(0.0f), -1.0f, 1.0f);
        }
    }
    // catch (std::exception &ex)
    {
        // std::print("{}\n", ex.what());
    }
    return currentsteps;
}

#pragma once
#include <functional>
#include <memory>
#include "xaudioprocessor.h"

namespace xenakios
{
using CreationFunc = std::function<std::unique_ptr<XAudioProcessor>()>;

class XapFactory
{
  private:
    XapFactory();
    void scanClapPlugins();
  public:
    static XapFactory &getInstance()
    {
        static XapFactory* fact = nullptr;
        if (!fact)
            fact = new XapFactory;
        return *fact;
    }
    void registerEntry(std::string name, CreationFunc createFunc)
    {
        m_entries.emplace_back(name, createFunc);
    }
    std::unique_ptr<XAudioProcessor> createFromName(std::string name)
    {
        for (auto &e : m_entries)
        {
            if (e.name == name)
                return e.createfunc();
        }
        return nullptr;
    }
    struct Entry
    {
        Entry() {}
        Entry(std::string name_, CreationFunc func_) : name(name_), createfunc(func_) {}
        std::string name;
        CreationFunc createfunc;
    };
    std::vector<Entry> m_entries;
};

class RegisterXap
{
  public:
    RegisterXap(std::string name, CreationFunc createFunc)
    {
        XapFactory::getInstance().registerEntry(name, createFunc);
    }
};
} // namespace xenakios

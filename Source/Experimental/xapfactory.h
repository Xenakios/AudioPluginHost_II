#pragma once
#include <functional>
#include <memory>
#include "xaudioprocessor.h"
#include <filesystem>

namespace xenakios
{
using CreationFunc = std::function<std::unique_ptr<XAudioProcessor>()>;

class XapFactory
{
  private:
    XapFactory();

    void scanClapPlugin(const std::filesystem::path &path);

  public:
    void scanClapPlugins();
    void scanVST3Plugins();
    static XapFactory &getInstance()
    {
        static XapFactory *fact = nullptr;
        if (!fact)
            fact = new XapFactory;
        return *fact;
    }
    void registerEntry(std::string name, std::string proctype, CreationFunc createFunc)
    {
        m_entries.emplace_back(name, proctype, createFunc);
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
        Entry(std::string name_, std::string proctype_, CreationFunc func_)
            : name(name_), proctype(proctype_), createfunc(func_)
        {
        }
        std::string name;
        std::string proctype;
        std::string manufacturer;
        CreationFunc createfunc;
    };
    std::vector<Entry> m_entries;
};

class RegisterXap
{
  public:
    RegisterXap(std::string name, std::string proctype, CreationFunc createFunc)
    {
        XapFactory::getInstance().registerEntry(name, proctype, createFunc);
    }
};
} // namespace xenakios

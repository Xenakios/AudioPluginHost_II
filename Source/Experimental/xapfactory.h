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
    void registerEntry(std::string name, std::string procid, CreationFunc createFunc)
    {
        m_entries.emplace_back(name, procid, createFunc);
        m_entries.back().proctype = "Internal";
        m_entries.back().manufacturer = "Xenakios";
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
    std::unique_ptr<XAudioProcessor> createFromID(std::string id)
    {
        for (auto &e : m_entries)
        {
            if (e.procid == id)
                return e.createfunc();
        }
        return nullptr;
    }
    struct Entry
    {
        Entry() {}
        Entry(std::string name_, std::string procid_, CreationFunc func_)
            : name(name_), procid(procid_), createfunc(func_)
        {
        }
        std::string name;
        std::string proctype;
        std::string manufacturer;
        std::string procid;
        CreationFunc createfunc;
    };
    std::vector<Entry> m_entries;
};

class RegisterXap
{
  public:
    RegisterXap(std::string name, std::string procid, CreationFunc createFunc)
    {
        XapFactory::getInstance().registerEntry(name, procid, createFunc);
    }
};
} // namespace xenakios

#include "fileplayer_xaudioprocessor.h"

class FilePlayerEditor : public juce::Component, public juce::Timer
{
  public:
    std::unique_ptr<juce::FileChooser> m_file_chooser;
    FilePlayerEditor(FilePlayerProcessor *proc) : m_proc(proc)
    {
        addAndMakeVisible(m_load_but);
        addAndMakeVisible(m_file_label);
        m_load_but.setButtonText("Load file...");
        m_load_but.onClick = [this]() { showFileOpenDialog(); };
        startTimer(100);
    }
    // message to processor doesn't own the file name string so need to keep it alive here
    std::string file_to_change_to;
    void showFileOpenDialog()
    {
        m_file_chooser =
            std::make_unique<juce::FileChooser>("Choose audio file", juce::File(), "*.wav", true);
        m_file_chooser->launchAsync(
            juce::FileBrowserComponent::FileChooserFlags::openMode |
                juce::FileBrowserComponent::FileChooserFlags::canSelectFiles,
            [this](const juce::FileChooser &chooser) {
                if (chooser.getResult() != juce::File())
                {
                    m_proc->importFile(chooser.getResult());
                    // file_to_change_to = chooser.getResult().getFullPathName().toStdString();
                    // FilePlayerProcessor::FilePlayerMessage msg;
                    // msg.opcode =
                    // FilePlayerProcessor::FilePlayerMessage::Opcode::RequestFileChange;
                    // msg.filename = file_to_change_to;
                    // m_proc->messages_from_ui.push(msg);
                }
            });
    }
    void timerCallback() override
    {
        FilePlayerProcessor::FilePlayerMessage msg;
        while (m_proc->messages_to_ui.pop(msg))
        {
            if (msg.opcode == FilePlayerProcessor::FilePlayerMessage::Opcode::FileChanged)
            {
                if (msg.filename.data())
                    m_file_label.setText(msg.filename.data(), juce::dontSendNotification);
            }
        }
    }
    void resized() override
    {
        m_load_but.setBounds(0, 150, 100, 25);
        m_file_label.setBounds(102, 150, 300, 25);
    }

  private:
    FilePlayerProcessor *m_proc = nullptr;
    juce::TextButton m_load_but;
    juce::Label m_file_label;
};

bool FilePlayerProcessor::guiCreate(const char *api, bool isFloating) noexcept
{
    m_editor = std::make_unique<FilePlayerEditor>(this);
    m_editor->setSize(600, 500);
    return true;
}

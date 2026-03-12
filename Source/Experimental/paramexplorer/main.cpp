#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>

inline juce::Colour getHeatmapColour(float v)
{
    // Clamp to be safe after the pow()
    v = juce::jlimit(0.0f, 1.0f, v);

    // Simple 3-stage gradient: Black -> Blue -> Red -> Yellow
    if (v < 0.33f)
        return juce::Colours::black.interpolatedWith(juce::Colours::blue, v / 0.33f);
    if (v < 0.66f)
        return juce::Colours::blue.interpolatedWith(juce::Colours::red, (v - 0.33f) / 0.33f);

    return juce::Colours::red.interpolatedWith(juce::Colours::yellow, (v - 0.66f) / 0.34f);
}

class SpectroGram
{
  public:
    std::unique_ptr<juce::dsp::FFT> fft;
    juce::AudioBuffer<float> insigbuf;
    int hopsize = 0;
    double insigsr = 0.0;
    std::vector<float> fftbuffer;
    double curmindb = -60.0;
    double curgain = 0.0;
    void setFFTSizeSamples(size_t sz)
    {
        size_t order = std::ceil(std::log2(sz));
        fft = std::make_unique<juce::dsp::FFT>(order);
        const int fftsize = fft->getSize();
        fftbuffer.resize(fftsize * 2);
        hopsize = fftsize / 2;
        img = juce::Image(juce::Image::ARGB, insigbuf.getNumSamples() / hopsize, fftsize / 2, true);
        generate();
    }
    SpectroGram()
    {

        juce::AudioFormatManager mana;
        mana.registerBasicFormats();
        auto filename = R"(C:\MusicAudio\sourcesamples\_count.wav)";
        auto reader = mana.createReaderFor(juce::File(filename));
        insigbuf.setSize(1, reader->lengthInSamples);
        setFFTSizeSamples(2048);
        insigsr = reader->sampleRate;
        reader->read(&insigbuf, 0, reader->lengthInSamples, 0, true, true);
        delete reader;
    }

    void generate()
    {
        double mindisplaydb = curmindb;
        double gain = juce::Decibels::decibelsToGain(curgain);
        img.clear(img.getBounds(), juce::Colours::black);
        const int fftsize = fft->getSize();
        fftbuffer.resize(fftsize * 2);

        auto wfunc = juce::dsp::WindowingFunction<float>{
            (size_t)fftsize, juce::dsp::WindowingFunction<float>::blackman};
        int incnt = 0;

        std::fill(fftbuffer.begin(), fftbuffer.end(), 0.0);
        const auto insiglen = insigbuf.getNumSamples();
        for (int j = 0; j < img.getWidth(); ++j)
        {
            int incnt = (double)insiglen / img.getWidth() * j;
            for (int i = 0; i < fftsize; ++i)
            {
                if (incnt + i < insiglen)
                    fftbuffer[i] = insigbuf.getSample(0, incnt + i);
                else
                    fftbuffer[i] = 0.0f;
            }
            wfunc.multiplyWithWindowingTable(fftbuffer.data(), fftsize);
            fft->performFrequencyOnlyForwardTransform(fftbuffer.data());

            int x = j;
            for (int i = 0; i < img.getHeight(); ++i)
            {
                int index = (fftsize / 2.0 / img.getHeight()) * i;
                float s = (2.0 * fftbuffer[index]); // / fft->getSize();
                s *= gain;
                float db = 20.0f * std::log10(std::max(1e-7f, s));

                // 2. Audacity Default Range: -60dB to 0dB
                float minDb = mindisplaydb;
                float maxDb = 0.0f;

                // 3. Map dB to a 0.0 ... 1.0 visual range
                float visualLevel = juce::jmap(db, minDb, maxDb, 0.0f, 1.0f);
                visualLevel = std::clamp(visualLevel, 0.0f, 1.0f);
                auto c = getHeatmapColour(visualLevel);
                img.setPixelAt(x, img.getHeight() - i, c);
            }
        }
    }
    juce::Image img;
};

class MainComponent : public juce::Component
{
  public:
    MainComponent()
    {
        addAndMakeVisible(slidMinLevel);
        addAndMakeVisible(slidGain);
        addAndMakeVisible(fftSizeCombo);
        fftSizeCombo.addItem("256", 256);
        fftSizeCombo.addItem("512", 512);
        fftSizeCombo.addItem("1024", 1024);
        fftSizeCombo.addItem("2048", 2048);
        fftSizeCombo.addItem("4096", 4096);
        fftSizeCombo.addItem("8192", 8192);
        fftSizeCombo.addItem("16384", 16384);
        fftSizeCombo.onChange = [this]() {
            if (fftSizeCombo.getSelectedId() > 0)
                sp.setFFTSizeSamples(fftSizeCombo.getSelectedId());
            repaint();
        };

        slidMinLevel.setRange(-96.0, -30.0);
        slidMinLevel.setValue(-60.0);
        slidMinLevel.onDragEnd = [this]() {
            sp.curmindb = slidMinLevel.getValue();
            sp.generate();
            repaint();
        };

        slidGain.setRange(-36.0, 0.0);
        slidGain.setValue(0.0);
        slidGain.onDragEnd = [this]() {
            sp.curgain = slidGain.getValue();
            sp.generate();
            repaint();
        };

        sp.generate();
        setSize(1000, 500);
    }
    void paint(juce::Graphics &g) override
    {
        g.setImageResamplingQuality(juce::Graphics::ResamplingQuality::highResamplingQuality);
        g.drawImage(sp.img, 0, 0, getWidth(), getHeight() - 100, 0, 0, sp.img.getWidth(),
                    sp.img.getHeight());
    }
    void resized() override
    {
        juce::FlexBox flex;
        flex.flexDirection = juce::FlexBox::Direction::column;
        flex.items.add(juce::FlexItem(fftSizeCombo).withFlex(1.0));
        flex.items.add(juce::FlexItem(slidMinLevel).withFlex(1.0));
        flex.items.add(juce::FlexItem(slidGain).withFlex(1.0));
        flex.performLayout(juce::Rectangle<int>{0, getHeight() - 100, getWidth(), 100});
    }
    juce::Slider slidMinLevel;
    juce::Slider slidGain;
    juce::ComboBox fftSizeCombo;
    SpectroGram sp;
};

class GuiAppApplication final : public juce::JUCEApplication
{
  public:
    //==============================================================================
    GuiAppApplication() {}

    // We inject these as compile definitions from the CMakeLists.txt
    // If you've enabled the juce header with `juce_generate_juce_header(<thisTarget>)`
    // you could `#include <JuceHeader.h>` and use `ProjectInfo::projectName` etc. instead.
    const juce::String getApplicationName() override { return "PAREXP"; }
    const juce::String getApplicationVersion() override { return "0.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    //==============================================================================
    void initialise(const juce::String &commandLine) override
    {
        // This method is where you should put your application's initialisation code..
        juce::ignoreUnused(commandLine);

        mainWindow.reset(new MainWindow(getApplicationName()));
        mainWindow->setSize(1000, 550);
    }

    void shutdown() override
    {
        // Add your application's shutdown code here..

        mainWindow = nullptr; // (deletes our window)
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        // This is called when the app is being asked to quit: you can ignore this
        // request and let the app carry on running, or call quit() to allow the app to close.
        quit();
    }

    void anotherInstanceStarted(const juce::String &commandLine) override
    {
        // When another instance of the app is launched while this one is running,
        // this method is invoked, and the commandLine parameter tells you what
        // the other instance's command-line arguments were.
        juce::ignoreUnused(commandLine);
    }

    //==============================================================================
    /*
        This class implements the desktop window that contains an instance of
        our MainComponent class.
    */
    class MainWindow final : public juce::DocumentWindow
    {
      public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(name,
                             juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                                 backgroundColourId),
                             allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);

            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());

            setVisible(true);
        }

        void closeButtonPressed() override
        {
            // This is called when the user tries to close this window. Here, we'll just
            // ask the app to quit when this happens, but you can change this to do
            // whatever you need.
            getInstance()->systemRequestedQuit();
        }

        /* Note: Be careful if you override any DocumentWindow methods - the base
           class uses a lot of them, so by overriding you might break its functionality.
           It's best to do all your work in your content component instead, but if
           you really have to override any DocumentWindow methods, make sure your
           subclass also calls the superclass's method.
        */

      private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

  private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION(GuiAppApplication)

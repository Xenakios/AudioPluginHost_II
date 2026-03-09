#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>

class SpectroGram
{
  public:
    static const int fftsize = 2048;
    juce::AudioBuffer<float> insigbuf;
    double insigsr = 0.0;
    int insiglen = 0;
    SpectroGram()
    {

        juce::AudioFormatManager mana;
        mana.registerBasicFormats();
        auto filename = R"(C:\MusicAudio\sourcesamples\_count.wav)";
        auto reader = mana.createReaderFor(juce::File(filename));
        img = juce::Image(juce::Image::ARGB, reader->lengthInSamples / fftsize * 2, fftsize / 2,
                          true);
        insigbuf.setSize(1, reader->lengthInSamples);
        insigsr = reader->sampleRate;
        insiglen = reader->lengthInSamples;
        reader->read(&insigbuf, 0, reader->lengthInSamples, 0, true, true);
        delete reader;
    }
    void generate(double maxdisplaydb)
    {
        img.clear(img.getBounds(), juce::Colours::black);
        std::array<float, fftsize * 2> fftbuffer;

        auto wfunc = juce::dsp::WindowingFunction<float>{
            (size_t)fftsize, juce::dsp::WindowingFunction<float>::blackman};
        int incnt = 0;
        juce::dsp::FFT fft{11};
        std::fill(fftbuffer.begin(), fftbuffer.end(), 0.0);
        for (int j = 0; j < img.getWidth(); ++j)
        {
            
            incnt = (double)insiglen / img.getWidth() * j;
            for (int i = 0; i < fftsize; ++i)
            {
                if (incnt + i < insiglen)
                    fftbuffer[i] = insigbuf.getSample(0, incnt + i);
                else
                    fftbuffer[i] = 0.0f;
            }
            wfunc.multiplyWithWindowingTable(fftbuffer.data(), fftsize);
            fft.performFrequencyOnlyForwardTransform(fftbuffer.data());
            double sum = 0.0;
            for (int i = 0; i < fftsize; ++i)
            {
                sum += fftbuffer[i];
            }
            if (sum > 0.0)
            {
                double scaler = 1.0 / sum;
                for (int i = 0; i < fftsize; ++i)
                {
                    // fftbuffer[i] *= scaler;
                }
            }
            int x = j;
            for (int i = 0; i < img.getHeight(); ++i)
            {
                int index = (fftsize / 2.0 / img.getHeight()) * i;
                float s = fftbuffer[index] * 0.125;
                s = std::clamp(s, 0.0f, 1.0f);
                // s = juce::Decibels::gainToDecibels(s);
                // s = s * s * s;
                const float mindb = -60.0f;
                const float maxdb = maxdisplaydb;
                // if (s > mindb)
                {
                    s = juce::jmap(s, 0.0f, 1.0f, 0.0f, 255.0f);
                    uint8_t pixval = s;
                    juce::Colour c{pixval, pixval, pixval};
                    img.setPixelAt(x, img.getHeight() - i, c);
                }
                // s = std::clamp(s, mindb, maxdb);
            }
            incnt += fftsize;
        }
    }
    juce::Image img;
};

class MainComponent : public juce::Component
{
  public:
    MainComponent()
    {
        addAndMakeVisible(slid);
        slid.setRange(-95.0, 0.0);
        slid.onDragEnd = [this]() {
            sp.generate(slid.getValue());
            repaint();
        };
        sp.generate(0.0);
        setSize(1000, 500);
    }
    void paint(juce::Graphics &g) override
    {
        // g.setImageResamplingQuality(juce::Graphics::ResamplingQuality::highResamplingQuality);
        g.drawImage(sp.img, 0, 0, getWidth(), getHeight() - 25, 0, 0, sp.img.getWidth(),
                    sp.img.getHeight());
    }
    void resized() override { slid.setBounds(0, getHeight() - 25, getWidth(), 25); }
    juce::Slider slid;
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

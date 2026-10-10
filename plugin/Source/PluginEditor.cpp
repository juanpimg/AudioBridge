#include "PluginProcessor.h"
#include "PluginEditor.h"

AudioBridgeAudioProcessorEditor::AudioBridgeAudioProcessorEditor (AudioBridgeAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (420, 280);

    muteToggle.setButtonText ("Silenciar salida de FL Studio (Mute FL)");
    muteToggle.setToggleState (audioProcessor.isMuteEnabled(), juce::dontSendNotification);
    muteToggle.onClick = [this] {
        audioProcessor.setMuteEnabled (muteToggle.getToggleState());
    };
    addAndMakeVisible (muteToggle);

    // Refresh GUI at 30 fps for smooth meters
    startTimerHz (30);
}

AudioBridgeAudioProcessorEditor::~AudioBridgeAudioProcessorEditor()
{
    stopTimer();
}

void AudioBridgeAudioProcessorEditor::timerCallback()
{
    // Smooth peak decay for VU meters
    float rawL = audioProcessor.getPeakL();
    float rawR = audioProcessor.getPeakR();

    smoothedPeakL = juce::jmax (rawL, smoothedPeakL * 0.85f);
    smoothedPeakR = juce::jmax (rawR, smoothedPeakR * 0.85f);

    if (muteToggle.getToggleState() != audioProcessor.isMuteEnabled()) {
        muteToggle.setToggleState (audioProcessor.isMuteEnabled(), juce::dontSendNotification);
    }

    repaint();
}

void AudioBridgeAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Gradient dark background
    g.fillAll (juce::Colour (0xff121217));

    // Header Card
    auto headerBounds = juce::Rectangle<float> (15.0f, 15.0f, (float)getWidth() - 30.0f, 60.0f);
    g.setColour (juce::Colour (0xff1e1e26));
    g.fillRoundedRectangle (headerBounds, 8.0f);

    // Title
    g.setColour (juce::Colour (0xff00e5ff));
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("AUDIOBRIDGE v2", 30, 24, 200, 24, juce::Justification::left);

    g.setColour (juce::Colour (0xff9e9ea8));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Transmisor VST3 para FL Studio", 30, 48, 220, 18, juce::Justification::left);

    // Connection Status Badge
    bool connected = audioProcessor.isReceiverConnected();
    juce::Colour badgeColor = connected ? juce::Colour (0xff00e676) : juce::Colour (0xffff9100);
    juce::String statusText = connected ? "RECEPTOR CONECTADO" : "ESPERANDO RECEPTOR...";

    auto badgeBounds = juce::Rectangle<float> ((float)getWidth() - 210.0f, 30.0f, 180.0f, 30.0f);
    g.setColour (badgeColor.withAlpha (0.15f));
    g.fillRoundedRectangle (badgeBounds, 15.0f);
    g.setColour (badgeColor.withAlpha (0.5f));
    g.drawRoundedRectangle (badgeBounds, 15.0f, 1.0f);

    // Dot indicator
    g.setColour (badgeColor);
    g.fillEllipse ((float)getWidth() - 195.0f, 41.0f, 8.0f, 8.0f);

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (statusText, (int)badgeBounds.getX() + 25, (int)badgeBounds.getY(), 145, 30, juce::Justification::centredLeft);

    // Main Card
    auto mainCard = juce::Rectangle<float> (15.0f, 85.0f, (float)getWidth() - 30.0f, 180.0f);
    g.setColour (juce::Colour (0xff18181f));
    g.fillRoundedRectangle (mainCard, 8.0f);

    // Info Labels
    g.setColour (juce::Colour (0xffa0a0aa));
    g.setFont (juce::FontOptions (12.0f));
    
    double sr = audioProcessor.getSampleRate();
    juce::String srString = (sr > 0.0) ? juce::String (sr, 0) + " Hz" : "48000 Hz (detectando)";
    g.drawText ("Frecuencia DAW: " + srString, 30, 95, 200, 20, juce::Justification::left);

    // VU Meters (Left & Right)
    int meterX = 30;
    int meterY = 130;
    int meterWidth = getWidth() - 60;
    int barHeight = 12;

    auto drawMeter = [&] (const juce::String& label, float peak, int y) {
        g.setColour (juce::Colour (0xff80808a));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (label, meterX, y, 20, barHeight, juce::Justification::centredLeft);

        auto trackBounds = juce::Rectangle<float> ((float)(meterX + 25), (float)y, (float)(meterWidth - 25), (float)barHeight);
        g.setColour (juce::Colour (0xff252530));
        g.fillRoundedRectangle (trackBounds, 3.0f);

        float clampedPeak = juce::jlimit (0.0f, 1.0f, peak);
        if (clampedPeak > 0.001f) {
            float fillWidth = (meterWidth - 25) * clampedPeak;
            auto fillBounds = juce::Rectangle<float> ((float)(meterX + 25), (float)y, fillWidth, (float)barHeight);
            
            juce::ColourGradient grad (juce::Colour (0xff00e5ff), (float)(meterX + 25), (float)y,
                                       (clampedPeak > 0.85f ? juce::Colour (0xffff1744) : juce::Colour (0xff00e676)),
                                       (float)(meterX + meterWidth), (float)y, false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (fillBounds, 3.0f);
        }
    };

    drawMeter ("L", smoothedPeakL, meterY);
    drawMeter ("R", smoothedPeakR, meterY + 20);

    // Tip Note
    g.setColour (juce::Colour (0xff70707a));
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("* El audio se transmite en tiempo real a AudioBridge Receiver en tus cascos.",
                30, 235, getWidth() - 60, 20, juce::Justification::centredLeft);
}

void AudioBridgeAudioProcessorEditor::resized()
{
    muteToggle.setBounds (28, 185, getWidth() - 56, 30);
    muteToggle.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffe4e4e7));
    muteToggle.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff00e5ff));
}

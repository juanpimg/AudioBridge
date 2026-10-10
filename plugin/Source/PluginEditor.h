#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class AudioBridgeAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                         public juce::Timer
{
public:
    AudioBridgeAudioProcessorEditor (AudioBridgeAudioProcessor&);
    ~AudioBridgeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    AudioBridgeAudioProcessor& audioProcessor;

    juce::ToggleButton muteToggle;
    float smoothedPeakL = 0.0f;
    float smoothedPeakR = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioBridgeAudioProcessorEditor)
};

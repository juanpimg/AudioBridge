#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../../common/SharedAudioProtocol.h"

class AudioBridgeAudioProcessor : public juce::AudioProcessor
{
public:
    AudioBridgeAudioProcessor();
    ~AudioBridgeAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Helper methods for the GUI Editor
    bool isReceiverConnected() const;
    bool isMuteEnabled() const;
    void setMuteEnabled (bool shouldMute);
    float getPeakL() const { return currentPeakL; }
    float getPeakR() const { return currentPeakR; }

private:
    HANDLE hMapFile = nullptr;
    HANDLE hDataEvent = nullptr;
    AudioBridge::SharedAudioSegment* sharedSegment = nullptr;

    juce::AudioParameterBool* muteParam = nullptr;

    float currentPeakL = 0.0f;
    float currentPeakR = 0.0f;

    void initSharedMemory();
    void closeSharedMemory();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioBridgeAudioProcessor)
};

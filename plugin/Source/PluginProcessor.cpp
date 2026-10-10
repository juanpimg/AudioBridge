#include "PluginProcessor.h"
#include "PluginEditor.h"

AudioBridgeAudioProcessor::AudioBridgeAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
      )
#endif
{
    addParameter (muteParam = new juce::AudioParameterBool (
        juce::ParameterID ("muteFLOutput", 1),
        "Mute FL Output",
        true // Mute by default so only receiver plays
    ));

    initSharedMemory();
}

AudioBridgeAudioProcessor::~AudioBridgeAudioProcessor()
{
    closeSharedMemory();
}

void AudioBridgeAudioProcessor::initSharedMemory()
{
    if (!sharedSegment) {
        sharedSegment = AudioBridge::createOrOpenSharedSegment(hMapFile, true);
    }
    if (!hDataEvent) {
        hDataEvent = CreateEventW(NULL, FALSE, FALSE, AudioBridge::DATA_EVENT_NAME);
    }
}

void AudioBridgeAudioProcessor::closeSharedMemory()
{
    if (hDataEvent) {
        CloseHandle(hDataEvent);
        hDataEvent = nullptr;
    }
    if (sharedSegment) {
        AudioBridge::closeSharedSegment(hMapFile, sharedSegment);
    }
}

const juce::String AudioBridgeAudioProcessor::getName() const
{
    return "AudioBridge v2";
}

bool AudioBridgeAudioProcessor::acceptsMidi() const { return false; }
bool AudioBridgeAudioProcessor::producesMidi() const { return false; }
bool AudioBridgeAudioProcessor::isMidiEffect() const { return false; }
double AudioBridgeAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int AudioBridgeAudioProcessor::getNumPrograms() { return 1; }
int AudioBridgeAudioProcessor::getCurrentProgram() { return 0; }
void AudioBridgeAudioProcessor::setCurrentProgram (int) {}
const juce::String AudioBridgeAudioProcessor::getProgramName (int) { return {}; }
void AudioBridgeAudioProcessor::changeProgramName (int, const juce::String&) {}

void AudioBridgeAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    initSharedMemory();
    if (sharedSegment) {
        sharedSegment->header.sampleRate.store((uint32_t)sampleRate, std::memory_order_release);
        sharedSegment->header.channels.store(AudioBridge::NUM_CHANNELS, std::memory_order_release);
    }
}

void AudioBridgeAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool AudioBridgeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}
#endif

void AudioBridgeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0 || totalNumInputChannels <= 0)
        return;

    // Ensure shared memory is open
    if (!sharedSegment) {
        initSharedMemory();
        if (!sharedSegment)
            return;
    }

    const float* leftIn  = buffer.getReadPointer(0);
    const float* rightIn = (totalNumInputChannels > 1) ? buffer.getReadPointer(1) : leftIn;

    // Calculate peaks for meters
    float blockPeakL = 0.0f;
    float blockPeakR = 0.0f;

    for (int i = 0; i < numSamples; ++i) {
        float absL = std::abs(leftIn[i]);
        float absR = std::abs(rightIn[i]);
        if (absL > blockPeakL) blockPeakL = absL;
        if (absR > blockPeakR) blockPeakR = absR;
    }

    currentPeakL = blockPeakL;
    currentPeakR = blockPeakR;
    sharedSegment->header.peakLeft.store(blockPeakL, std::memory_order_relaxed);
    sharedSegment->header.peakRight.store(blockPeakR, std::memory_order_relaxed);

    // Update sample rate in case DAW changed it
    uint32_t currentSR = (uint32_t)getSampleRate();
    if (currentSR > 0 && sharedSegment->header.sampleRate.load(std::memory_order_relaxed) != currentSR) {
        sharedSegment->header.sampleRate.store(currentSR, std::memory_order_relaxed);
    }

    // Write to lock-free ring buffer
    uint64_t wFrame = sharedSegment->header.writeFrame.load(std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i) {
        uint32_t idx = (uint32_t)((wFrame + i) & AudioBridge::RING_BUFFER_MASK);
        sharedSegment->buffer[idx * AudioBridge::NUM_CHANNELS + 0] = leftIn[i];
        sharedSegment->buffer[idx * AudioBridge::NUM_CHANNELS + 1] = rightIn[i];
    }

    sharedSegment->header.writeFrame.store(wFrame + numSamples, std::memory_order_release);
    sharedSegment->header.producerHeartbeat.store(GetTickCount64(), std::memory_order_release);

    // Notify receiver
    if (hDataEvent) {
        SetEvent(hDataEvent);
    }

    // Mute DAW output if enabled to prevent duplicate playback / driver conflicts
    if (isMuteEnabled()) {
        buffer.clear();
    }
}

bool AudioBridgeAudioProcessor::isReceiverConnected() const
{
    if (!sharedSegment) return false;
    uint64_t now = GetTickCount64();
    uint64_t consumerHb = sharedSegment->header.consumerHeartbeat.load(std::memory_order_relaxed);
    return (now >= consumerHb) && ((now - consumerHb) < 2000);
}

bool AudioBridgeAudioProcessor::isMuteEnabled() const
{
    return muteParam ? muteParam->get() : true;
}

void AudioBridgeAudioProcessor::setMuteEnabled (bool shouldMute)
{
    if (muteParam) {
        *muteParam = shouldMute;
    }
}

bool AudioBridgeAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* AudioBridgeAudioProcessor::createEditor()
{
    return new AudioBridgeAudioProcessorEditor (*this);
}

void AudioBridgeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream (destData, true);
    stream.writeBool (isMuteEnabled());
}

void AudioBridgeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream (data, static_cast<size_t> (sizeInBytes), false);
    if (stream.getNumBytesRemaining() > 0) {
        setMuteEnabled (stream.readBool());
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioBridgeAudioProcessor();
}

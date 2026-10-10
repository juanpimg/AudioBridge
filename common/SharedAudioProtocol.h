#pragma once

#include <windows.h>
#include <cstdint>
#include <atomic>

namespace AudioBridge {

    constexpr const wchar_t* SHARED_MEM_NAME   = L"Local\\AudioBridgev2_SharedMem";
    constexpr const wchar_t* DATA_EVENT_NAME   = L"Local\\AudioBridgev2_DataEvent";

    constexpr uint32_t PROTOCOL_MAGIC          = 0x41554252; // 'AUBR'
    constexpr uint32_t PROTOCOL_VERSION        = 2;
    constexpr uint32_t RING_BUFFER_FRAMES      = 65536;      // Must be power of 2 (~1.36s @ 48kHz)
    constexpr uint32_t RING_BUFFER_MASK        = RING_BUFFER_FRAMES - 1;
    constexpr uint32_t NUM_CHANNELS            = 2;          // Stereo (L, R)

    struct alignas(64) SharedAudioHeader {
        uint32_t magic;
        uint32_t version;
        
        // Audio stream properties
        std::atomic<uint32_t> sampleRate;        // e.g. 44100, 48000
        std::atomic<uint32_t> channels;          // 2
        
        // SPSC Lock-Free atomic frame counters (monotonically increasing)
        alignas(64) std::atomic<uint64_t> writeFrame;
        alignas(64) std::atomic<uint64_t> readFrame;
        
        // Heartbeats (GetTickCount64 ms)
        alignas(64) std::atomic<uint64_t> producerHeartbeat;
        alignas(64) std::atomic<uint64_t> consumerHeartbeat;
        
        // VU Meter peak values for visualization
        std::atomic<float> peakLeft;
        std::atomic<float> peakRight;

        // Flags
        std::atomic<uint32_t> flags; // bit 0: isPlaying
    };

    struct alignas(64) SharedAudioSegment {
        SharedAudioHeader header;
        // Interleaved stereo samples: [L0, R0, L1, R1, ...]
        float buffer[RING_BUFFER_FRAMES * NUM_CHANNELS];
    };

    // Helper to open or create shared memory segment
    inline SharedAudioSegment* createOrOpenSharedSegment(HANDLE& outHandle, bool isCreator) {
        HANDLE hMap = nullptr;
        if (isCreator) {
            hMap = CreateFileMappingW(
                INVALID_HANDLE_VALUE,
                NULL,
                PAGE_READWRITE,
                0,
                sizeof(SharedAudioSegment),
                SHARED_MEM_NAME
            );
        } else {
            hMap = OpenFileMappingW(
                FILE_MAP_ALL_ACCESS,
                FALSE,
                SHARED_MEM_NAME
            );
            if (!hMap) {
                // If opening failed because receiver started first, create it
                hMap = CreateFileMappingW(
                    INVALID_HANDLE_VALUE,
                    NULL,
                    PAGE_READWRITE,
                    0,
                    sizeof(SharedAudioSegment),
                    SHARED_MEM_NAME
                );
            }
        }

        if (!hMap) {
            outHandle = nullptr;
            return nullptr;
        }

        bool wasCreated = (GetLastError() != ERROR_ALREADY_EXISTS);

        void* view = MapViewOfFile(
            hMap,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            sizeof(SharedAudioSegment)
        );

        if (!view) {
            CloseHandle(hMap);
            outHandle = nullptr;
            return nullptr;
        }

        SharedAudioSegment* segment = static_cast<SharedAudioSegment*>(view);

        if (wasCreated) {
            // First time initialization
            memset(segment, 0, sizeof(SharedAudioSegment));
            segment->header.magic = PROTOCOL_MAGIC;
            segment->header.version = PROTOCOL_VERSION;
            segment->header.sampleRate.store(48000, std::memory_order_relaxed);
            segment->header.channels.store(NUM_CHANNELS, std::memory_order_relaxed);
            segment->header.writeFrame.store(0, std::memory_order_relaxed);
            segment->header.readFrame.store(0, std::memory_order_relaxed);
            segment->header.producerHeartbeat.store(0, std::memory_order_relaxed);
            segment->header.consumerHeartbeat.store(0, std::memory_order_relaxed);
            segment->header.peakLeft.store(0.0f, std::memory_order_relaxed);
            segment->header.peakRight.store(0.0f, std::memory_order_relaxed);
            segment->header.flags.store(0, std::memory_order_relaxed);
        }

        outHandle = hMap;
        return segment;
    }

    inline void closeSharedSegment(HANDLE& handle, SharedAudioSegment*& segment) {
        if (segment) {
            UnmapViewOfFile(segment);
            segment = nullptr;
        }
        if (handle) {
            CloseHandle(handle);
            handle = nullptr;
        }
    }
}

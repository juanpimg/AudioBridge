// receiver/main.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <commctrl.h>
#include <shellapi.h>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "../common/SharedAudioProtocol.h"

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <atomic>

// --- GUI Control IDs ---
#define ID_COMBO_DEVICE   201
#define ID_BTN_APPLY      202
#define ID_COMBO_LATENCY  203
#define ID_BTN_TRAY       204
#define ID_TIMER_UI       205
#define WM_TRAY_ICON      (WM_USER + 10)
#define ID_TRAY_RESTORE   301
#define ID_TRAY_EXIT      302

// --- Global Audio State ---
ma_context g_context;
ma_device g_device;
bool g_isDeviceRunning = false;
bool g_isAppRunning = true;
std::vector<ma_device_id> g_playbackDeviceIds;
std::vector<std::wstring> g_playbackDeviceNames;

HANDLE g_hMapFile = nullptr;
AudioBridge::SharedAudioSegment* g_sharedSegment = nullptr;

// Audio buffer settings
uint32_t g_targetLatencyFrames = 512; // Default ~10.6 ms at 48kHz
bool g_isBuffering = true;
double g_fractionalReadPos = 0.0;
uint64_t g_lastReadFrame = 0;

// UI Handles
HWND g_hWnd = nullptr;
HWND g_hComboDevice = nullptr;
HWND g_hComboLatency = nullptr;
HWND g_hStatusText = nullptr;
HWND g_hMeterL = nullptr;
HWND g_hMeterR = nullptr;
HWND g_hBtnApply = nullptr;
HWND g_hBtnTray = nullptr;
NOTIFYICONDATAW g_nid = { sizeof(NOTIFYICONDATAW) };
bool g_inTray = false;

// Forward declarations
void initSharedMemory();
void startAudioDevice(ma_device_id* pDevId, uint32_t periodFrames);
void stopAudioDevice();
void updateUIStatus();

// Audio Callback (runs on real-time WASAPI thread)
void audioDataCallback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount)
{
    float* out = static_cast<float*>(pOutput);

    if (!g_sharedSegment) {
        memset(out, 0, frameCount * AudioBridge::NUM_CHANNELS * sizeof(float));
        return;
    }

    uint64_t now = GetTickCount64();
    uint64_t producerHb = g_sharedSegment->header.producerHeartbeat.load(std::memory_order_relaxed);
    
    // Check if DAW is active (heartbeat within 350ms)
    bool isDawProducing = (now >= producerHb) && ((now - producerHb) < 350);
    g_sharedSegment->header.consumerHeartbeat.store(now, std::memory_order_relaxed);

    if (!isDawProducing) {
        // DAW is paused / stopped -> emit silence smoothly
        memset(out, 0, frameCount * AudioBridge::NUM_CHANNELS * sizeof(float));
        g_isBuffering = true;
        g_fractionalReadPos = 0.0;
        return;
    }

    uint64_t wFrame = g_sharedSegment->header.writeFrame.load(std::memory_order_acquire);
    uint64_t rFrame = g_lastReadFrame;

    int64_t availableFrames = static_cast<int64_t>(wFrame - rFrame);

    // Sanity checks on ring buffer bounds
    if (availableFrames < 0 || availableFrames > AudioBridge::RING_BUFFER_FRAMES) {
        // Underflow or severe lag: jump to target cushion behind producer
        rFrame = (wFrame > g_targetLatencyFrames) ? (wFrame - g_targetLatencyFrames) : 0;
        g_lastReadFrame = rFrame;
        g_fractionalReadPos = 0.0;
        availableFrames = static_cast<int64_t>(wFrame - rFrame);
        g_isBuffering = true;
    }

    // Jitter cushion buffering
    if (g_isBuffering) {
        if (availableFrames >= static_cast<int64_t>(g_targetLatencyFrames)) {
            g_isBuffering = false;
        } else {
            memset(out, 0, frameCount * AudioBridge::NUM_CHANNELS * sizeof(float));
            return;
        }
    }

    // Determine resampling ratio
    uint32_t dawSR = g_sharedSegment->header.sampleRate.load(std::memory_order_relaxed);
    if (dawSR == 0) dawSR = 48000;
    uint32_t deviceSR = pDevice->sampleRate;
    if (deviceSR == 0) deviceSR = 48000;

    double baseRatio = static_cast<double>(dawSR) / static_cast<double>(deviceSR);

    // Adaptive micro-drift compensation (+/- 0.05% nudge)
    // Keeps the buffer centered around g_targetLatencyFrames without dropouts
    if (availableFrames > static_cast<int64_t>(g_targetLatencyFrames + 128)) {
        baseRatio *= 1.0006; // Consume slightly faster
    } else if (availableFrames < static_cast<int64_t>(g_targetLatencyFrames - 128)) {
        baseRatio *= 0.9994; // Consume slightly slower
    }

    double readPos = g_fractionalReadPos;
    uint32_t framesRead = 0;

    for (ma_uint32 i = 0; i < frameCount; ++i) {
        uint64_t frameIndexA = rFrame + static_cast<uint64_t>(readPos);
        uint64_t frameIndexB = frameIndexA + 1;

        if (static_cast<int64_t>(wFrame - frameIndexA) <= 0) {
            // Buffer starvation during this frame: fade to silence
            out[i * 2 + 0] = 0.0f;
            out[i * 2 + 1] = 0.0f;
            g_isBuffering = true;
            continue;
        }

        double frac = readPos - std::floor(readPos);

        uint32_t bufIdxA = static_cast<uint32_t>(frameIndexA & AudioBridge::RING_BUFFER_MASK);
        uint32_t bufIdxB = static_cast<uint32_t>(frameIndexB & AudioBridge::RING_BUFFER_MASK);

        float leftA = g_sharedSegment->buffer[bufIdxA * 2 + 0];
        float rightA = g_sharedSegment->buffer[bufIdxA * 2 + 1];
        float leftB = g_sharedSegment->buffer[bufIdxB * 2 + 0];
        float rightB = g_sharedSegment->buffer[bufIdxB * 2 + 1];

        // Linear interpolation for crystal clear sound at any sample rate
        out[i * 2 + 0] = static_cast<float>(leftA + (leftB - leftA) * frac);
        out[i * 2 + 1] = static_cast<float>(rightA + (rightB - rightA) * frac);

        readPos += baseRatio;
    }

    uint64_t intAdvance = static_cast<uint64_t>(std::floor(readPos));
    g_lastReadFrame += intAdvance;
    g_fractionalReadPos = readPos - intAdvance;

    g_sharedSegment->header.readFrame.store(g_lastReadFrame, std::memory_order_release);
}

void initSharedMemory()
{
    if (!g_sharedSegment) {
        g_sharedSegment = AudioBridge::createOrOpenSharedSegment(g_hMapFile, false);
    }
}

void stopAudioDevice()
{
    if (g_isDeviceRunning) {
        ma_device_stop(&g_device);
        ma_device_uninit(&g_device);
        g_isDeviceRunning = false;
    }
}

void startAudioDevice(ma_device_id* pDevId, uint32_t periodFrames)
{
    stopAudioDevice();

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.pDeviceID = pDevId;
    deviceConfig.playback.format    = ma_format_f32;
    deviceConfig.playback.channels  = AudioBridge::NUM_CHANNELS;
    deviceConfig.sampleRate         = 48000; // Native Windows / Bluetooth rate
    deviceConfig.dataCallback       = audioDataCallback;
    deviceConfig.periodSizeInFrames = periodFrames;
    deviceConfig.performanceProfile = ma_performance_profile_low_latency;

    if (ma_device_init(&g_context, &deviceConfig, &g_device) == MA_SUCCESS) {
        ma_device_start(&g_device);
        g_isDeviceRunning = true;
    }
}

void updateUIStatus()
{
    if (!g_sharedSegment) {
        initSharedMemory();
    }

    uint64_t now = GetTickCount64();
    bool connected = false;
    uint32_t dawSR = 48000;
    float peakL = 0.0f;
    float peakR = 0.0f;

    if (g_sharedSegment) {
        uint64_t prodHb = g_sharedSegment->header.producerHeartbeat.load(std::memory_order_relaxed);
        connected = (now >= prodHb) && ((now - prodHb) < 1500);
        dawSR = g_sharedSegment->header.sampleRate.load(std::memory_order_relaxed);
        peakL = g_sharedSegment->header.peakLeft.load(std::memory_order_relaxed);
        peakR = g_sharedSegment->header.peakRight.load(std::memory_order_relaxed);
    }

    std::wstring status;
    if (connected) {
        double latencyMs = (static_cast<double>(g_targetLatencyFrames) / 48000.0) * 1000.0;
        status = L"Estado: CONECTADO A FL STUDIO\r\n";
        status += L"DAW: " + std::to_wstring(dawSR) + L" Hz | Latencia: ~" + std::to_wstring(static_cast<int>(latencyMs)) + L" ms (" + std::to_wstring(g_targetLatencyFrames) + L" frames)";
    } else {
        status = L"Estado: ESPERANDO FL STUDIO...\r\n";
        status += L"Abre FL Studio y añade AudioBridge.vst3 en el Master.";
    }

    SetWindowTextW(g_hStatusText, status.c_str());

    // Update VU Meters
    int valL = std::clamp(static_cast<int>(peakL * 100.0f), 0, 100);
    int valR = std::clamp(static_cast<int>(peakR * 100.0f), 0, 100);
    SendMessage(g_hMeterL, PBM_SETPOS, valL, 0);
    SendMessage(g_hMeterR, PBM_SETPOS, valR, 0);
}

void minimizeToTray()
{
    ShowWindow(g_hWnd, SW_HIDE);
    g_inTray = true;

    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY_ICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"AudioBridge Receiver (Activo)");

    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void restoreFromTray()
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    ShowWindow(g_hWnd, SW_SHOW);
    SetForegroundWindow(g_hWnd);
    g_inTray = false;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        HFONT hFontBold = CreateFontW(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        // Device selection
        HWND hLblDev = CreateWindowW(L"STATIC", L"Salida de Audio (Auriculares):", WS_VISIBLE | WS_CHILD,
                                     20, 15, 360, 20, hwnd, NULL, NULL, NULL);
        SendMessage(hLblDev, WM_SETFONT, (WPARAM)hFontBold, TRUE);

        g_hComboDevice = CreateWindowW(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                                       20, 38, 360, 250, hwnd, (HMENU)ID_COMBO_DEVICE, NULL, NULL);
        SendMessage(g_hComboDevice, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Populate devices
        SendMessageW(g_hComboDevice, CB_ADDSTRING, 0, (LPARAM)L"[Predeterminado de Windows]");
        
        ma_device_info* pPlaybackInfos;
        ma_uint32 playbackCount;
        if (ma_context_get_devices(&g_context, &pPlaybackInfos, &playbackCount, NULL, NULL) == MA_SUCCESS) {
            for (ma_uint32 i = 0; i < playbackCount; ++i) {
                wchar_t wName[256] = {0};
                MultiByteToWideChar(CP_UTF8, 0, pPlaybackInfos[i].name, -1, wName, 256);
                SendMessageW(g_hComboDevice, CB_ADDSTRING, 0, (LPARAM)wName);
                g_playbackDeviceIds.push_back(pPlaybackInfos[i].id);
                g_playbackDeviceNames.push_back(wName);
            }
        }
        SendMessage(g_hComboDevice, CB_SETCURSEL, 0, 0);

        // Latency Buffer Selection
        HWND hLblLat = CreateWindowW(L"STATIC", L"Modo de Latencia / B\x00FAfer:", WS_VISIBLE | WS_CHILD,
                                     20, 75, 170, 20, hwnd, NULL, NULL, NULL);
        SendMessage(hLblLat, WM_SETFONT, (WPARAM)hFontBold, TRUE);

        g_hComboLatency = CreateWindowW(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                                        20, 98, 220, 150, hwnd, (HMENU)ID_COMBO_LATENCY, NULL, NULL);
        SendMessage(g_hComboLatency, WM_SETFONT, (WPARAM)hFont, TRUE);

        SendMessageW(g_hComboLatency, CB_ADDSTRING, 0, (LPARAM)L"Ultra-baja (~5 ms - 256 frames)");
        SendMessageW(g_hComboLatency, CB_ADDSTRING, 0, (LPARAM)L"Equilibrada (~10 ms - 512 frames)");
        SendMessageW(g_hComboLatency, CB_ADDSTRING, 0, (LPARAM)L"Segura (~20 ms - 1024 frames)");
        SendMessage(g_hComboLatency, CB_SETCURSEL, 1, 0); // Default: Balanced

        // Apply Button
        g_hBtnApply = CreateWindowW(L"BUTTON", L"Aplicar Salida", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                                    250, 98, 130, 26, hwnd, (HMENU)ID_BTN_APPLY, NULL, NULL);
        SendMessage(g_hBtnApply, WM_SETFONT, (WPARAM)hFontBold, TRUE);

        // VU Meter Bars
        HWND hLblVU = CreateWindowW(L"STATIC", L"Nivel de Audio (L / R):", WS_VISIBLE | WS_CHILD,
                                    20, 135, 360, 18, hwnd, NULL, NULL, NULL);
        SendMessage(hLblVU, WM_SETFONT, (WPARAM)hFontBold, TRUE);

        g_hMeterL = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                    20, 155, 360, 14, hwnd, NULL, NULL, NULL);
        SendMessage(g_hMeterL, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        g_hMeterR = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                    20, 172, 360, 14, hwnd, NULL, NULL, NULL);
        SendMessage(g_hMeterR, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        // Status Display
        g_hStatusText = CreateWindowW(L"STATIC", L"Iniciando...", WS_VISIBLE | WS_CHILD,
                                      20, 195, 360, 40, hwnd, NULL, NULL, NULL);
        SendMessage(g_hStatusText, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Minimize to Tray Button
        g_hBtnTray = CreateWindowW(L"BUTTON", L"Minimizar a la Bandeja (Tray)", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                   20, 245, 360, 32, hwnd, (HMENU)ID_BTN_TRAY, NULL, NULL);
        SendMessage(g_hBtnTray, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Start default audio device
        startAudioDevice(NULL, 256);
        initSharedMemory();

        // 30Hz Timer for UI and Meters
        SetTimer(hwnd, ID_TIMER_UI, 33, NULL);
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_BTN_APPLY) {
            int devIndex = SendMessage(g_hComboDevice, CB_GETCURSEL, 0, 0);
            int latIndex = SendMessage(g_hComboLatency, CB_GETCURSEL, 0, 0);

            if (latIndex == 0) {
                g_targetLatencyFrames = 256;
            } else if (latIndex == 1) {
                g_targetLatencyFrames = 512;
            } else {
                g_targetLatencyFrames = 1024;
            }

            uint32_t periodFrames = (g_targetLatencyFrames >= 512) ? 256 : 128;

            if (devIndex == 0) {
                startAudioDevice(NULL, periodFrames);
            } else if (devIndex > 0 && static_cast<size_t>(devIndex - 1) < g_playbackDeviceIds.size()) {
                startAudioDevice(&g_playbackDeviceIds[devIndex - 1], periodFrames);
            }
            g_isBuffering = true;
            updateUIStatus();
        }
        else if (id == ID_BTN_TRAY) {
            minimizeToTray();
        }
        return 0;
    }

    case WM_TIMER: {
        if (wParam == ID_TIMER_UI) {
            updateUIStatus();
        }
        return 0;
    }

    case WM_TRAY_ICON: {
        if (lParam == WM_LBUTTONDBLCLK) {
            restoreFromTray();
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESTORE, L"Abrir Ventana");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Cerrar AudioBridge");

            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);

            if (cmd == ID_TRAY_RESTORE) {
                restoreFromTray();
            } else if (cmd == ID_TRAY_EXIT) {
                DestroyWindow(hwnd);
            }
        }
        return 0;
    }

    case WM_SYSCOMMAND: {
        if (wParam == SC_MINIMIZE) {
            minimizeToTray();
            return 0;
        }
        break;
    }

    case WM_DESTROY: {
        KillTimer(hwnd, ID_TIMER_UI);
        if (g_inTray) {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
        }
        stopAudioDevice();
        if (g_sharedSegment) {
            AudioBridge::closeSharedSegment(g_hMapFile, g_sharedSegment);
        }
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int nCmdShow)
{
    // Initialize COM and Common Controls
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icex);

    if (ma_context_init(NULL, 0, NULL, &g_context) != MA_SUCCESS) {
        MessageBoxW(NULL, L"Error al inicializar el sistema de audio de Windows.", L"Error AudioBridge", MB_ICONERROR);
        return 1;
    }

    const wchar_t CLASS_NAME[] = L"AudioBridgeReceiverWindowClass";

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);

    RegisterClassExW(&wc);

    g_hWnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"AudioBridge Receiver v2 - Escucha tus Cascos",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 330,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hWnd) {
        ma_context_uninit(&g_context);
        return 1;
    }

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg = { };
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ma_context_uninit(&g_context);
    CoUninitialize();
    return 0;
}

# AudioBridge v2

Solución de audio de ultra-baja latencia para **FL Studio** y auriculares Bluetooth / USB en Windows.

[![Descargar AudioBridge v2](https://img.shields.io/badge/Descargar%20Release-v2.0.0%20(Listo%20para%20usar)-00e5ff?style=for-the-badge&logo=github)](https://github.com/juanpimg/AudioBridge/releases)

---

## ⚡ Descarga Rápida (Listo para Usar en cualquier PC)

Si quieres usar AudioBridge directamente sin compilar nada:

1. Ve a la sección de **[Releases de GitHub](https://github.com/juanpimg/AudioBridge/releases)**.
2. Descarga **`AudioBridge_v2_ListoParaOtroPC.zip`**.
3. Descomprime el archivo y haz doble clic en **`INSTALAR_VST3.bat`**.
   *(Instalará automáticamente el plugin en `C:\Program Files\Common Files\VST3\AudioBridge v2.vst3` y te dejará el reproductor en el Escritorio).*
4. Abre **FL Studio**, haz un escaneo rápido en *Options > Manage plugins* y añade **AudioBridge v2** en tu canal Master.
5. Abre **`AudioBridge_v2_Receiver.exe`** en tu Escritorio, elige tus cascos y ¡listo!

---

## ¿Qué problema resuelve?
En Windows, los drivers ASIO (FL Studio ASIO, ASIO4ALL, drivers dedicados) a menudo entran en conflicto con auriculares Bluetooth (por ejemplo, Intel Smart Sound, AirPods, Sony WH/WF, auriculares inalámbricos), causando:
- Modo telefónico manos libres (audio degradado a 8/16 kHz mono con distorsión).
- Silencio absoluto por bloqueo de modo exclusivo.
- Chasquidos y desincronización por discrepancias de frecuencia de muestreo.

**AudioBridge v2** resuelve esto dividiendo el trabajo:
1. **Plugin VST3 en FL Studio (`AudioBridge v2.vst3`)**: Se añade en el canal **Master** del mixer de FL Studio. Captura el audio con latencia cero a través de **Memoria Compartida de Windows (IPC Lock-Free)** y silencia la salida directa de FL Studio para que no interfiera su driver ASIO.
2. **Receptor de Audio (`AudioBridge_v2_Receiver.exe`)**: Aplicación nativa de Windows con interfaz gráfica. Reproduce el audio en tiempo real a través de **WASAPI Shared Stereo (A2DP HQ)** directamente en tus cascos, con colchón anti-jitter y compensación de micro-desvío de reloj.

---

## Estructura del Repositorio
- `common/SharedAudioProtocol.h`: Definición de la memoria compartida lock-free, eventos Win32 y sincronización de micro-desvío.
- `plugin/`: Código fuente C++ del plugin VST3 utilizando el framework JUCE 8.
- `receiver/`: Código fuente C++ de la aplicación receptora con GUI Win32 y motor de audio WASAPI (`miniaudio`).
- `releases/`: Archivo comprimido portable listo para distribución y releases.
- `instalar_o_actualizar.bat`: Script para recompilar e instalar localmente en cualquier momento.

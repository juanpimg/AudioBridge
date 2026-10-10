# AudioBridge v2

Solución de audio de ultra-baja latencia para **FL Studio** y auriculares Bluetooth / USB en Windows.

## ¿Qué problema resuelve?
En Windows, los drivers ASIO (FL Studio ASIO, ASIO4ALL, drivers dedicados) a menudo entran en conflicto con auriculares Bluetooth (por ejemplo, Intel Smart Sound, AirPods, Sony WH/WF, auriculares inalámbricos), causando:
- Modo telefónico manos libres (audio degradado a 8/16 kHz mono con distorsión).
- Silencio absoluto por bloqueo de modo exclusivo.
- Chasquidos y desincronización por discrepancias de frecuencia de muestreo.

**AudioBridge v2** resuelve esto dividiendo el trabajo:
1. **Plugin VST3 en FL Studio (`AudioBridge.vst3`)**: Se añade en el canal **Master** del mixer de FL Studio. Captura el audio con latencia cero a través de **Memoria Compartida de Windows (IPC Lock-Free)** y silencia la salida directa de FL Studio para que no interfiera su driver ASIO.
2. **Receptor de Audio (`AudioBridgeReceiver.exe`)**: Aplicación nativa de Windows con interfaz gráfica. Reproduce el audio en tiempo real a través de **WASAPI Shared Stereo (A2DP HQ)** directamente en tus cascos, con colchón anti-jitter y compensación de micro-desvío de reloj.

---

## Cómo Empezar a Usarlo (Paso a Paso)

### 1. Iniciar la Aplicación Receptora
- Abre el ejecutable **`AudioBridgeReceiver.exe`** que se ha dejado en tu **Escritorio**.
- En el desplegable **Salida de Audio (Auriculares)**, selecciona tus cascos Bluetooth (o déjalo en `[Predeterminado de Windows]`).
- Haz clic en **Aplicar Salida**.
- Si lo deseas, pulsa **Minimizar a la Bandeja (Tray)** para enviarlo junto al reloj de Windows.

### 2. Configurar FL Studio
1. Abre **FL Studio**.
2. Ve al menú superior: **Options > Manage plugins**.
3. Haz clic en **Find installed plugins**. FL Studio detectará automáticamente `AudioBridge` en la carpeta `C:\Program Files\Common Files\VST3\`.
4. Abre el Mixer (`F9`) y selecciona el canal **Master**.
5. Añade **AudioBridge** como efecto en una de las ranuras (slots) del Master.
6. Verás la ventana de AudioBridge con el indicador en verde: **`● RECEPTOR CONECTADO`**.
7. ¡Listo! Dale al Play en tu proyecto de FL Studio. El sonido saldrá nítido, estéreo en alta definición y sin retraso por tus cascos.

---

## Estructura del Proyecto
- `common/SharedAudioProtocol.h`: Definición de la memoria compartida lock-free, eventos Win32 y sincronización de micro-desvío.
- `plugin/`: Código fuente C++ del plugin VST3 utilizando el framework JUCE 8.
- `receiver/`: Código fuente C++ de la aplicación receptora con GUI Win32 y motor de audio WASAPI (`miniaudio`).
- `instalar_o_actualizar.bat`: Script para recompilar e instalar de nuevo en cualquier momento.

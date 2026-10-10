@echo off
chcp 65001 >nul
echo ========================================================
echo       AUDIOBRIDGE v2 - RECOMPILAR Y ACTUALIZAR
echo ========================================================
echo.

echo 1. Cerrando instancias de AudioBridge_v2_Receiver.exe si estuvieran abiertas...
taskkill /f /im AudioBridgeReceiver.exe >nul 2>&1
taskkill /f /im AudioBridge_v2_Receiver.exe >nul 2>&1

echo 2. Recompilando AudioBridge v2 en modo Release...
cmake --build C:/AudioBridge_build --config Release
if %errorlevel% neq 0 (
    echo [ERROR] Error durante la compilación.
    pause
    exit /b %errorlevel%
)

echo 3. Instalando "AudioBridge v2.vst3" en C:\Program Files\Common Files\VST3\...
if not exist "C:\Program Files\Common Files\VST3" mkdir "C:\Program Files\Common Files\VST3"
xcopy /E /I /Y "C:\AudioBridge_build\plugin\AudioBridgev2_artefacts\Release\VST3\AudioBridge v2.vst3" "C:\Program Files\Common Files\VST3\AudioBridge v2.vst3" >nul

echo 4. Actualizando AudioBridge_v2_Receiver.exe en el Escritorio...
copy /Y "C:\AudioBridge_build\receiver\Release\AudioBridgeReceiver.exe" "%USERPROFILE%\Desktop\AudioBridge_v2_Receiver.exe" >nul
copy /Y "C:\AudioBridge_build\receiver\Release\AudioBridgeReceiver.exe" "%~dp0AudioBridge_v2_Receiver.exe" >nul

echo.
echo ========================================================
echo  ¡ACTUALIZACIÓN COMPLETADA!
echo.
echo  - Plugin VST3:
echo    "C:\Program Files\Common Files\VST3\AudioBridge v2.vst3"
echo.
echo  - Receptor en el Escritorio:
echo    "AudioBridge_v2_Receiver.exe"
echo.
echo  (Tu versión anterior AudioBridge v1 sigue intacta al lado)
echo ========================================================
echo.
pause

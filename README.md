# Ritmo CR-78

Caja de ritmos de estilo analógico de finales de los 70, con diseño, nombre, sonidos y patrones originales.
Formatos: VST3, AU (macOS) y app standalone. Plataformas: macOS 10.13+ (Intel y Apple Silicon) y Windows 10+.

## Compilar

Requisitos: CMake 3.22+, y Xcode (macOS) o Visual Studio 2022 (Windows).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Los binarios quedan en `build/RitmoCR78_artefacts/Release/` (carpetas `VST3`, `AU`, `Standalone`).
Para instalar el plugin en macOS copia `Ritmo CR-78.vst3` a `~/Library/Audio/Plug-Ins/VST3/` y `Ritmo CR-78.component` a `~/Library/Audio/Plug-Ins/Components/`.
En Windows copia `Ritmo CR-78.vst3` a `C:\Program Files\Common Files\VST3\`.

## Hoja de ruta

1. Esqueleto + CI (hecho)
2. Voces sintetizadas
3. Secuenciador y ritmos
4. Mezclador y efectos
5. Interfaz gráfica
6. MIDI, salidas múltiples y presets

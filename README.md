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
2. Voces sintetizadas (hecho)
3. Secuenciador y ritmos
4. Mezclador y efectos
5. Interfaz gráfica
6. MIDI, salidas múltiples y presets

## Instrumentos (fase 2)

14 voces sintetizadas, sin samples, cada una con Nivel, Afinación (±12 st), Decay, Tono, Pan y Mute:

| Instrumento | Nota MIDI | Síntesis |
|---|---|---|
| Bombo | 36 | senoide percutida con caída de tono y clic |
| Aro | 37 | dos senoides cortas saturadas |
| Caja | 38 | dos senoides + ruido filtrado (bordón) |
| Charles | 42 | banco metálico + ruido, paso alto |
| Platillo | 49 | banco metálico + ruido, decay largo |
| Metal | 51 | banco metálico, banda media |
| Pandereta | 54 | ruido modulado por banco metálico |
| Cencerro | 56 | dos cuadradas filtradas, envolvente doble |
| Bongó agudo / grave | 60 / 61 | senoide percutida |
| Conga | 64 | senoide percutida con caída de tono |
| Maracas | 70 | ráfaga de ruido agudo |
| Güiro | 73 | ruido en banda con raspado |
| Claves | 75 | senoide aguda muy corta |

Velocidad MIDI > 100 = acento. En la interfaz, Mayús+clic en un pad = acento.

`render_demo <carpeta>` genera `01_instrumentos.wav` y `02_ritmo_demo.wav` para escuchar las voces sin DAW.

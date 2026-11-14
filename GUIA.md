# Relink Deck Tuner: guía de uso

Mod de rendimiento para Granblue Fantasy: Relink v2.0.5 en Steam Deck, con los ajustes gráficos en Standard. Objetivo: 30 FPS estables en diálogos y pueblos sin degradar la imagen. El detalle técnico y todas las mediciones están en [report.md](report.md) (§7).

## Qué hace cada pieza

| Pieza | Archivos | Efecto |
|---|---|---|
| **Vegetación LOD1** | `data.i` + `data/model/…/*.minfo` (606 modelos) | Árboles, arbustos y hierba usan su segundo nivel de detalle también de cerca. Es lo que más pesa en los diálogos con vegetación detrás (un solo arbusto costaba el 31 % del fotograma). Las hojas conservan el viento. Afecta a todo el juego. |
| **RelinkDeckTuner.asi** | `scripts/RelinkDeckTuner.asi` + `.ini` | Durante los diálogos baja **solo la resolución del 3D** (la interfaz y los textos siguen nítidos) y la devuelve al terminar. Sustituye el reescalador del juego, que causaba bandas horizontales, por **AMD FSR 1.0**. |
| **Mods de datos** | `data/layout/timeline/*_light.scene.msg`, `data/filter/pc00|pd00/*`, `data/system/NPC/*` | Quitan sombras dinámicas a luces de cinemáticas, simplifican sombras y niebla en Folca/Seedhollow y dejan la densidad de NPCs al 80 %. Efecto pequeño. |
| **GBFRelinkFix 1.1.5** (de Lyall) | `winmm.dll` (cargador ASI) + `scripts/GBFRelinkFix.*` | Salta los logos; sombras a 128 (la mitad que Standard); LOD lejano a 0.8. |

## `scripts/RelinkDeckTuner.ini`

El juego lo relee cada segundo, así que se puede cambiar con el juego abierto. Solo `Upscaler` se lee al arrancar.

| Clave | Valor recomendado | Qué hace |
|---|---|---|
| `Mode` | `1` | 0 = no toca nada (solo log); 1 = `Scale` en diálogos y `BaseScale` fuera; 2 = `Scale` siempre. |
| `BaseScale` | `1.0` | Escala del 3D fuera de los diálogos, también con FSR1. 1.0 = nativo. |
| `Scale` | `0.8` | Escala del 3D por eje mientras está activo (0.8 = 1024x576 → 1280x720). 0.85 se ve algo más nítido con menos margen. |
| `HotkeyBaseScale` / `HotkeyDialogueScale` | `120` (F9) / `121` (F10) | Cada pulsación pasa a la siguiente escala (1.0 → 0.95 → 0.9 → 0.85 → 0.8 → 0.75 → 1.0) y la guarda en el `.ini`. En la Deck, asignarlas a botones traseros en la configuración de mando de Steam. |
| `Fields` | `1` | 1 = solo el 3D (UI nítida). No usar 3. |
| `Upscaler` | `1` | 1 = FSR1; 2 = bicúbico; 0 = el del juego (con bandas). |
| `DialogueLodBias` | `1.0` | Factor de LOD del escenario durante los diálogos. 0.5 da unos FPS más, pero los objetos lejanos se simplifican a la vista. |
| `ExitDelayMs` | `700` | Cuánto espera sin cámara de cinemática antes de dar el diálogo por terminado. |
| `LogPeriodMs` | `2000` | Cada cuánto escribe en `RelinkDeckTuner.log` una línea de estado, con `fps` y `dialogueFps`. |

## Construir e instalar

### Paquete v2.0.5 (Steam Deck / PC)

Requiere una copia limpia de `data.i` de la v2.0.5 en `backup/data.i` y los archivos de `deck_pkg/`.

```bash
bash tools/make_deck_pkg.sh
bash tools/make_release.sh v0.15
```

Esto genera la carpeta `release/RelinkDeckTuner-v0.15/` con `install.sh`, `install.bat`, `uninstall.sh`, `uninstall.bat` y `SHA256SUMS.txt`. Proton necesita `WINEDLLOVERRIDES="winmm=n,b"` en las opciones de lanzamiento de Steam o configurado en `winecfg`.

### Versiones >= 2.0.5 (p. ej. 2.0.6+)

Parchea directamente los archivos `data.i` / `data.N` de la carpeta del juego (o exporta una carpeta lista para Reloaded-II):

```bash
python -I tools/patch_any_version.py "<ruta_al_juego>" --mode direct
```

## Volver al juego de fábrica

Usar `uninstall.sh` / `uninstall.bat` del paquete de release, o manualmente con el juego cerrado restaurar `data.i.orig` sobre `data.i` y borrar `winmm.dll`, `scripts/` y las carpetas modificadas dentro de `data/` (`filter`, `layout`, `model`, `system`).

## Medir

- **En la Deck:** overlay de rendimiento de SteamOS en nivel 4. Además, `scripts/RelinkDeckTuner.log` registra `fps` (todo el juego) y `dialogueFps` (en diálogo) cada `LogPeriodMs`.

## Resultados de referencia (PC, GPU integrada; en la Deck se ganó en la misma proporción)

| | Lyria (Folca) | Verbose Man (Seedhollow) |
|---|---|---|
| Juego de fábrica | 18 FPS | 25 FPS |
| Paquete final | 25 FPS (+39 %) | 38 FPS (+52 %) |

En la Deck, con vegetación LOD1 y el 3D a 0.85 en los diálogos, el diálogo de Lyria llegó a 30 FPS.

# Relink Deck Tuner v0.11

Paquete de rendimiento para **Granblue Fantasy: Relink v2.0.5** en Steam Deck (ajustes gráficos en Standard, 1280x720). Objetivo: 30 FPS estables en diálogos y pueblos, con la imagen casi igual que la original.

> **Solo para la v2.0.5 y uso personal.** `juego/` contiene archivos del juego modificados (`data.i` y `data/…`). Si el juego se actualiza, el `data.i` deja de valer; los `.asi` comprueban la versión y se desactivan solos, así que el juego no se rompe.

## Contenido

| Ruta | Qué es |
|---|---|
| `juego/data.i`, `juego/data/model/…` | Vegetación (606 modelos) en su segundo nivel de detalle también de cerca. Las hojas conservan el viento. |
| `juego/data/layout/timeline/…` | Luces de cinemáticas sin sombra dinámica. |
| `juego/data/filter/pc00|pd00/…` | Folca y Seedhollow: sombra simple y niebla reducida; sin luz volumétrica en la iglesia. |
| `juego/data/system/NPC/…` | Densidad de NPCs al 80 % en Folca y Seedhollow. |
| `juego/scripts/RelinkDeckTuner.asi` + `.ini` | En diálogos baja solo el 3D (la interfaz sigue nítida) y lo reescala con **AMD FSR 1.0**. Atajos F9/F10. Registra FPS y tirones. |
| `juego/winmm.dll` + `juego/scripts/GBFRelinkFix.*` | GBFRelinkFix 1.1.5 de Lyall (cargador ASI, sin logos, sombras 128, LOD lejano 0.8). |
| `instalar.sh` / `desinstalar.sh` | Instalación con copia de seguridad y comprobación SHA‑256; vuelta al juego de fábrica. |
| `SHA256SUMS.txt` | Sumas de todos los archivos de `juego/`. |
| `GUIA.md` | Guía completa (opciones, medición, cómo se regenera). |
| `licencias/` | MIT de GBFRelinkFix y de AMD FidelityFX FSR 1.0. |

## Instalar en la Steam Deck

1. Copia esta carpeta a la Deck.
2. En el modo escritorio, abre Konsole y ejecuta (con el juego cerrado):
   ```
   bash instalar.sh "/home/deck/Documents/nogames/Granblue Fantasy Relink"
   ```
   (cambia la ruta por la de tu juego).
3. **Solo la primera vez:** Proton tiene que cargar el `winmm.dll` del juego. En Steam, en las propiedades del juego, pon como opción de lanzamiento:
   ```
   WINEDLLOVERRIDES="winmm=n,b" %command%
   ```
4. Al abrir el juego no deben salir los logos del inicio. `scripts/RelinkDeckTuner.log` debe contener `temporal upsampler shader replaced by FSR1 EASU`.

En Windows es igual: copia el contenido de `juego/` en la carpeta del juego, sin pasos de Proton.

## Ajustes (`scripts/RelinkDeckTuner.ini`)

| Clave | Valor | Qué hace |
|---|---|---|
| `Mode` | `1` | `Scale` en diálogos, `BaseScale` fuera de ellos. 0 = desactivado. |
| `Scale` | `0.8` | Escala del 3D en diálogos. |
| `BaseScale` | `1.0` | Escala del 3D fuera de los diálogos. |
| `Upscaler` | `1` | 1 = FSR1, 2 = bicúbico, 0 = el del juego (con bandas). |
| `HotkeyBaseScale` / `HotkeyDialogueScale` | `120` (F9) / `121` (F10) | Cada pulsación pasa a la siguiente escala (1.0 → 0.95 → 0.9 → 0.85 → 0.8 → 0.75) y la guarda. En la Deck, asígnalas a botones traseros desde la configuración de mando de Steam. |

Los ajustes de GBFRelinkFix están en `scripts/GBFRelinkFix.ini`.

## Desinstalar

```
bash desinstalar.sh "/home/deck/Documents/nogames/Granblue Fantasy Relink"
```

Restaura el `data.i` de fábrica (lo guardó `instalar.sh` como `data.i.orig`) y quita los archivos del mod.

## Resultados medidos

| | Juego de fábrica | Con el paquete |
|---|---|---|
| Diálogos con vegetación detrás, Deck | 22‑27 FPS | 30 FPS |
| Diálogo del Verbose Man (Seedhollow), PC de pruebas | 25 FPS | 38 FPS |
| Pueblos, Deck | Zinkenstill: 30 con frametime de 28‑38 ms | Seedhollow: 30 estables (33,5 ms; casi sin fotogramas > 40 ms) |

## Créditos

- GBFRelinkFix: Lyall (codeberg.org/Lyall/GBFRelinkFix), licencia MIT.
- FSR 1.0: AMD FidelityFX, licencia MIT.
- Formatos de datos: GBFRDataTools de Nenkai (esquemas FlatBuffers).

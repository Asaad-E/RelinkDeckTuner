# Propuesta: "Relink Deck Tuner"

Un único mod que hace tres cosas:

1. **Desbloquea** las opciones gráficas que el juego oculta en PC.
2. **Añade** ajustes que el juego no tiene.
3. Tiene **perfiles automáticos** para los dos puntos débiles en Steam Deck: **diálogos** y **pueblos**.

Todo lo de esta propuesta se apoya en `report.md`. Lo marcado [H] es hipótesis y depende de una prueba en la Deck.

---

## 1. Qué ve el jugador

### 1.1 En el menú del juego (Opciones → Gráficos)

Opciones que ya existen en las tablas del juego pero están ocultas en PC (`option_item.tbl`):

| Opción | Valores | Para qué sirve en Deck |
|---|---|---|
| Shadow Quality | (los que traiga la tabla) | Sombras más baratas en todo el juego. |
| Character Models | Standard / Medium / High | Menos polígonos en NPCs y personajes de los pueblos. |
| Background Models | Standard / Medium / High | Escenario más ligero en los pueblos. |
| Effects | Standard / Medium / High | Menos coste de partículas. |
| Decal | Fast / Standard / High | Menos calcos (suciedad, marcas) en el suelo. |
| Depth of Field | On / Off | Quitar el desenfoque de fondo, el efecto más presente en los diálogos. |

[H] Hay que comprobar que el motor las respete en PC. Si alguna no hace nada, se conecta a mano con un hook de la capa B (§2).

### 1.2 En la configuración del mod (fuera del juego)

| Grupo | Ajuste | Por defecto (perfil Deck) |
|---|---|---|
| **Global** | Multiplicador de LOD | 0.75 |
| | Resolución de sombras (permite bajar del mínimo de 256 del menú) | 256 |
| | Escala de render interna [H] | 100 % (la opción existe, pero empieza desactivada) |
| | Desactivar TAA | No |
| **Modo diálogo** (se activa solo durante cinemáticas y conversaciones) | Quitar sombras dinámicas de las luces de evento | Sí |
| | DOF en diálogos | Off |
| | LOD extra en diálogos | 1.0 (no tocar: la cámara está cerca) |
| | Escala de render en diálogos [H] | 100 % |
| **Modo pueblo** (se activa solo en pc00 / pd00 y anexos) | Multiplicador de LOD en pueblo | 0.6 |
| | Sombra simple y niebla reducida (flags de zona) | Sí |
| | Luz volumétrica (iglesia de pc00) | Off |
| | Densidad de NPCs [H] | 100 % (opciones: 75 % / 50 %) |
| | Resolución de sombras en pueblo | 128 |

Perfiles listos para usar:

- **"Deck 30 estable"**: los valores de la tabla. Objetivo: que nunca baje de 30.
- **"Deck 40"**: el anterior más LOD 0.5, NPCs al 75 % y, si se valida, escala de render al 85 %.
- **"Calidad"**: todo como el juego original; solo desbloquea las opciones del menú.

---

## 2. Cómo está construido

Un solo paquete de **Reloaded‑II** con dos capas:

```
RelinkDeckTuner/
├─ GBFR/data/...            ← capa A: archivos de datos (.json → .msg, los convierte el GBFR Mod Manager)
└─ RelinkDeckTuner.dll      ← capa B: mod C# de Reloaded‑II (hooks + configuración + perfiles)
```

### Capa A: datos (sin código, se aplica al arrancar)

| Id | Archivo(s) | Cambio | Estado |
|---|---|---|---|
| A1 | `system/table/option_item.tbl` | Activar los flags de las 6 opciones ocultas | Formato verificado, efecto [H] |
| A2 | 38 × `layout/timeline/*_light.scene.msg` | `bCastDynamicShadow_ = 0` en las 64 luces de evento | Formato verificado |
| A3 | `filter/pc00/*`, `filter/pd00/*` | `useSimpleShadowQualityFlag_` y `useFogDownScaleQualityFlag_` con todos los bits a 1 | Formato verificado, bits [H] |
| A4 | `filter/pc00/church_00/01` | `VolumetricLightParameter.active_ = false` | Formato verificado |
| A5 | `system/NPC/c00/d00_npcConfig.msg` | `npcCost_` / `density_` según el ajuste de densidad | Formato verificado, efecto [H] |

La capa A se **genera con un script** (`src/tools/build_mod.py`, por escribir) a partir de los archivos del juego instalado. Así, cuando el juego se actualice, basta con volver a ejecutar el script; nunca se distribuyen archivos del juego.

Limitación: los archivos de datos no cambian en caliente. A2 hace que los diálogos tengan siempre luces sin sombra, y A3-A5 afectan siempre a los pueblos. Por eso son casillas independientes.

### Capa B: código (hooks en tiempo real)

| Hook | Ubicación en v2.0.5 | Estado |
|---|---|---|
| Multiplicador de LOD | patrón → `0x802236` | Verificado (único) |
| Resolución de sombras | patrón → `0x21a3f83` (fila del preset) | Verificado (único) |
| TAA off | patrón → `0x21607a0` | Verificado (único) |
| Escala de render | `0x6bbaa0` (aplica la resolución cada fotograma) | Punto localizado; falta saber qué par es el de render [H] |
| Detectar "en evento" | Arranque y fin de `Timeline::WindowTalkModule` / `CutsceneEvent` | **Por localizar** |
| Detectar "en pueblo" | Carga de stage (`pc00`/`pd00` aparecen en rutas `layout/` y `filter/`) | **Por localizar** (se puede enganchar la lectura de `filter/<stage>/`) |
| DOF on/off | `DofParameter.active_` o la pista `Dof` del timeline | **Por localizar** |

Los "modos" (diálogo/pueblo) solo cambian **valores que el hook lee cada fotograma** (LOD, sombra, DOF, escala). Por eso entran y salen sin recargar nada.

Todos los hooks buscan **patrones de bytes**, no direcciones fijas. Si una actualización rompe uno, ese ajuste se desactiva y queda registrado en el log; el resto sigue funcionando.

---

## 3. Riesgos y alternativas

| Riesgo | Mitigación |
|---|---|
| Reloaded‑II en Steam Deck (Proton) es más engorroso de instalar que en Windows. | Plan B: la capa B como **ASI + `.ini`** (cargado con `WINEDLLOVERRIDES="winmm=n,b" %command%`), y la capa A aplicada con `add-external-files` de GBFRDataTools sobre `data.i` (con copia de seguridad). |
| Los mods de tablas (`.tbl`) se rompen entre versiones. | A1 se genera con el script desde la tabla instalada y comprueba el tamaño de fila (52 bytes) antes de tocar nada. |
| Las opciones ocultas no funcionan en PC. | Se conectan por la capa B o se quitan del menú. |
| Juego online / Endless Ragnarok. | Solo se tocan gráficos; no se modifican tablas de juego ni partidas guardadas. |

---

## 4. Orden de trabajo y pruebas en la Deck (mínimas)

| Fase | Entrega | Prueba en Deck (≈5 min cada una) |
|---|---|---|
| 0 | GBFRelinkFix con LOD 0.6 / sombras 128 (sin desarrollo) | Overlay en un pueblo y en un diálogo: ¿cuánto gana? ¿Limita la GPU o la CPU? |
| 1 | Capa A completa (A1-A5) generada por script | ¿Salen las opciones ocultas? ¿Hacen algo? FPS en pueblo y diálogo. |
| 2 | Capa B global (LOD, sombras, TAA) + configuración y perfiles | Comparar con la fase 0 (debería igualarla). |
| 3 | Detección de evento y de pueblo + modos automáticos | Entrar y salir de un diálogo y de un pueblo: ¿cambia sin tirones? |
| 4 | Escala de render y DOF (si se validan) | Calidad visual frente a FPS. |

Cada fase se puede usar por sí sola. Si la fase 0 ya deja los pueblos a 30 estables, las fases 3-4 pasan a ser opcionales.

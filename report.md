# Granblue Fantasy: Relink: rendimiento en Steam Deck (diálogos y pueblos)

Versión analizada: `granblue_fantasy_relink.exe` v2.0.5 (fichero `v2.0.5.xxh128`, 28‑ago‑2026), índice `data.i` con codename `relink`, 12 archivos `data.N`.
Fecha: 2026‑11‑02.

Leyenda de confianza:

- **[VERIFICADO]**: comprobado directamente en el ejecutable o en los datos del juego con las herramientas de `src/tools/`. Es reproducible.
- **[FUENTE]**: viene de documentación o código de terceros que he contrastado con el binario.
- **[HIPÓTESIS]**: deducción razonable que solo puede confirmarse en el juego.

---

## 1. Resumen ejecutivo

| Pregunta | Respuesta |
|---|---|
| ¿Sube el juego la resolución interna en los diálogos? | **Muy probablemente no.** Las dos variables globales de resolución de render solo se escriben desde la inicialización y desde la función que aplica el perfil de gráficos. Esa función se ejecuta cada fotograma en el bucle principal, pero siempre lee la fila del preset elegido en Opciones. Los datos de las cinemáticas tampoco tienen ningún parámetro de resolución [VERIFICADO]. |
| Entonces, ¿por qué bajan los FPS en los diálogos? | Las escenas de diálogo encienden efectos que el gameplay normal no usa: **luces puntuales con sombra dinámica que se recalcula cada fotograma**, **luces dedicadas a personajes**, **DOF animado plano a plano** y cientos de pistas de luces animadas. Además, la cámara está muy cerca de los personajes, así que se ven con el LOD máximo [VERIFICADO en datos; el coste exacto es HIPÓTESIS]. |
| ¿Por qué bajan en los pueblos? | Los dos pueblos principales (`pc00` y `pd00`) tienen **5‑20 veces más NPCs colocados** que cualquier zona de combate (298 y 236 frente a ≤57). Además tienen miles de luces de área ambiental y un `foliage` de 2 MB. En sus filtros de zona, los flags que activan "sombra simple" y "niebla a resolución reducida" tienen máscaras distintas a las de otras zonas [VERIFICADO en datos; impacto real en FPS: HIPÓTESIS]. |
| ¿Hay forma de bajar el coste? | Sí, por dos vías. **(A) Mod de datos** (Reloaded‑II + GBFR Mod Manager, ficheros `.json`): quitar sombras dinámicas de las luces de cinemáticas, limitar volumétricas y forzar sombra simple / niebla reducida en pueblos. **(B) Mod de código** (ASI estilo GBFRelinkFix): bajar el LOD, la resolución de sombras y el DOF, y opcionalmente escalar la resolución de render. Los patrones de bytes de LOD, sombras y TAA están **comprobados en v2.0.5** [VERIFICADO]. |
| ¿Qué puedo probar ya, sin programar nada? | GBFRelinkFix (Codeberg, ago‑2026) con `Level of Detail Multiplier < 1` y `Shadow Quality Resolution` baja. Sus 8 patrones encuentran una sola coincidencia en tu ejecutable (§5.1). |

---

## 2. Metodología y herramientas (todo en `src/tools/`)

| Herramienta | Qué hace |
|---|---|
| `gbfr_archive.py` | Lector propio de `data.i`/`data.N`: FlatBuffer `IndexFile`, hash XXH64 de la ruta en minúsculas y bloques LZ4. Verificado con los vectores de prueba oficiales de XXH64. Resuelve **404.180 de 408.165** archivos con la lista de rutas de GBFRDataTools. |
| `patscan.py` | Busca patrones de bytes estilo IDA en `.text`. |
| `disasm.py` | Desensambla un rango del ejecutable (capstone). |
| `xref.py` | Busca referencias RIP‑relativas a una dirección (lecturas y escrituras de variables globales). |
| `callers.py` | Busca `call`/`jmp rel32` hacia una función. |
| `evtb_filters.py`, `evtb_bools.py` | Analizan los `master.evtb` de cinemáticas (FlatBuffers, esquema de GBFRDataTools). |
| `scene_stats.py`, `layout_aggregate.py` | Cuentan objetos de escena (luces, NPCs…) en `layout/**/*.scene.msg` (MessagePack). |
| `ccomp_dump.py` | Aplana los 173 `filter/**/*.ccomp.msg` (parámetros de postprocesado y luz por zona). |

Las salidas intermedias están en `src/dumps/` (ignorado por git, se regenera con las herramientas).

Uso de `agy` (Gemini 3.8 Flash High): clasificó ~3.000 cadenas del ejecutable relacionadas con rendimiento (`src/notes/agy_strings_report.md`). **Verifiqué cada fila:** 429 coinciden literalmente con el volcado y 8 no, porque `agy` las truncó con "...". Sus "Top 20 leads" son hipótesis. Solo uso aquí las que confirmé después en los datos (por ejemplo `useSimpleShadowQualityFlag_`, `useFogDownScaleQualityFlag_`, `isDistanceCulling_`).

Por qué no usé GBFRDataTools directamente: el entorno no me permite compilar código externo. Usé su código fuente solo como especificación de formatos.

---

## 3. Diálogos y cinemáticas

### 3.1 Tipos de evento [VERIFICADO]

El ejecutable construye los nombres de evento con `cw%04x`, `ct%04x`, `ci%04x`, `cm%04x`, `cn%04x`, además de `ft_pl%04x`, `vn_pl%04x` y `p%03x_ev%04x`. En los datos (`system/event/<tipo>/<id>/master.evtb`):

| Tipo | Eventos | Planos (cuts) | Notas |
|---|---|---|---|
| `ci` | 119 | 445 | Cinemáticas grandes (algunos `master.evtb` superan los 10 MB). |
| `ct` | 62 | 1.349 | Escenas habladas en motor con mucha animación (17.228 `.mot`). |
| `cw` | 98 | 2.189 | Escenas de diálogo con muchos planos cortos. |
| `cm` / `cn` | 7 / 4 | 1 c/u | Triviales. |

Las líneas de guion (`system/table/scenario/*.msg`) se reparten así: VT 8.913, FT 6.632, WD 2.258, CT 2.046, SB 1.395, AC 769, IN 760, CN 121, CM 98. La mayoría del diálogo (VT/FT) **no** depende de un `evtb`, sino del sistema de conversación en campo. [HIPÓTESIS: es el que ves en los pueblos.]

### 3.2 La resolución interna no cambia en las cinemáticas [VERIFICADO]

1. Hay una **tabla de 6 perfiles gráficos** en `.data` (RVA `0x6b82460`, 6 filas × `0x4C` bytes) y un índice global de perfil (RVA `0x7034940`, valor 0..5, donde 5 es "Custom"). Cada fila contiene, entre otras cosas, 4 resoluciones de sombra (`+0x4..+0x10`, por defecto 2048) y dos pares de resolución (`+0x24..+0x30`, por defecto 1920×1080 y 1920×1080).
2. La función `0x6bbaa0` copia `fila[+0x24..+0x30]` a 4 globales (`0x6b822e0`, `…e4`, `…e8`, `…ec`) con `xchg`. Si cambian, recrea los recursos.
3. `xref.py` muestra que **esas 4 globales solo se escriben en dos sitios**: `0x1c2034` (inicialización) y `0x6bbadf` (dentro de `0x6bbaa0`).
4. `callers.py`: `0x6bbaa0` solo se llama desde `0x1b0048`, que es el **bucle principal de mensajes de Windows** (`PeekMessage` → `Translate/Dispatch`, compara con `0x12` = `WM_QUIT`), y desde `0x1e7c1c` (arranque).
5. El índice de perfil solo se escribe en `0x1c1f42` (inicialización, valor 5) y en `0x6bbbb2` (al aplicar la configuración).
6. Ninguno de los 31 tipos de pista de "filtro" de las cinemáticas (`TimelineFilterData`) es una resolución. `MultiFrameSuperResolutionParameter` solo aparece en los filtros de zona (`filter/`), nunca en los `evtb`.

**Conclusión:** la resolución de render se decide con el preset de Opciones y no hay código de eventos que la cambie. Si en la Deck se ve "más nítido" durante los diálogos, lo más probable es que sea por el DOF y la cámara cercana, no por una resolución mayor. [Un 10 % de duda: podría existir un escalado de viewport dentro del pase de render que no he rastreado. La prueba 7.2 lo descarta en un minuto.]

### 3.3 Lo que sí encarece los diálogos [VERIFICADO en datos]

**a) Luces puntuales con sombra dinámica dedicadas a cada evento.**
`layout/timeline/` tiene 222 capas de escena para cinemáticas (`ci`/`ct`/`cw` `####_light|cloud|wind|localibl.scene.msg`). En ellas hay **64 luces puntuales activas con `bCastDynamicShadow_ = 1` y `castDynamicShadowFrequency_ = 1`** (sombra recalculada cada fotograma), repartidas en 33 eventos: por ejemplo `ct0020` (4), `ct1050` (4), `ct1110` (4), `ct8040` (4), `ct9040` (5), `cid326` (5). En comparación, todo el pueblo `pd00` tiene 0 y `pc00` tiene 5. Una sombra de luz puntual cuesta hasta 6 caras de cubemap (el motor tiene `PointLightShadowCastDualParaboloid` y `PointLightShadowCastPerspective`).
Además hay **38 luces con `useEventCharaLight_ = true`** (iluminación de personaje solo para eventos).

**b) Luces animadas por plano.** En los `evtb` de tipo `ct` hay **1.129 pistas `DeferredPointLight`** y 798 `AmbientAreaLight` para 1.349 planos. Las `cw` tienen 205 y 218. En `ct`, las claves con `DeferredPointLight.field_5 = true` son 761 de 1.167. [El significado exacto de `field_5` no está documentado.]

**c) DOF plano a plano.** Los eventos `cw` tienen **2.161 pistas `Dof` para 2.189 planos** (casi una por plano), y `Dof.field_12 = true` en 1.748 de 2.165 claves. El filtro de zona también trae `DofParameter` activo en 168 de 173 zonas (`blurSizeMax_ = 16`, `apertureShape_ = 1`). El DOF de diálogo es un candidato directo a recorte.

**d) Iluminación volumétrica y nubes por evento.** Hay capas `*_cloud.scene.msg` de hasta 6,8 MB (`ct0030_cloud`) y pistas `VolumetricLight` en todos los `ct`/`cw`. [Coste: HIPÓTESIS.]

**e) Personajes en primer plano.** Cámara cercana = LOD 0 de personajes, pelo y tela (`isUseClothPysics_` existe en el ejecutable). [HIPÓTESIS: coste proporcional al número de personajes en plano.]

---

## 4. Pueblos

### 4.1 Identificación [VERIFICADO]

- `pc00` = **"redtown"** (con iglesia). Tiene `system/NPC/c00_npcConfig.msg`.
- `pd00` = **"yellowtown"**: backstreet, bar, frontcastle, port, square, street1/2. Tiene `system/NPC/d00_npcConfig.msg`.
- `pd01`/`pd02`/`pd04`: zonas anexas (castillo, "7st square/backstreet").
- [HIPÓTESIS: corresponden a Folca y Seedhollow. Los textos del juego confirman que esos dos son los pueblos hub, pero el mapeo exacto id → nombre no está verificado.]

### 4.2 Densidad de NPCs [VERIFICADO]

En `layout/<zona>/placement*.scene.msg`, los `PlacementInfo` con `memberType_ = 7` son NPCs. `values_[0]` es el id del NPC: 33 de 34 ids de `pc00` y 34 de 36 de `pd00` coinciden con carpetas `np/<id>`.

| Zona | Colocaciones de NPC | Ids distintos |
|---|---|---|
| **pc00** | **298** | 34 |
| **pd00** | **236** | 36 |
| pd02 | 73 | |
| p100 (combate) | 57 | |
| p600 (combate) | 54 | |
| resto | ≤ 48 | |

No todas las colocaciones están activas a la vez (las hay por fase de historia o `placement_fate`), pero el orden de magnitud deja claro el peso de los pueblos.

`npcConfig` tiene un **sistema de presupuesto de multitud**: `playerCost_ = 15`, `npcCost_ = 2`, `objCost_ = 1`, `npcLength_ = 5` y áreas de `density_` ≈ 5,1. El ejecutable tiene `VillageNPCEx` y `NPCConfig::DensityParam`. [HIPÓTESIS: subir `npcCost_` o bajar `density_` reduciría los NPCs simultáneos. No he localizado la fórmula en el código.]

### 4.3 Luces y otros objetos [VERIFICADO]

| Zona | Luces puntuales activas | Con sombra dinámica | Luces de área ambiental | LocalIBL |
|---|---|---|---|---|
| pc00 | 92 | 5 | **2.422** | 65 |
| pd00 | 371 | 0 | 1.175 | **205** |
| p600 (combate) | 4.222 | 0 | 3.310 | 194 |

Por tanto, el número de luces puntuales **no** explica por sí solo los pueblos (hay zonas de combate con más). Lo que distingue a los pueblos es NPCs + vegetación (`redtown_foliage_00` pesa 2,1 MB) + LocalIBL/luces de área + 65 emisores de efectos en `pc00`.

### 4.4 Flags de calidad por zona [VERIFICADO; semántica de bits: HIPÓTESIS]

Cada zona tiene `filter/<zona>/*.ccomp.msg` con, entre otros:

- `DeferredLightParameter.useSimpleShadowQualityFlag_` (máscara de bits)
- `GlobalParameter.useFogDownScaleQualityFlag_` (máscara de bits)
- `VolumetricLightParameter` (`active_`, `numStepsMin_/Max_`, `qualityMask_`)
- `MultiFrameSuperResolutionParameter` (`active_`, `qualityMask_ = 255`) en 44 zonas, incluida `pc00/redtown_00`

Valores en los pueblos:

| Archivo | simpleShadow | fogDownScale | Volumétrica |
|---|---|---|---|
| pc00/redtown_00 | 240 | 57 | – |
| pc00/church_00 | 1 | 59 | activa |
| pc00/church_01 | 58 | 53 | activa |
| pd00/square_00 | 448 | 5 | – |
| pd00/backstreet_00 | 448 | 15 | – |
| pd00/bar_00 | 58 | 58 | – |

En el resto de zonas los valores más comunes son 53/57/58 (simpleShadow) y 58/53 (fog). [HIPÓTESIS: cada bit es un nivel de calidad o plataforma. Si es así, `square`/`backstreet` (448 = bits 6‑8) **no activarían la sombra simple en los niveles bajos** que usa la Deck. Poner todos los bits a 1 (p. ej. 511) en los pueblos podría ser una mejora gratis. Hay que comprobarlo en el juego.]

---

## 5. Palancas disponibles para el mod

### 5.1 Código (ASI) con patrones comprobados en v2.0.5 [VERIFICADO]

Patrones tomados de GBFRelinkFix (Codeberg, commit del 7‑ago‑2026) y escaneados en tu ejecutable con `patscan.py`:

| Función | Patrón | RVA en v2.0.5 | Coincidencias |
|---|---|---|---|
| Distancia de LOD (multiplica `xmm1`) | `C5 ?? 5E ?? ?? ?? ?? ?? C5 ?? 5F ?? C5 ?? 29 … C6 ?? ?? ?? ?? ?? 01` (+0xC) | `0x802236` | 1 |
| Resolución de sombras (fila del preset `+0xC/+0x10`, y `+0x4/+0x8` si es mayor de 2048) | `8B ?? ?? 0C 8B ?? ?? 10 8B ?? ?? ?? B9 ?? ?? ?? ?? 48 ?? ?? ??` | `0x21a3f83` | 1 |
| Desactivar TAA | `0F ?? ?? ?? 88 ?? ?? 48 … 5E` | `0x21607a0` | 1 |
| Limitador de FPS / suelo de velocidad | (ver `notes/gbfrelinkfix_v2_patterns.txt`) | `0x1affa3` / `0x1b01de` | 1 / 1 |
| Lista de resoluciones (resolución personalizada) | `0F ?? ?? 0F ?? ?? 48 8D …` | 4 sitios | 4 |

La versión antigua de GitHub (archivada) **falla** en la mitad de los patrones con v2.0.5. Hay que usar la de Codeberg.

Puntos de enganche propios encontrados:

- `0x6bbaa0`: aplica la resolución de render cada fotograma (§3.2). [HIPÓTESIS: escalar ahí el primer par (render) dejando el segundo (salida) daría un "render scale" de verdad. Falta confirmar cuál par es cuál: el primero tiene 6 lectores y el segundo decenas.]
- Tabla de presets `0x6b82460` + índice `0x7034940`: se pueden forzar resoluciones de sombra por debajo de 256 (el mínimo del menú es "Standard = 256").

### 5.2 Datos (sin código) [FUENTE: guía oficial de modding de Nenkai]

- Se instala con **Reloaded‑II + GBFR Mod Manager (`gbfrelink.utility.manager`)**. Los archivos van en `<mod>/GBFR/data/<ruta original>`.
- Desde la v1.0.5 el loader **convierte `.json` → `.msg` automáticamente**, así que los `scene.msg` y `ccomp.msg` (MessagePack) se pueden editar como JSON.
- El loader 2.0.1 es compatible con 2.0 (Endless Ragnarok). Los mods de tablas suelen romperse entre versiones; los de layouts y filtros no tienen ese problema conocido.

Cambios de datos candidatos, por orden de seguridad:

| # | Archivo(s) | Cambio | Objetivo |
|---|---|---|---|
| D1 | 38 `layout/timeline/*_light.scene.msg` | `bCastDynamicShadow_: 0` en las 64 luces | Diálogos/cinemáticas: quitar sombras de luces puntuales calculadas cada fotograma. |
| D2 | `filter/pc00/*`, `filter/pd00/*` (6 archivos) | `useSimpleShadowQualityFlag_` y `useFogDownScaleQualityFlag_` → todos los bits | Pueblos: sombra simple y niebla a resolución reducida. |
| D3 | `filter/pc00/church_00/01` | `VolumetricLightParameter.active_: false` o `numStepsMax_` más bajo | Iglesia de pc00. |
| D4 | `filter/**/cutscene/*.ccomp.msg` (5) + `DofParameter.blurSizeMax_` | Bajar `blurSizeMax_` (16 → 8) | DOF más barato. Las pistas de `evtb` pueden sobrescribirlo plano a plano. |
| D5 | `system/NPC/c00/d00_npcConfig.msg` | `npcCost_` 2 → 4 o `density_` a la mitad | Menos NPCs simultáneos (semántica sin confirmar). |

[Todos los cambios D* son HIPÓTESIS de impacto: el formato está verificado, el efecto en FPS no.]

### 5.3 Opciones del juego que conviene revisar primero [VERIFICADO: existen los textos]

`text_ui.msg` incluye: Graphics Quality (Standard/Medium/High/Ultra High/Custom), Lighting, Shadow Quality, Textures, Rendering ("calidad de efectos y modelos"), Background Models, Character Models, Effects, Blur, **"Limit Number of Point Lights"**, **"Limit Shadow Cache"** y "Battle Effects Intensity". Las dos opciones de "Limit…" atacan justo el coste de §3.3a. [Comprueba que están activadas en tu Deck.]

---

## 6. Plan de mod recomendado

1. **Fase 0, sin programar (10 min).** Instalar GBFRelinkFix (Codeberg) en la Deck. Opción de lanzamiento de Steam: `WINEDLLOVERRIDES="winmm=n,b" %command%` (override de DLL estándar de Proton para cargadores ASI). Configurar `[Level of Detail] Multiplier = 0.6` y `[Shadow Quality] Resolution = 128`. Medir en un pueblo y en un diálogo.
2. **Fase 1, mod de datos** (Reloaded‑II): D1 + D2, generados con un script desde `src/tools` (falta escribirlo). Cambios mínimos y reversibles.
3. **Fase 2, ASI propio** (C++ con safetyhook, mismo patrón que GBFRelinkFix): todo lo de la fase 0, más
   - escalado de resolución de render en `0x6bbaa0`, si la prueba 7.3 lo confirma,
   - un "modo diálogo" que baje DOF y LOD solo durante eventos. Para detectar eventos se puede enganchar el arranque de `Timeline::WindowTalkModule` / `CutsceneEvent`; queda por localizar.

---

## 7. Pruebas mínimas en la Deck (pocas y baratas)

| # | Prueba | Qué responde | Tiempo |
|---|---|---|---|
| 7.1 | Overlay de rendimiento nivel 4 (GPU %, CPU %, FPS) en un pueblo y en un diálogo, sin mods. | ¿Cuello de botella de GPU (diálogos) o de CPU (pueblos)? Decide si priorizar D1/DOF o NPCs/LOD. | 2 min |
| 7.2 | Captura de pantalla en un diálogo y en gameplay en el mismo sitio; comparar nitidez del HUD/fondo. | Descarta del todo el cambio de resolución (§3.2). | 1 min |
| 7.3 | Fase 0 con `LOD 0.6` / `Shadow 128` y repetir 7.1. | Cuánto aportan LOD y sombras. | 5 min |

---

## 8. Hipótesis descartadas o no verificadas

- "El juego sube la resolución en los diálogos": **descartada en código** (§3.2), pendiente solo de la confirmación visual 7.2.
- `MultiFrameSuperResolution` como reescalado temporal tipo FSR: el componente existe y está activo en 44 zonas, pero **no he verificado** si escala resolución o solo acumula muestras (TAA).
- `bDebug_DisableShadow_` en `DeferredPointLightParameter` (todas las zonas lo tienen a `false`): podría ser código de depuración eliminado en la versión final. No está probado.
- Streaming de texturas Granite (`granite/2k|4k`): el ejecutable trae mensajes de "GPU cache too small". Podría causar tirones al entrar en pueblos. No está probado.

---

## 9. Fuentes

- GBFRelinkFix (Codeberg, activo): https://codeberg.org/Lyall/GBFRelinkFix (patrones y offsets, §5.1)
- GBFRDataTools (formato `data.i`, `evtb`, lista de rutas): https://github.com/Nenkai/GBFRDataTools
- Guía de modding: https://nenkai.github.io/relink-modding/modding/creating_mods/ y https://nenkai.github.io/relink-modding/modding/installing_mods/ y https://nenkai.github.io/relink-modding/modding/endless_ragnarok_and_mods/
- GBFR Mod Manager: https://github.com/WistfulHopes/gbfrelink.utility.manager
- Contexto de usuarios (Deck, caídas en pueblos): https://steamcommunity.com/app/881020/discussions/0/4208120159992148673 y https://steamcommunity.com/app/881020/discussions/0/4208119288695646903

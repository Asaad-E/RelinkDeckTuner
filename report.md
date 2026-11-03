# Granblue Fantasy: Relink: rendimiento en Steam Deck (diálogos y pueblos)

Versión analizada: `granblue_fantasy_relink.exe` v2.0.5 (fichero `v2.0.5.xxh128`, 28‑ago‑2026), índice `data.i` con codename `relink`, 12 archivos `data.N`.
Fecha: 2026‑11‑02 (investigación) y 2026‑11‑03 (perfilado, FSR1, benchmarks).

Leyenda de confianza:

- **[VERIFICADO]**: comprobado directamente en el ejecutable o en los datos del juego con las herramientas de `src/tools/`. Es reproducible.
- **[FUENTE]**: viene de documentación o código de terceros que he contrastado con el binario.
- **[HIPÓTESIS]**: deducción razonable que solo puede confirmarse en el juego.

---

## 0. Estado final (2026‑11‑03), medido

| Pregunta | Respuesta con datos |
|---|---|
| ¿Qué hace caer los diálogos? | **Dibujar la geometría cercana**, sobre todo vegetación con alpha test. En el Verbose Man, un solo arbusto (`bg012c`, 9.366 índices × 19 instancias) es el 31 % del fotograma y la vegetación el 49 %. En Lyria la vegetación es el 27 % y el escenario opaco el 28 % (RenderDoc, §7.9 y §7.11). Post‑proceso ~1 ms, sombras y LOD lejano ≈ 0 (§7.8). |
| ¿Sube el juego la resolución en diálogos? | No (§3.2, confirmado en las capturas: 1280x720 constante). |
| ¿Qué funciona? | (1) **Vegetación LOD1** por datos (`build_foliage.py`): −15 % de tiempo de fotograma en diálogos y mejora también caminando. (2) **Bajar solo el 3D en diálogos** (`RelinkDeckTuner.asi`, `Fields = 1`): la UI sigue nítida y casi no parpadea. (3) **FSR1** en lugar del reescalador temporal del juego, que dejaba bandas: mismo coste y sin bandas (§7.12). |
| Resultado | PC: Lyria 18 → 25 FPS y Verbose Man 25 → 38 con el paquete final (§7.13‑7.15). Deck: Lyria a 30 FPS con LOD1 + 0.85. |
| ¿Qué no funcionó? | Opciones ocultas del menú (el PC no tiene código para ellas), apagar efectos de post‑proceso (~1 ms y se ve mal), LOD lejano y sombras (no afectan a lo cercano) y `DialogueLodBias` (poca ganancia; la vegetación instanciada no pasa por ese bucle). |
| Pendiente | Nada crítico. En la Deck: FSR1 funciona bajo Proton (log), diálogo a 30 FPS y Seedhollow a 30 estables sin tirones (§7.16). Opcional: RCAS (nitidez de FSR) si 0.8 se ve blando. |

Guía de uso: [GUIA.md](GUIA.md).

## 1. Resumen ejecutivo

| Pregunta | Respuesta |
|---|---|
| ¿Sube el juego la resolución interna en los diálogos? | **Muy probablemente no.** Las dos variables globales de resolución de render solo se escriben desde la inicialización y desde la función que aplica el perfil de gráficos. Esa función se ejecuta cada fotograma en el bucle principal, pero siempre lee la fila del preset elegido en Opciones. Los datos de las cinemáticas tampoco tienen ningún parámetro de resolución [VERIFICADO]. |
| Entonces, ¿por qué bajan los FPS en los diálogos? | Las escenas de diálogo encienden efectos que el gameplay normal no usa: **luces puntuales con sombra dinámica que se recalcula cada fotograma**, **luces dedicadas a personajes**, **DOF animado plano a plano** y cientos de pistas de luces animadas. Además, la cámara está muy cerca de los personajes, así que se ven con el LOD máximo [VERIFICADO en datos; el coste exacto es HIPÓTESIS]. |
| ¿Por qué bajan en los pueblos? | Los dos pueblos principales (`pc00` y `pd00`) tienen **5‑20 veces más NPCs colocados** que cualquier zona de combate (298 y 236 frente a ≤57). Además tienen miles de luces de área ambiental y un `foliage` de 2 MB. En sus filtros de zona, los flags que activan "sombra simple" y "niebla a resolución reducida" tienen máscaras distintas a las de otras zonas [VERIFICADO en datos; impacto real en FPS: HIPÓTESIS]. |
| ¿Hay forma de bajar el coste? | Sí, por dos vías. **(A) Mod de datos** (Reloaded‑II + GBFR Mod Manager, ficheros `.json`): quitar sombras dinámicas de las luces de cinemáticas, limitar volumétricas y forzar sombra simple / niebla reducida en pueblos. **(B) Mod de código** (ASI estilo GBFRelinkFix): bajar el LOD, la resolución de sombras y el DOF, y opcionalmente escalar la resolución de render. Los patrones de bytes de LOD, sombras y TAA están **comprobados en v2.0.5** [VERIFICADO]. |
| ¿Tiene el juego opciones ocultas? | Sí. `option_item.tbl` contiene Shadow Quality, Effects, Character Models, Background Models, Decal y DOF, pero están desactivadas en PC (§5.3). "Limit Number of Point Lights" / "Limit Shadow Cache" **no existen**: son textos huérfanos (corregido respecto a la primera versión). |
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
| `option_table.py` | Decodifica `option_item.tbl` y enlaza los textos mediante el hash XXHash32 propio del juego. |
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
`layout/timeline/` tiene 222 capas de escena para cinemáticas (`ci`/`ct`/`cw` `####_light|cloud|wind|localibl.scene.msg`). En ellas hay **1.412 luces puntuales con `bCastDynamicShadow_ = 1`** (sombra recalculada cada fotograma con `castDynamicShadowFrequency_ = 1`) en 58 capas. 64 están activas al cargar la escena, repartidas en 33 eventos, y el resto las enciende la propia cinemática: por ejemplo `ct0020` (4), `ct1050` (4), `ct1110` (4), `ct8040` (4), `ct9040` (5), `cid326` (5). En comparación, todo el pueblo `pd00` tiene 0 y `pc00` tiene 5. Una sombra de luz puntual cuesta hasta 6 caras de cubemap (el motor tiene `PointLightShadowCastDualParaboloid` y `PointLightShadowCastPerspective`).
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

### 5.3 Opciones del menú: visibles, ocultas y huérfanas [VERIFICADO]

> **Corrección:** la primera versión de este reporte decía que "Limit Number of Point Lights" y "Limit Shadow Cache" eran opciones del juego. **Es falso.** Esos textos (`TXT_PAU_OPT_TOG_LIMIT_PLIGHT` / `_SCASH`) existen en `text_ui.msg`, pero ni su nombre ni su hash aparecen en las tablas de opciones, en el ejecutable ni en ninguno de los 28.681 archivos de `ui/` y `system/`. Son textos huérfanos.

El menú se define en `system/table/option_item.tbl` (235 filas de 52 bytes). Los textos se enlazan por el hash XXHash32 propio del juego, que verifiqué con dos vectores conocidos. Herramienta: `tools/option_table.py`. Cada fila tiene 16 bytes de flags. Los bytes 5 y 9 están a 1 en todas las opciones que se ven en PC y a 0 en las ocultas [la semántica exacta de cada byte es HIPÓTESIS; el patrón es consistente]:

| Visibles en PC | Ocultas en PC (filas presentes, flags a 0) |
|---|---|
| Display Device, Graphics Quality, Screen Mode, Lighting, Resolution, Frame Rate, VSync, Textures, Rendering, Blur, Colorblind, Battle Effects Intensity, Player Silhouette | **Shadow Quality** (fila 60), **Effects** (69), **Character Models** (71), **Background Models** (72), **Decal** (73, valores Fast/Standard/High), **Depth/DOF** (63, On/Off), IBL (68), Reflection (75), HDR (54), DirectX (52), Benchmark (59), Graphics Mode Calidad/Rendimiento (57/58, de consola) |

La fila "Custom" de la tabla de presets (§3.2) se rellena con 19 campos de la configuración (función `0x6bb950`), así que el motor de PC sigue leyendo valores por categoría.

**Resultado de reactivarlas (prueba en Deck, 2026‑11‑02) [VERIFICADO]:** con el mod `options`, las 6 opciones aparecen en el menú y se pueden cambiar, pero **vuelven a su valor al salir del menú** y `GraphicSetting.vdat` no se modifica. El ejecutable lo explica:
- Las claves de las opciones visibles aparecen en 4‑8 sitios del código (`0x2089xx`, `0x2091xx`‑`0x209bxx`, `0x20cdxx`‑`0x20d3xx` y la tabla `0x315c1xx`). Son, previsiblemente, carga, aplicación y guardado.
- Las de Shadow (`0x2f6f51d1`), Character (`0xfbce1e5b`), Background (`0x4c1d6fce`), Effect (`0x4c37cdf9`), Decal (`0xffd48260`) y DOF (`0xf1d8b677`) aparecen **una sola vez**, en el bloque `0x20e3xx`‑`0x20e46x`, que es el registro de todas las claves.

La versión de PC no tiene código que guarde ni aplique esos valores. Desde datos no se pueden hacer funcionar. Haría falta un hook que lea el valor y además encontrar qué parámetro del render controla cada una (en consola lo hace código que en PC no está). Para Sombras y LOD ya existen equivalentes en GBFRelinkFix.

### 5.4 Cómo dar opciones configurables al mod

| Vía | Qué permite | Coste |
|---|---|---|
| **Variantes del mod de datos** (p. ej. "Diálogos sin sombras", "Pueblos ligeros") como mods separados en Reloaded‑II | Activar o desactivar cada grupo con una casilla en Reloaded‑II. No cambia en caliente. | Bajo |
| **ASI con `.ini`** (como GBFRelinkFix) | Valores numéricos (multiplicador de LOD, resolución de sombras, escala de render, DOF on/off, "modo diálogo"). Se lee al arrancar. | Medio |
| **Mod C# de Reloaded‑II con configuración** | Pantalla de configuración en Reloaded‑II, sin tocar archivos. | Medio |
| ~~Reactivar opciones ocultas del menú del juego~~ | Se ven, pero no se guardan ni se aplican en PC (§5.3). | **Descartada** |
| **Añadir opciones nuevas al menú del juego** | Opciones propias (p. ej. "Calidad en diálogos") dentro del menú. | Alto: filas nuevas en `option_item.tbl` / `option_switch_button.tbl`, textos nuevos en `text_ui.msg` y un hook que lea el valor guardado. Las tablas cambian de formato entre versiones. |

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

### 7.4 Resultado de la prueba 7.1 (Deck, sin mods, 2026‑11‑02) [MEDIDO]

Overlay de MangoHud nivel 4, juego de fábrica (v2.0.5). Las fotos están en `notes/deck_baseline/` (no van a git).

| Foto | Escena | FPS | Frametime | GPU: carga / reloj / W | CPU: carga / reloj / W |
|---|---|---|---|---|---|
| 01 | Diálogo en el barco, fondo de cielo | **30** | 33,3 ms | 73 % / 998 MHz / 3,2 W | 37 % / 3499 MHz / 6,4 W |
| 02 | Diálogo en el bosque (p100), vegetación de fondo | **26** | 39,1 ms (38,6–40,1) | **99 %** / 1453 MHz / 10,3 W | 41 % / **1399 MHz** / 1,7 W |
| 03 | Igual, plano abierto con flores y 3 personajes | **22** | 46,1 ms (45,4–47,7) | **99 %** / ~1400 MHz / 10,2 W | 36 % / 1399 MHz / 1,6 W |
| 04 | Igual, primer plano con flores detrás | **23** | 43,0 ms (42,6–44,6) | **99 %** / 1425 MHz / 10,5 W | 36 % / 1399 MHz / 1,1 W |
| 05 | Gameplay en el pueblo inicial (p100 `village`) | **30** | 33,4 ms (27,9–38,1) | 86 % / 1065 MHz / 4,6 W | 46 % / **3255 MHz** / 6,9 W |

**Diálogos: cuello de botella de GPU.** En los diálogos lentos la GPU está al 99 % y se lleva casi todo el presupuesto de 15 W. La CPU baja a 1,4 GHz y apenas consume, y el frametime es plano (carga sostenida, no tirones de carga). Los mismos dos personajes van a 30 FPS con fondo de cielo (01) y a 22‑26 con vegetación detrás (02‑04). Por eso el coste principal es **el fondo**: vegetación densa a corta distancia, más el DOF que la desenfoca y su sombra. Los personajes pesan menos. Para el peor caso hace falta recortar un ~30 % de tiempo de GPU (46 → 33 ms).

**Pueblo: limitado por CPU.** La CPU va a 3,25 GHz y la GPU tiene margen (86 % a solo 1065 MHz). Mantiene 30 FPS, pero con un frametime de 28‑38 ms, rozando el límite. Ningún núcleo llega al 100 %, lo que encaja con un hilo principal que salta entre núcleos.

**Consecuencias para el mod:**
- El pueblo inicial es `p100` (Zinkenstill), **no** `pc00`/`pd00`: el mod `towns` no lo toca. `p100` tiene 57 NPCs colocados frente a 298/236 de los pueblos grandes, así que en Folca/Seedhollow (`pc00`/`pd00`) cabe esperar bajadas reales por CPU.
- En diálogos exteriores de día, las 1.412 luces puntuales con sombra (§3.3a) probablemente no están presentes. Por tanto, el mod `dialogue` solo ayudará en interiores y escenas nocturnas. Para estas escenas, las palancas con más efecto son: **LOD/densidad de fondo** (`Background Models`, multiplicador de LOD de GBFRelinkFix), **resolución de sombras** y **DOF**. Si no basta, la vía segura es bajar la escala de render solo durante los diálogos (fase 2, `0x6bbaa0`).
- Queda abierta la 7.2 (nitidez); el patrón de GPU no la necesita para decidir.

### 7.5 Con mods (Deck, 2026‑11‑02) [MEDIDO, sin A/B en la misma escena]

Configuración: mods `dialogue` + `towns` (data.i parcheado con `tools/patch_index.py`) y GBFRelinkFix 1.1.5 (LOD 0.6, sombras 512). Render a 1280x720.

| Escena | FPS | GPU | CPU | Nota |
|---|---|---|---|---|
| Diálogos en Zinkenstill (3 capturas) | 27‑29 | 94‑99 % | 1,4 GHz | Siguen limitados por GPU; antes, en el bosque, 22‑26 (escena distinta). |
| Gameplay en Zinkenstill | 30 (29‑38 ms) | 88 % | 3,4 GHz | Igual que sin mods: límite de CPU. |
| Diálogo en Seedhollow ("Verbose Man"), matorrales detrás | 27 (36,4‑37,3 ms) | 94 % / 1427 MHz / 9,7 W | 38 % / 1,4 GHz / 1,7 W | Único diálogo que baja en esta sesión; limitado por GPU. |
| Pueblo 2 (Seedhollow, misión "Family Zathba"), partida avanzada | **30 estables** | 81 % / 4,1 W | 41 % / 6,3 W | El usuario indica que antes no mantenía 30 aquí. Overlay de nivel 1 (sin frametime). |

**Sin mods (juego de fábrica, misma partida, 21:32‑21:35):**

| Escena | FPS | Frametime | GPU | CPU |
|---|---|---|---|---|
| Seedhollow ("Gather intel on Seedhollow's army"), plaza del mercado | 30 | 29,2‑36,6 ms | 84‑91 % / 1115‑1204 MHz / 5,1‑5,9 W | 43‑46 % / 3,3 GHz / 5,8‑6,1 W |
| Diálogo "Zathba's Henchman", fondo de arbustos | 30 | 23‑42 ms | 88 % / 956 MHz / 4,4 W | 38 % / 3,5 GHz |

**Lectura:**
- El pueblo 2 era **Seedhollow**, no Folca.
- Sin mods también mantiene 30 en esta partida, con algo más de GPU (84‑91 % frente a 81 %). Los mods no son lo que lo sostiene; la mejora que se recordaba no se reproduce en estas condiciones.
- La VRAM es de 3,3 GiB con y sin mods. La bajada de 4,5 a 3,3 GiB de §7.5 se debía a la escena o sesión, no al mod (corregido).
- El diálogo de fábrica medido no es el mismo que el de 27 FPS con mods (la partida avanzó), así que no sirve de A/B. Para eso se ha dejado una partida fija en el **slot 3** (copia en `~/Documents/gbfr_benchmark_save/` en la Deck), justo antes de esos diálogos.


### 7.6 A/B con la partida fija del slot 3 [MEDIDO]

Mismo punto de la partida ("Head to the Family Zathba's base", Seedhollow).

| Escena | Sin mods | Con mods (dialogue + towns + GBFRelinkFix LOD 0.6 / sombras 512) |
|---|---|---|
| Diálogo "Verbose Man" (arbustos detrás) | 26 FPS, 38,2 ms (37,6‑39,4), GPU 99 % / 1443 MHz / 10,2 W | 27 FPS, 37,0 ms (36,4‑37,3), GPU 94 % / 1427 MHz / 9,7 W |
| Seedhollow, mercado bajo el arco (escalinata roja, NPCs, vegetación) | 27 FPS, 36,5 ms (36,0‑38,3), GPU 94 % / 1419 MHz / 9,7 W, CPU 1,4 GHz | **30 FPS**, 33,5 ms (30,6‑37,0), GPU 93 % / **1282 MHz / 7,8 W**, CPU 2,4 GHz / 3,5 W |

- **Diálogo:** los mods ganan unos 1,2 ms (~3 %). Es real pero insuficiente: faltan ~4 ms para 30 FPS.
- **Pueblo:** este punto de Seedhollow está **limitado por GPU**, no por CPU (la CPU baja a 1,4 GHz). La vegetación y la escena del mercado mandan, igual que en los diálogos.
- **Pueblo con mods:** llega al tope de 30 y la GPU necesita menos reloj y energía (1282 MHz / 7,8 W frente a 1419 MHz / 9,7 W). El presupuesto que sobra pasa a la CPU (de 1,4 a 2,4 GHz). Es la mejora más clara medida hasta ahora. La causa más probable es el LOD 0.6 y las sombras 512 de GBFRelinkFix sobre la vegetación lejana; no está separado del mod `towns`. [Cámara casi idéntica, no exacta.]

### 7.7 Fase 2: `RelinkDeckTuner.asi` (2026‑11‑02) [MEDIDO / PENDIENTE]

- **Detección de diálogo:** el gancho en `0x942698` (cámara de cinemática) se ejecuta una vez por fotograma durante el diálogo (919 llamadas en 36 s) y **0** veces caminando por el pueblo. Es una señal válida. [VERIFICADO en el log]
- En la Deck, la fila de preset activa es la 5 (Custom) con `1280,720,1280,720`. Las globales `0x6b822e0..ec` son iguales.
- Bajada de resolución solo en diálogo (`Mode = 1`, `Scale = 0.75`), activada en caliente a las 21:56. **Funciona:** al empezar el diálogo, la fila y las globales pasan a `960,540,960,540` y el render baja.
  - Diálogo "Bon Vivant" (vegetación detrás): **30 FPS estables, 33,3 ms (31,5‑35,6)**, GPU 80 % / 956 MHz / 4,0 W. Antes, diálogos así iban a 26‑27 FPS con la GPU al 99 % y 10 W.
  - El usuario lo ve "más borroso pero manejable".
  - Como sobra mucha GPU, se sube a `Scale = 0.85` (1088x612) para el siguiente diálogo.
  - Con `Fields = 2` (solo el segundo par a 0.85), el diálogo "Verbose Man" siguió a **26 FPS, GPU 99 %**: el segundo par por sí solo no reduce el render. Se vuelve a `Fields = 3`. [El log de esa sesión se sobrescribió al reiniciar; desde la v0.2 el log se acumula.]
  - `Fields = 3`, `Scale = 0.85` (1088x612), "Verbose Man": **30 FPS**, 33,4 ms pero inestable (24,8‑41,9), GPU 86 % / 1469 MHz / 9,6 W. Al límite. Se prueba 0.8 (1024x576).
  - `Scale = 0.8` (1024x576), "Verbose Man": **30 FPS**, 33,4 ms (28,9‑36,7, más estable), GPU 85 % / 1334 MHz / 7,4 W, CPU 3,0 GHz. **Valor recomendado** para diálogos.
  - `Mode = 2` (siempre), `Fields = 3`, `Scale = 0.8`: **todo** se ve a baja resolución, también la interfaz y los textos. Con los dos pares, la UI se dibuja a la resolución reducida (igual que en los diálogos). Siguiente prueba: `Fields = 1` (solo el primer par), por si es el del 3D y el segundo el de la UI/salida. `0x23a0dc4` usa el primer par para elegir el tamaño de un buffer a mitad o a resolución completa (960x540 / 1920x1080).
  - **`Fields = 1` (solo el primer par) = solo el 3D.** La UI y los textos se quedan nítidos; el 3D se dibuja más pequeño. El primer par es la resolución de la escena 3D y el segundo la de la UI/salida. Con 0.8 (1024x576), el usuario lo ve "muy bajo" de resolución. Se prueba 0.9 (1152x648) en caliente.
  - **Modo "siempre" (`Mode = 2`, `Fields = 1`), guardado para más adelante:** 0.9 (1152x648) y 0.95 (1216x684) se ven aceptables, con un solo parpadeo al arrancar; 0.8 se ve demasiado bajo. A 0.95 la ganancia es pequeña (~10 % de píxeles). Por ahora se deja en 1.0.
  - **Configuración elegida (22:30):** `Mode = 1`, `Scale = 0.8`, `Fields = 1`, es decir, solo el 3D baja a 1024x576 durante los diálogos y la UI sigue nítida. Pendiente de medir y de ver si el parpadeo es menos visible al cambiar solo el 3D.
  - Con solo el 3D en diálogos (`Fields = 1`, 0.8), el **parpadeo es casi nulo** según el usuario. Se sube a `Scale = 0.9` (1152x648) en diálogos.
  - Solo 3D a 0.9 (1152x648), "Verbose Man": **30 FPS**, 33,1 ms (32,1‑35,0), GPU 89 % / 1411 MHz / 9,4 W. Cumple, pero con poco margen. Se prueba 0.85.
  - **Problema: parpadeo negro** al cambiar de resolución, también cambiando solo el segundo par (`Fields = 2`). Causa: las globales de tamaño se usan para **crear** los render targets (p. ej. `0x1da6ce` copia el tamaño en un descriptor de textura, `0x26dac6` reserva buffers de `ancho×alto×3`). Cambiar el tamaño obliga a recrearlos y se pierde al menos un fotograma (y el historial de TAA). No es un fallo del mod: es inherente a cambiar el tamaño en caliente. Para evitarlo hay dos vías:
    - cambiar solo cuando la pantalla ya está en negro (pantallas de carga entre zonas);
    - un escalado por viewport dentro del motor, que es mucho más complejo.

**Pendiente: pueblos/ciudad.** Con `LOD Multiplier = 0.75` (antes 0.6), el usuario nota **más tirones en el pueblo** y un **uso muy alto en la ciudad** (Seedhollow). Hay que reducir el coste ahí sin volver a 0.6 en todas partes. Opciones:
- LOD por zona: 0.6 solo en pueblos.
- Aplicar también la bajada de resolución en pueblos (necesita detectar la zona).
- Reducir NPCs (`npcConfig`, §4.2).

### 7.8 Prueba por eliminación de efectos (2026‑11‑02) [PENDIENTE]

Con la escala de resolución sola no basta: algunas escenas siguen en 26 FPS incluso a 0.8, y se ve mal. El usuario juega con todos los ajustes gráficos al mínimo (Standard).

`tools/build_effects.py` apaga componentes completos de post‑proceso (`<Componente>.active_ → false` en todos los `filter/*.ccomp.msg`), por grupos:

| Grupo | Componentes | Fotogramas clave apagados (todas las zonas) |
|---|---|---|
| snn | SymmetricNearestNeighborParameter (filtro pictórico) | 145 |
| dof | DofParameter | 168 |
| aal | AmbientAreaLightParameter (hasta 2.048 luces por pasada en Seedhollow) | 172 |
| sky | SkyCloudSdfParameter (nubes) | 69 |
| post | Bloom, MotionBlur, ColorShadow, DropShadow | 171 / 158 / 13 / 130 |
| plant | PlantParameter (viento en vegetación) | 68 |

Referencia previa: diálogo de Lyria ("No wonder you're a master swordswoman", Zinkenstill) con toda la imagen a 0.85 (`Fields = 3`): **26 FPS**, 37,9 ms, GPU 99 % / 1385 MHz / 10,2 W. La resolución sola no lo salva.

Primera variante `fx_all`: todos los grupos apagados, sobre `dialogue` + `towns` y con `RelinkDeckTuner` en `Mode = 0`. Sirve para medir el techo de ganancia. `patch_index.py` admite ahora que un mod posterior sustituya un archivo de otro: `fx_all` reescribe los filtros de `towns` conservando sus cambios (verificado: solo difieren los `active_`).

**Resultado `fx_all`** ("Verbose Man", sin escala de resolución): **28 FPS**, 36,0 ms (35,5‑36,5), GPU 99 % / 1415 MHz / 10,5 W. Con solo `dialogue` + `towns` eran 27 FPS / 37,0 ms; de fábrica, 26 / 38,2. **Todos esos efectos juntos cuestan ~1 ms** y el juego se ve "horrible" sin ellos. Descartado: el coste está en la geometría (vegetación, personajes, sombras), no en el post‑proceso.

**Error detectado en nuestra configuración:** el usuario juega con Lighting Quality = Standard, cuya resolución de sombras por defecto es **256** (según el `.ini` de GBFRelinkFix: Ultra 2048, High 1024, Standard 256). Al poner `Resolution = 512` en GBFRelinkFix estábamos **duplicando** la resolución de sombras respecto a su preset. Siguiente prueba: sombras 128 y LOD 0.4, para medir el peso de la geometría y las sombras.

**Resultado sombras 128 + LOD 0.4** ("Verbose Man", sin escala de resolución): **27 FPS**, 36,5 ms (36,1‑37,9), GPU 99 %. Igual que con sombras 512 / LOD 0.75. **Ni la resolución de sombras ni el LOD influyen** en este diálogo. Se dejan sombras 256 (el valor de Standard) y LOD 0.75.

**Conclusión de 7.6‑7.8:** en los diálogos pesados, el coste es por píxel de la geometría cercana: hojas con transparencia superpuestas, sombreado de personajes y luz diferida. El post‑proceso (~1 ms), las sombras y el LOD apenas cuentan. Lo único que ha movido la cifra es reducir píxeles (escala de resolución), y aun así hay escenas (Lyria) que se quedan en 26 FPS a 0.85.

### 7.9 Perfilado con RenderDoc del diálogo "Verbose Man" [MEDIDO en PC]

El juego usa **Direct3D 11**. Captura en el PC (AMD Radeon integrada, misma arquitectura que la Deck; Standard, 1280x720, mismo punto del slot 3). RenderDoc no puede lanzar el juego: el proceso se protege (ni `taskkill` puede cerrarlo). Se cargó desde dentro con el ASI loader y un `.asi` mínimo que hace `LoadLibrary(renderdoc.dll)`. Herramientas: `tools/rdc_pass_costs.py` (coste GPU por draw) y `tools/rdc_inspect.py` (estado de pipeline, shader e imágenes antes/después). Resultados en `notes/renderdoc/`.

| Pase | GPU | % del fotograma |
|---|---|---|
| **G‑buffer** (4 targets 1280x720 + profundidad) | **45,4 ms** (85 draws) | 64 % |
| Mapa de sombras 2048x2048 | 8,0 ms (208 draws) | 11 % |
| Iluminación / post‑proceso (todo lo demás) | ~17 ms | 25 % |
| **Total** | **70,5 ms** | |

Dentro del G‑buffer, **un solo draw (EID 2326, malla de 9.366 índices × 19 instancias) cuesta 21,9 ms (31 % del fotograma)**: son los arbustos y árboles detrás del personaje (`notes/renderdoc/vegetation_draw_2326.png`). Cubren solo el 18,5 % de la pantalla. Los 5 draws más caros (21,9 + 3,6 + 3,3 + 3,1 + 2,9 ms ≈ **35 ms, la mitad del fotograma**) usan **el mismo pixel shader de vegetación**:
- alpha test con `discard`, sin culling (doble cara), profundidad con escritura;
- texturas virtuales Granite: tabla de páginas, caché de albedo, normal y máscara en arrays de 16272x12384;
- **escribe feedback de streaming en 3 UAV** (`g_ResolveParam0..2`) antes del `discard`, y luego hace un segundo `discard` por dither (`g_DitherTex`).

Con `discard` + UAV, la GPU no puede descartar píxeles ocultos antes del shader (early‑Z). Cada capa de hojas superpuesta paga la búsqueda en tabla de páginas, la muestra de albedo y la escritura UAV. El coste es **sobredibujado de vegetación**. Esto explica todas las pruebas anteriores: escalar la resolución ayuda (menos píxeles), y ni el post‑proceso, ni las sombras, ni el LOD lo hacen (los arbustos están cerca).

Modelos candidatos por número de índices (por buscar a qué zona pertenecen; los layouts de `pd00` no los nombran como texto): 9366 → `bg012c`, `bg2084`, `bg6735`, `bga238`.

### 7.10 Vegetación más ligera por umbrales de LOD [MEDIDO: resuelve el diálogo]

El draw caro (9.366 índices) coincide con un trozo del LOD0 de **`bg012c`**, un árbol mediano con materiales `mediumtree_*` y `leaf_01`. Sus LODs tienen 14.580 / 10.230 / 5.430 / 2.952 índices. En `.minfo` (esquema `MInfo_ModelInfo.fbs` de GBFRDataTools), el campo `lod_screen_size_thresholds` decide qué LOD se usa según el tamaño en pantalla, y se lee al cargar la zona. `tools/minfo.py` lo lee.

`tools/build_foliage.py` detecta como vegetación los modelos `bg`/`ba` cuyo `.mmat` nombra texturas de hojas, arbustos, hierba, etc. A esos modelos les sube a 1000 los umbrales de los LOD 0..N, para que no puedan elegirse de cerca. Solo cambian 4 bytes por umbral, y se comprueba. Se excluyen los modelos sin un LOD final alcanzable.

Primera prueba (`--level 2`): 243 modelos de vegetación usan como mucho el LOD2 (≈37 % de los índices en `bg012c`). Es para medir la ganancia máxima; si funciona, se ajusta a LOD1. Instalado en la Deck junto a `dialogue` + `towns`, con la escala de resolución apagada.

**Resultado (Deck, "Verbose Man", resolución nativa 1280x720, sin escala):** **30 FPS**, 33,3 ms (28,9‑37,0), GPU **85 % / 1283 MHz / 7,3 W**, CPU 3,2 GHz. Antes: 27 FPS, 36,5‑37 ms, GPU 99 % / 10,4 W. **Es la primera vez que este diálogo llega a 30 sin bajar la resolución.** La GPU incluso queda con margen y cede energía a la CPU. El usuario lo da por bueno ("perfecto"). Queda como paquete principal de la Deck: `dialogue` + `towns` + `foliage_lod2` + GBFRelinkFix (sombras 256, LOD 0.75) + `RelinkDeckTuner` en `Mode = 0`.

**Pero:**
- El diálogo de Lyria (slot 1, Zinkenstill) con `foliage_lod2` y resolución nativa da **24 FPS**, 40,9 ms, GPU 94 %. Antes, con toda la imagen a 0.85, eran 26. En esa escena la vegetación aligerada no basta; habrá que perfilarla aparte.
- El usuario ve los árboles **"muy quietos"**: los LOD bajos no tienen la animación de viento. LOD2 es demasiado agresivo.
- Se vuelve a la vegetación original (`dialogue` + `towns`) para que el usuario compare el aspecto.

### 7.11 Perfilado del diálogo de Lyria (slot 1, Zinkenstill) [MEDIDO en PC]

Captura `gbfr_frame5082`, con vegetación original. Fotograma de 140 ms en el PC (1.771 eventos).

| Pixel shader | Tiempo | % | Draws | Qué es |
|---|---|---|---|---|
| vegetación (alpha test, 341 líneas) | 40,7 ms | 26,8 % | 67 | árboles: `bg012c` (9.366 idx × 9 = 12,5 ms) y otros de 8.070 y 5.334 índices |
| opaco estático (475 líneas, sin discard) | 26,5 ms | 17,4 % | 208 | escenario general |
| opaco estático (543 líneas) | 15,8 ms | 10,4 % | 110 | escenario general |
| alpha test (515 líneas) | 7,8 ms | 5,1 % | 40 | |
| resto | ~61 ms | ~40 % | | sombras (12 ms), prepaso, iluminación, post |

En el diálogo del Verbose Man, la vegetación era el **49 %** del fotograma. Aquí es el **27 %**: Lyria tiene mucho escenario opaco (casas, heno, suelo) con shaders largos. El coste de lo opaco es "píxeles × shader" (sin discard, el early‑Z funciona), así que ahí solo ayuda reducir píxeles (escala de resolución). Por eso `foliage_lod2` dejó Lyria en 24 FPS.

**Sobre el LOD de GBFRelinkFix:** su multiplicador escala las **distancias** de cambio de LOD, para que los objetos lejanos cambien antes o después. Un árbol a pocos metros ocupa media pantalla y sigue en LOD0 con cualquier multiplicador razonable (0.4 no cambió nada, §7.8). `build_foliage.py` cambia los umbrales de cada modelo de vegetación, así que afecta también a los objetos cercanos.

**Prueba combinada en Lyria** (`foliage_lod1` en 606 modelos + 3D a 0.9 en diálogo, aplicado según el log): **21 FPS**, 48,1 ms, GPU 94 % a **1600 MHz pero solo 5,4 W**, 67 °C, CPU 2,4 GHz, justo después de despertar la Deck. Peor que los 24 FPS con `foliage_lod2` sin escala. Ese consumo tan bajo a reloj máximo no encaja con las sesiones anteriores (~10 W a 1,4 GHz). Las mediciones entre sesiones no son comparables. Hace falta un A/B en la misma sesión, cambiando solo una cosa.

Con `foliage_lod1`, **los árboles sí se mueven con el viento** (confirmado por el usuario). LOD1 conserva la animación; LOD2 no.

Tras reiniciar la Deck, la misma prueba (`foliage_lod1` + 3D a 0.9) da **27 FPS**, 36,8 ms (36,0‑37,3), GPU 94 % / 1426 MHz / 9,7 W. Los 21 FPS anteriores eran un estado anómalo tras la suspensión. Faltan ~3,5 ms; se prueba `Scale = 0.85` en caliente.

Con `Scale = 0.85` (1088x612): **30 FPS**, 33,7 ms (33,3‑34,1), GPU 99 % / 1441 MHz / 10,4 W. Llega a 30, pero justo. El usuario lo ve "en el límite de aceptable": **el reescalado del juego produce bandas horizontales**. Parece un muestreo de punto o sin filtrar al estirar 612 → 720 líneas (factor 1,18, no entero). Mejorar ese filtro (lineal o FSR1) atacaría la causa del mal aspecto; hay que localizar el pase de reescalado en una captura con la escala activa.

### 7.12 Reescalado: el "TUP" del juego produce las bandas; sustituido por FSR1 [VERIFICADO en PC]

**Diagnóstico** (captura automática con el 3D a 0.85, `auto_frame2435`):
- Con la escala activa, todo el G‑buffer y la iluminación se dibujan a 1088x612. Un único pase, **EID 5219** (pixel shader DXBC `0e4625da…`, ~215 instrucciones), pasa la imagen a 1280x720 antes de dibujar la UI.
- Ese shader es el **temporal upsampler** del motor (`ParamBuffer`: `orgScreen` = tamaño de origen, `isOddFrame_`, `isStopTUP_`). Reconstruye columnas/filas alternas entre fotogramas pares e impares. En la captura, `isStopTUP_ = 0` (activo).
- **No usa sampler**: todas las lecturas son `ld` (texel exacto, sin filtrar) con coordenadas `ftoi(uv * orgScreen)`. Con factores no enteros (612 → 720 = 1,18) repite filas: son las **bandas horizontales** que veía el usuario. Con `isStopTUP_ = 1` hace un `ld` directo (vecino más cercano, mismo problema).
- Medido: la salida real del TUP difiere de un nearest‑neighbour en solo 1,1‑1,3/255 de media. En la práctica no reconstruye nada útil en estas escenas.

**Solución (RelinkDeckTuner 0.5, `Upscaler` en el `.ini`):**
- El `.asi` engancha `D3D11CreateDevice` en la tabla de importaciones del juego (sin parchear código).
- Del dispositivo, engancha `ID3D11Device::CreatePixelShader` (vtable, índice 15).
- Cuando el juego crea **exactamente** ese shader (checksum DXBC), crea en su lugar uno propio con las mismas ligaduras (`t3`, `cb1.orgScreen`, firma `SV_Position` + `TEXCOORD0`). Cualquier otro shader pasa sin tocar.
  - `Upscaler = 1`: **AMD FidelityFX FSR 1.0 EASU** (`asi/third_party/fsr1`, licencia MIT). El `Gather4` se emula con `Load`, así que no necesita sampler. Se lee cada 2x2 una vez (las llamadas R/G/B reutilizan los texels).
  - `Upscaler = 2`: bicúbico Catmull‑Rom (16 `Load`). A escala 1:1 devuelve exactamente la entrada (verificado: diferencia 0).
  - `Upscaler = 0`: el TUP original.
- A resolución nativa ese pase no se ejecuta, así que la sustitución no cambia nada fuera de la escala.

**Validación:**
- Arnés D3D11 propio (`asi/test/upscale_test.cpp`) que ejecuta cada `.cso` como el juego, sobre la textura de entrada real extraída de la captura.
- Comparativas ×3 en `notes/upscaler/`: con TUP y nearest se ven dientes de sierra en mandíbula y pelo; el bicúbico es suave; **FSR1 da bordes limpios y nítidos**.
- Coste en la GPU del PC (Radeon integrada), 1088x612 → 1280x720: nearest 0,07 ms, bicúbico 0,45 ms, **FSR1 0,85 ms**. El TUP original costaba 2,16 ms en la captura (contador de RenderDoc).
- En el juego (PC): el log muestra `temporal upsampler shader replaced by FSR1 EASU: hr=0x00000000`, y los diálogos de Lyria se ven sin bandas con 0.85.

### 7.13 Matriz de rendimiento automatizada en PC (2026‑11‑03) [MEDIDO]

**Método** (`tools/pcbench/`):
- `drive.py` restaura las partidas de referencia y arranca el juego. Reconoce cada pantalla comparando capturas con miniaturas de referencia (y los bordes del texto "Main Menu" del HUD), carga el slot y dispara el diálogo:
  - slot 1: Lyria, *"No wonder you're a master swordswoman!"*, Folca;
  - slot 3: el Verbose Man, Seedhollow.
- Lee `dialogueFps` del log de RelinkDeckTuner (la cámara de cinemática se actualiza una vez por fotograma) durante 15 s y toma la mediana.
- Las teclas solo se envían si el juego tiene el foco.
- `matrix.sh` instala cada combinación de datos y `.ini`, mide los dos diálogos y deja el juego de fábrica al final.
- Ajustes: los de la Deck (Standard, 1280x720), con el límite de FPS subido a 120 **solo en el PC**, para medir margen y no el tope de 30.
- GPU: Radeon integrada del PC, más lenta que la de la Deck. Las cifras sirven para comparar configuraciones entre sí.
- Ruido: la línea base repetida dio 18/25 y 19/25 FPS (±1 FPS).

| Configuración | Lyria (slot 1) | Verbose Man (slot 3) | Ganancia media (tiempo de fotograma) |
|---|---|---|---|
| A. Nativo (fábrica) | 18‑19 | 25 | — |
| B. 3D 0.85, TUP del juego | 22 | 31 | −19 % |
| C. 3D 0.85, **FSR1** | 23 | 32 | −22 % |
| D. 3D 0.85, bicúbico | 23 | 32 | −22 % |
| E. 3D 0.75, FSR1 | 27 | 38 | −33 % |
| F. 3D 0.9, FSR1 | 21 | 29 | −13 % |
| G. Mods de datos `dialogue` + `towns` | 19 | 25 | ~0 % |
| H. Vegetación LOD1 | 21 | 31 | −15 % |
| **I. Vegetación LOD1 + 3D 0.85 FSR1** | **25** | **39** | **−31 %** |
| J. Vegetación LOD2 | 22 | 36 | −22 % |

**Conclusiones:**
- **FSR1 no cuesta rendimiento** respecto al TUP del juego (incluso +1 FPS) y quita las bandas. El bicúbico rinde igual pero es más blando.
- Los mods de datos `dialogue` + `towns` no mueven estos dos diálogos (no tienen luces puntuales con sombra ni son zonas `pc00`/`pd00` afectadas por filtros).
- La vegetación ayuda mucho más en Seedhollow (Verbose Man, −20 %) que en Folca (Lyria, −10 %). Coincide con el perfilado: la vegetación es el 49 % del fotograma del Verbose Man y el 27 % del de Lyria.
- Las dos palancas se suman: **LOD1 + 3D 0.85 FSR1** da −31 % de tiempo de fotograma. En la Deck esa combinación midió 30 FPS en Lyria (§7.11), lo que cuadra con 22‑23 FPS nativos × 1,33.

**Segunda tanda** (con FPS de campo: caminando 12 s antes del diálogo, contador de fotogramas del bucle principal; tope 120 en PC):

| Configuración | Lyria | Verbose Man | Campo Folca | Campo Seedhollow |
|---|---|---|---|---|
| Nativo | 18 | 25 | 45 | 30 |
| Vegetación LOD1 | 21 | 31 | 50 | 32 |
| LOD1 + FSR1 0.85 | 25 | 39 | — | — |
| LOD1 + FSR1 0.8 | 26 | 43 | 48 | 33 |
| LOD1 + FSR1 0.75 | 30 | 47 | 51 | 34 |
| Vegetación LOD2 | 22 | 36 | 52 | 37 |

La vegetación ligera también ayuda fuera de los diálogos, sobre todo en Seedhollow (30 → 32 con LOD1, 37 con LOD2). Resultados en bruto: `notes/bench/results_raw.txt`.

### 7.14 Selección de LOD del motor y `DialogueLodBias` [VERIFICADO; ganancia pequeña]

**Cómo se encontró:**
- `asi/research/lodtrace.cpp`, un `.asi` de investigación solo para PC, busca en memoria los umbrales de `bg012c` {1.0, 0.6, 0.24, 0.2} y pone watchpoints hardware (DR0‑DR3) en todos los hilos.
- Las copias "en bruto" del `.minfo` (5) no se leen nunca después de cargar la zona, lo que confirma "only read once".
- Hay 2 copias derivadas (precedidas por 5.0) que se leen **~930 veces/s** desde `exe+0x23a4bc6`.

**El bucle de selección** (`0x23a4b97‑0x23a4c03`):
- Multiplica el tamaño en pantalla del modelo por un factor de la tabla `.rdata` `exe+0x54abf98` = {0.25, 1.0}. El índice lo da un flag del objeto, y la tabla tiene una sola referencia en todo el ejecutable.
- Recorre los umbrales y elige el primer LOD cuyo umbral queda por debajo, con histéresis respecto al LOD actual.

**`DialogueLodBias`** (RelinkDeckTuner 0.8):
- Escribe ese 1.0 (4 bytes, solo si la tabla coincide byte a byte) mientras la cámara de cinemática está activa, y lo restaura al salir.
- Medido: 0.5 → Lyria 20 FPS y Verbose Man 26; 0.3 → 21 y 26. Sumado a LOD1 + FSR1 0.85 → 27 y 40 (antes 25 y 39).
- **Coste visible**: a 0.3 las balas de heno de Folca pasan a cajas planas (`notes/bench/lodbias_030_vs_nativo.png`).
- Los arbustos caros del Verbose Man **no cambian**: se dibujan con `DrawIndexedInstancedIndirect` (instancias en GPU), cuyo LOD no pasa por este bucle. Solo responden a los umbrales del `.minfo` (`build_foliage.py`).
- Conclusión: se deja como opción, **desactivada por defecto (1.0)**.

### 7.15 Configuración recomendada para la Deck

| Pieza | Valor | Por qué |
|---|---|---|
| Vegetación (`foliage_lod1`) | 606 modelos empiezan en LOD1 | −15 % de tiempo de fotograma en diálogos con vegetación; conserva el viento (LOD2 lo pierde). También ayuda caminando. |
| RelinkDeckTuner | `Mode = 1`, `Scale = 0.85`, `Fields = 1`, `Upscaler = 1` | Solo el 3D baja en diálogos, la UI queda nítida, casi sin parpadeo, y FSR1 sin bandas. Con LOD1, Lyria llegó a 30 en la Deck. |
| `DialogueLodBias` | 1.0 (desactivado) | Poca ganancia y cambio visible. |
| Mods `dialogue` + `towns` | incluidos | No afectan a estos diálogos. Son baratos y actúan en interiores/noches y en `pc00`/`pd00`. |
| GBFRelinkFix | sombras 256 (= Standard), LOD 0.75 | Sombras como el preset; el LOD 0.75 es elección del usuario (con 0.6 había menos tirones en pueblos). |

Si una escena concreta sigue por debajo de 30, el siguiente paso es `Scale = 0.8`. En PC aporta +10 % de FPS en el Verbose Man y +4 % en Lyria, a cambio de algo más de blandura (sin bandas, gracias a FSR1).

### 7.16 Pueblos: tirones y densidad de NPCs (Deck, 2026‑11‑03) [MEDIDO]

**RelinkDeckTuner 0.9** mide cada fotograma. Un trampolín en la entrada de la función por fotograma (`0x6bbaa0`) lee `RDTSC`. Cada `LogPeriodMs` escribe el fotograma más largo (`maxFrameMs`) y cuántos superan 40 ms (`slow40`), con el TSC calibrado contra `QueryPerformanceCounter`. En PC, el menú a 60 FPS mide exactamente 16,7 ms por fotograma.

Paseo de ~1,5 min por Seedhollow (slot 3), mismo recorrido, paquete final (vegetación LOD1, etc.):

| | FPS (mediana) | Fotograma más largo cada 2 s: mediana / p90 | Tirones > 40 ms (fuera de cargas) |
|---|---|---|---|
| NPCs 100 % (`density_` 5.10) | 29,9 | 33,5 / 34,2 ms | 152 ms, 42 ms, 41 ms |
| NPCs 80 % (`density_` 4.08) | 29,9 | 33,5 / 34,3 ms | 41 ms |

El salto de ~370‑390 ms al principio de cada sesión es la carga. Con el paquete final, **Seedhollow va a 30 FPS estables y casi sin tirones**. Bajar la densidad de NPCs al 80 % no cambia nada medible, y el usuario lo ve "casi igual". Al principio del proyecto el frametime en pueblo oscilaba entre 28 y 38 ms: la mejora viene de la vegetación ligera, no de los NPCs. Se mantiene la densidad original.

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

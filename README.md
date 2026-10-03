# eCU-10 MST

Ecualizador de mastering (Audio Unit y VST3) hecho con JUCE. Es la versión para mastering de **eCU-10**: mismo aspecto de hardware analógico (Studer / grafito y ámbar) y un motor pensado para trabajar con precisión.

## Qué incluye

**1. Fase y precisión**
- Tres modos de fase: **mínima** (sin latencia), **natural** (fase parcial, menos pre-eco) y **lineal** (sin desfase entre frecuencias). Los filtros FIR se recalculan al mover los controles y la latencia se informa al host.
- **Calidad** de la fase natural/lineal en tres niveles (Low, Medium, High): menos o más latencia y resolución en graves. Los cambios de fase, calidad o sobremuestreo se hacen con un fundido corto, sin clics.
- Todo el cálculo de filtros y el procesado es en **doble precisión**.
- **Suavizado de coeficientes**: automatizar o mover un control no produce clics.

**2. Resolución de control**
- Pasos de 0,1 dB; **Mayús + arrastrar** (knobs y puntos de la curva) para ajuste fino; clic en el valor para escribir un número.
- Rango de los knobs de ganancia conmutable: ±18 / ±12 / ±6 / ±3 dB.

**3. Estéreo y Mid/Side serio**
- Cada banda actúa en **estéreo, Mid, Side, solo izquierdo o solo derecho**.
- **Graves en mono** (paso alto de 24 dB/oct sobre el Side) y **anchura** estéreo.
- **Goniómetro** y **correlación** estéreo.

**4. Medición**
- **LUFS** momentáneo, corto plazo e integrado (ITU-R BS.1770 / EBU R128) y **true peak** (4x).
- Analizador con tres resoluciones (hasta FFT de 32768), suavizado por 1/6 y 1/3 de octava y línea de **pico máximo**.
- **Delta**: se oye solo la diferencia entre el procesado y el original (con el volumen igualado y alineado en el tiempo).
- **Monitorización**: suma a mono, intercambio L/R e inversión de polaridad de cada canal (los medidores miden la señal del programa), y **filtro DC** a la entrada.
- **Bypass** con el original retardado lo mismo que el procesado, y **Igualar volumen** (compensa el loudness para que la comparación A/B sea justa).

**5. Audio / procesado**
- Saturación opcional (cinta / válvula) con **sobremuestreo 2x o 4x**, apagada por defecto, con mezcla en paralelo sin efecto peine.
- **Dither** TPDF a 16 o 24 bits.
- **EQ dinámico** por banda con **compresión o expansión**, detector de **pico o RMS** y **sidechain externo**.
- Compatible hasta **192 kHz**.

**6. Flujo de trabajo**
- 8 bandas: paso alto, shelf de graves, cuatro medias, shelf de agudos y paso bajo (6/12/24/48 dB/oct).
- Tipos de banda: campana, **notch**, shelf, **Pultec** (realce y atenuación a la vez), **tilt** y **Baxandall**.
- **Solo de banda** (S), **A/B/C/D** de ajustes, **deshacer / rehacer** y presets de fábrica y de usuario.

## Instalación (macOS)

Descarga el artefacto de la última ejecución de *Actions*, descomprime y copia:

- `eCU-10 MST.component` a `~/Library/Audio/Plug-Ins/Components/`
- `eCU-10 MST.vst3` a `~/Library/Audio/Plug-Ins/VST3/`

Como no está firmado, quita la cuarentena (escribe `xattr -cr ` y arrastra el archivo a la Terminal):

```
xattr -cr "/ruta/a/eCU-10 MST.component"
```

## Compilación y pruebas

El flujo de trabajo `Construir plugin eCU-10 MST` (manual) compila, comprueba la latencia en 24 casos (`docs/latencia.txt`), ejecuta las pruebas funcionales (`docs/pruebas.txt`), valida con `auval` y `pluginval` y sube el resultado como artefacto. Con la opción *screenshot* guarda capturas de la interfaz en `docs/`.

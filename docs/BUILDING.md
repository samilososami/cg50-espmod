# Compilación, pruebas e instalación

## Dependencias

- PrizmSDK para fx-CG50 en `/opt/prizmsdk-linux` o `$FXCGSDK`.
- `arduino-cli` con el core `esp32:esp32` y la placa `XIAO_ESP32C3`.
- Biblioteca Arduino `ArduinoJson`.
- GCC/G++, Python 3 y Pillow.

Ejemplo para preparar Arduino CLI:

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install ArduinoJson
```

Los scripts detectan UID 0 y reanudan la compilación como el usuario `kali`.
Así los mismos comandos funcionan como usuario normal y root sin mantener dos
cores ESP32 ni dejar artefactos con propietarios distintos.

## Compilar

```bash
./tools/build-addins
./tools/build-firmware
./tools/checksums
```

Los resultados quedan en `dist/`:

- `CASIOWIFI.g3a`
- `cg50-espmod-esp32-app.bin`
- `cg50-espmod-esp32-merged.bin`
- `checksums.txt`

CasioGPT se compila y publica desde su
[repositorio independiente](https://github.com/samilososami/CasioGPT).

## Pruebas de host

```bash
./tools/test
```

La batería usa AddressSanitizer y UndefinedBehaviorSanitizer para comprobar:

- 100 escaneos simulados de extremo a extremo.
- UART fragmentada, checksums dañados, respuestas antiguas y reapertura.
- Conexión abierta/protegida, NVS, credenciales y cancelación.
- Arbitraje entre escaneo y reconexión, reintentos y timeout.
- Las siete pantallas CasioWIFI a 384×216.
- Regresión del protocolo cloud que consumen clientes externos como CasioGPT.
- Ausencia de credenciales con formato sensible en fuentes y binarios.

Las pruebas de host no sustituyen una validación física del enlace UART, la
radio Wi-Fi ni la salida/reapertura del add-in.

## Flashear la XIAO

```bash
./tools/flash-esp32 /dev/ttyACM0
```

El hostname queda como `casio-cg50`. El mismo firmware sirve a CasioWIFI y a
clientes compatibles con el protocolo documentado en `docs/PROTOCOL.md`.

## Instalar CasioWIFI

Pon la fx-CG50 en modo USB Flash y ejecuta:

```bash
./tools/install-calculator
```

El instalador:

1. recompila CasioWIFI;
2. identifica una única unidad Casio;
3. guarda una copia local de nombres anteriores y del add-in actual;
4. escribe mediante archivo temporal y sincroniza;
5. compara SHA-256;
6. informa del espacio libre y desmonta limpiamente.

No elimina ni modifica `CASIOGPT.g3a` ni ningún otro add-in ajeno a CasioWIFI.

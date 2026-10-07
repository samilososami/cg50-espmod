# Compilación, pruebas e instalación

## Dependencias

- PrizmSDK para fx-CG50, disponible mediante `$FXCGSDK` o en
  `/opt/prizmsdk-linux`.
- `arduino-cli` con el core `esp32:esp32` y la placa `XIAO_ESP32C3`.
- Biblioteca Arduino `ArduinoJson`.
- Compiladores C/C++, Python 3 y Pillow.
- Opcional: `pyserial` para la sonda física de Ollama Cloud.

Ejemplo para preparar Arduino CLI cuando ya está instalado:

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install ArduinoJson
```

Los scripts detectan UID 0 y reutilizan el perfil Arduino de `kali`, por lo que
los mismos comandos funcionan como usuario normal y como root sin duplicar el
core ESP32 ni generar artefactos propiedad de root.

## Compilar

```bash
./tools/build-addins
./tools/build-firmware
./tools/checksums
```

Los resultados revisables quedan en `dist/`:

- `CASIOWIFI.g3a`
- `CASIOGPT.g3a`
- `cg50-esp32mod-esp32-app.bin`
- `cg50-esp32mod-esp32-merged.bin`
- `checksums.txt`

## Pruebas de host

```bash
./tools/test
```

La batería ejecuta con AddressSanitizer y UndefinedBehaviorSanitizer:

- 100 escaneos simulados de extremo a extremo.
- UART fragmentada, bytes iniciales/finales perdidos, checksum dañado,
  respuestas antiguas y reapertura del puerto.
- Conexión abierta/protegida, NVS, reuso seguro de credenciales y cancelación.
- Subida fragmentada de API key/prompt, offsets de streaming, historial y F6.
- Render nativo de CasioGPT a 384×216.
- Búsqueda de la API key real o de tokens con formato sensible en fuentes,
  documentación y binarios.

Las pruebas de host no sustituyen la validación física de UART, Wi-Fi, TLS y la
salida/reapertura del add-in.

## Flashear la XIAO

```bash
./tools/flash-esp32 /dev/ttyACM0
```

El hostname queda como `casio-cg50`. El firmware único sirve simultáneamente a
CasioWIFI y CasioGPT; actualiza las dos apps y el firmware juntos cuando cambie
la versión del protocolo.

## Instalar en la CG50

Pon la calculadora en modo USB Flash y ejecuta:

```bash
./tools/install-calculator
```

El instalador:

1. recompila ambos add-ins;
2. identifica una única unidad Casio;
3. guarda copias locales de nombres anteriores (`CASIOESP.g3a`, `CWIFI.g3a`) y
   de los add-ins actuales;
4. escribe mediante archivo temporal, sincroniza y compara SHA-256;
5. instala opcionalmente `casiogpt_api.txt` sin imprimirlo;
6. informa del espacio libre y desmonta limpiamente.

La API key puede estar en `./casiogpt_api.txt` o en la ruta indicada por
`CASIOGPT_API_FILE`. Ambas opciones son locales; el archivo normal está ignorado
por Git.

## Sonda cloud física

```bash
./tools/run-esp32-cloud-probe /dev/ttyACM0
```

Esta prueba flashea temporalmente un sketch que verifica Wi-Fi, TLS,
autenticación, modelo y NDJSON desde la ESP32 real. En un bloque `finally`
restaura siempre el firmware normal. Nunca imprime la API key.

# Protocolo UART v5

CasioWIFI y CasioGPT comparten el mismo transporte. La fx-CG50 inicia todas las
operaciones y la XIAO ESP32-C3 responde sin bloquear la interfaz de la
calculadora.

## Capa física

- 9600 baudios, 8 bits, sin paridad, 1 bit de parada (`9600 8N1`).
- XIAO D7 = RX; XIAO D6 = TX.
- Máximo 8 bytes por escritura, con al menos 12 ms entre fragmentos.
- Buffer de frame: 240 bytes.

## Frame

```text
LF @@@ <payload ASCII> * <FNV-1a de 8 hex> LF
```

El checksum es FNV-1a de 32 bits sobre el payload. Tres caracteres `@` y el LF
inicial permiten resincronizar aunque se pierdan bytes de despertar. El receptor
también acepta el frame inmediatamente después de validar checksum, por lo que
la pérdida del LF final no descarta una respuesta válida.

Cada operación contiene un ID monotónico. Una retransmisión conserva el mismo
ID: el firmware devuelve el estado existente en lugar de iniciar de nuevo un
escaneo, conexión o generación. Las respuestas con un ID antiguo se descartan.

## Wi-Fi

```text
SCAN:<id>
  -> WAIT:<id>
  -> READY:<id>:<cantidad>

GET:<id>:-1
  -> WAIT / READY / ERROR

GET:<id>:<indice>
  -> ITEM:<id>:<indice>:<rssi>:<auth>:<flags>:<ssidHex>

JOIN:<id>:<scanId>:<indice>:<claveHex o ->
  -> LINK:<id>:<fase>:<ssidHex>:<resultado>

JSTATE:<id>
STATE:<id>
CANCEL:<id>
```

`flags` usa bit 0 para la red conectada y bit 1 para una red guardada. `fase` es
0 desconectada, 1 conectando, 2 conectada y 3 fallo. El `scanId` evita conectar
por accidente al índice de una lista distinta si el escaneo ha caducado.

Los SSID y contraseñas se codifican en hexadecimal para que `:`, `*` y bytes no
imprimibles no rompan el frame. WEP y WPA2 Enterprise se rechazan; se admiten
redes abiertas y WPA personales con clave ASCII de 8–63 caracteres o PSK
hexadecimal de 64 caracteres.

## Comprobación de Internet

```text
NET_BEGIN:<id> -> NET_WAIT:<id> | NET_DONE:<id> | NET_ERROR:<id>:<motivo>
NET_GET:<id>   -> NET_WAIT:<id> | NET_DONE:<id> | NET_ERROR:<id>:<motivo>
```

La ESP32 comprueba HTTPS contra Ollama sin bloquear el loop UART.

## CasioGPT

Una consulta se carga por fragmentos para que la API key y el prompt no tengan
que caber en un único frame:

```text
GPT_NEW:<id>
GPT_BEGIN:<id>:<keyLength>:<promptLength>
GPT_KEY:<id>:<offset>:<hex>
GPT_PROMPT:<id>:<offset>:<hex>
GPT_RUN:<id>
GPT_GET:<id>:<outputOffset>
GPT_CANCEL:<id>
```

Respuestas principales:

```text
GPT_ACK:<id>:<etapa>:<offset>
GPT_WAIT:<id>
GPT_CHUNK:<id>:<offset>:<hex>
GPT_DONE:<id>:<truncated>
GPT_CANCELLED:<id>
GPT_ERROR:<id>:<motivo>
```

Cada `GPT_GET` devuelve como máximo 64 caracteres y exige el offset exacto. Si
una petición se repite, el mismo offset produce el mismo fragmento; el texto no
se duplica. F6 envía `GPT_CANCEL`, cierra el socket TLS activo y limpia clave y
prompt en RAM.

El endpoint cloud devuelve NDJSON con transferencia HTTP chunked. El firmware
usa `HTTPClient::writeToStream()` para recibir el cuerpo ya decodificado y un
`Stream` personalizado para interpretar cada línea JSON conforme llega. Leer el
socket crudo mezclaría los tamaños de chunks HTTP con JSON y produciría errores
o respuestas corruptas.

## Recuperación

- La calculadora reintenta una petición conservando ID.
- A mitad de los reintentos reabre su UART local.
- La ESP32 elimina duplicados idénticos de la cola de transmisión.
- Checksum incorrecto, respuesta antigua, índice duplicado u offset fuera de
  orden se descartan sin avanzar el estado visible.
- Al salir, los add-ins cancelan operaciones, limpian datos sensibles, drenan
  brevemente TX y ejecutan `Serial_Close(1)` antes de volver al menú.

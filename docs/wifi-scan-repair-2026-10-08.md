# Reparación del escaneo — 8 de octubre de 2026

El fallo reportado era «Error desconocido» al escanear desde cWIFI/CasioWIFI.
Se encontraron dos problemas diferentes:

1. La reconexión periódica llamaba a `WiFi.begin()` sin gestionar el intento ya
   en curso. El auto-reconnect del driver también estaba habilitado. Un intento
   de asociación puede bloquear el inicio del escaneo; en la placa real el
   firmware anterior devolvió `ERROR:411:SCAN`, con resultado de inicio `-2`.
   Este conflicto está descrito en la
   [documentación de Espressif](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32c3/api-guides/wifi.html).
2. El add-in borraba `last_error` al comenzar la siguiente consulta de estado,
   aunque la pantalla de error siguiese visible. Por eso el mensaje concreto
   acababa sustituido por «Error desconocido».

## Corrección

- Un único planificador cooperativo controla los intentos de conexión.
- El escaneo cancela una asociación pendiente, pero no corta una conexión sana.
- Espera no bloqueante de 200 ms y hasta tres intentos de inicio de escaneo.
- Límite total de 20 s; cancelación y fallos liberan el estado de escaneo.
- Las reconexiones tienen tiempo límite y una pausa; no interrumpen SCAN/JOIN.
- Las consultas de estado no borran ni reemplazan un error visible.
- No se cambian pines, hostname, formato de redes guardadas ni contraseñas.

Se ha aplicado al firmware base y a la variante con CasioCAST. En la placa
está instalada la variante Cast; se escribió y verificó solo la aplicación en
`0x10000`, sin borrar NVS ni cambiar particiones. Se conserva el backup previo.

## Comprobación

- Pruebas nativas: 100 escaneos simulados, credenciales/NVS, transporte UART,
  recuperación de errores, cancelación, timeout y regresiones CasioGPT/Cast.
- La regresión del mensaje de error falló antes del cambio y pasa después.
- Radio real: cinco escaneos consecutivos; cinco redes en cada uno; tiempos
  3,01 / 3,01 / 3,01 / 3,00 / 3,00 s. Dos redes guardadas antes y después.
- CasioWIFI instalado en la fx-CG50; copia idéntica por SHA-256 y desmontaje seguro.
  La versión antigua `CWIFI.g3a` se retiró después de guardar y verificar una copia.
- La prueba de radio usa USB físico y el mismo manejador del firmware que UART.
  No sustituye la prueba del teclado/enlace UART en la calculadora.
- Tras instalar ambas mitades, el escaneo volvió a funcionar desde CasioWIFI en
  la fx-CG50. Esa validación física confirma el flujo Casio -> UART -> ESP32 ->
  radio para esta reparación concreta; no certifica por sí sola aplicaciones
  distintas ni todos los receptores de red posibles.

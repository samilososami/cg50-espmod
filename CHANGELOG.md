# Changelog

## v0.1.2 — 2026-10-08

- Comprobación HTTPS con hasta diez intentos y progreso `NET_WAIT:N:10`.
- Reconexión inmediata de redes guardadas al recibir `STATE` o `NET_BEGIN`.
- Errores diferenciados para pérdida de Wi-Fi y fallo de red/TLS.
- La tarea de verificación deja libre el loop UART y adopta peticiones repetidas
  sin crear trabajos duplicados.
- Pruebas de regresión para nueve fallos seguidos, recuperación en el décimo y
  agotamiento completo.

## v0.1.1 — 2026-10-08

- CasioGPT pasa a su repositorio independiente: `samilososami/CasioGPT`.
- El repositorio base queda centrado en hardware, CasioWIFI y firmware puente.
- Reparado el conflicto entre reconexión automática y escaneo Wi-Fi.
- Añadidos reintentos acotados, cancelación limpia y conservación de errores.
- Instalador limitado a CasioWIFI: ya no modifica otros add-ins ni credenciales.
- Checksums de release directamente verificables desde `dist/`.
- Documentación, web y metadatos actualizados para la nueva separación.

## v0.1.0 — 2026-10-07

Primera publicación completa del mod:

- CasioWIFI, renombrado desde el prototipo `cWIFI`.
- Escaneo y conexión Wi-Fi, redes abiertas/protegidas y hasta ocho credenciales
  persistentes en NVS.
- CasioGPT con verificación de ESP32/Internet, conversación y streaming desde
  Ollama Cloud.
- Firmware compartido para XIAO ESP32-C3 con hostname `casio-cg50`.
- Protocolo UART v5 con checksum, IDs, reintentos y cancelación.
- Cierre y reapertura limpia de ambos add-ins.
- Documentación completa del cableado físico y fotografías del montaje real.
- Pruebas de host para transporte, UI, Wi-Fi/NVS, GPT y detección de secretos.

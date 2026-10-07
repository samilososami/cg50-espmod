# Montaje físico y cableado

Esta guía describe el montaje que aparece en las fotografías del repositorio:
una Casio fx-CG50 comunicada por su puerto serie de 3 pines con una Seeed Studio
XIAO ESP32-C3. La ESP32 se alimenta y flashea mediante su propio USB-C.

> Abrir y soldar la calculadora conlleva riesgo. Retira las pilas, desconecta
> ambos USB y verifica cada contacto con un multímetro antes de soldar.

## Material usado

- Casio fx-CG50.
- Seeed Studio XIAO ESP32-C3.
- Dos resistencias en serie de 1 kΩ para las líneas UART.
- Cable fino aislado, soldador y multímetro.
- Cable USB-C de datos/alimentación para la XIAO.

## Señales

El puerto Casio de 2,5 mm sigue esta asignación:

```text
TIP    = RX de la calculadora
RING   = TX de la calculadora
SLEEVE = GND
```

La UART debe cruzarse entre transmisor y receptor:

| Origen | Protección | Destino | Sentido |
|---|---:|---|---|
| TX de la CG50 (ring) | 1 kΩ en serie | D7 / RX de XIAO | CG50 → ESP32 |
| D6 / TX de XIAO | 1 kΩ en serie | RX de la CG50 (tip) | ESP32 → CG50 |
| GND de la CG50 (sleeve) | conexión directa | GND de XIAO | referencia común |

La línea de firmware que materializa esta orientación es:

```cpp
Casio.begin(9600, SERIAL_8N1, D7, D6); // RX, TX
```

No intercambies D6 y D7: **D7 recibe** lo que transmite la calculadora y **D6
transmite** hacia el receptor de la calculadora.

## Identificar los contactos antes de soldar

1. Retira las cuatro pilas y desconecta todos los USB.
2. En continuidad, identifica primero GND. Debe tener continuidad con el
   negativo de las pilas y con blindajes de masa conocidos.
3. Idealmente introduce un jack TRS de 2,5 mm sin alimentar nada y localiza qué
   soldadura corresponde a tip, ring y sleeve. No deduzcas el pad por su forma.
4. Con la calculadora encendida pero la XIAO todavía desconectada, mide las dos
   señales respecto a GND. Deben presentar niveles lógicos próximos a 3,3 V, no
   una alimentación de 5 V o la tensión total de las pilas.
5. Apaga de nuevo y comprueba que no hay continuidad accidental entre TX, RX,
   GND o alimentación.

## Orden de montaje recomendado

1. Prueba la comunicación con la XIAO fuera de la carcasa y alimentada por USB.
2. Suelda GND común.
3. Suelda TX de la CG50 a D7/RX mediante 1 kΩ.
4. Suelda D6/TX a RX de la CG50 mediante 1 kΩ.
5. Comprueba continuidad extremo a extremo y ausencia de cortocircuitos.
6. Conecta únicamente el USB-C de la XIAO y flashea el firmware.
7. Enciende la CG50, instala CasioWIFI y ejecuta un escaneo.
8. Solo después de una prueba estable, fija la placa y enruta el cable USB sin
   pellizcarlo al cerrar la carcasa.

## Alimentación

En la revisión documentada, la XIAO usa **su propio USB-C**. La CG50 conserva
sus cuatro pilas AAA y ambos dispositivos comparten solamente GND y las dos
señales UART.

No conectes el pack de cuatro AAA directamente a `3V3` ni a `5V` de la XIAO.
Una futura alimentación interna requeriría un regulador apropiado, margen para
los picos de Wi-Fi y validación térmica/eléctrica independiente; no forma parte
de esta versión.

## Fotografías del proceso

1. [Prototipo externo](../docs/images/hardware/01-breadboard-prototype.jpg)
2. [Identificación con la calculadora abierta](../docs/images/hardware/02-probing-open-calculator.jpg)
3. [Medición y soldadura](../docs/images/hardware/03-soldering-and-measurement.jpg)
4. [Primer plano de la XIAO instalada](../docs/images/hardware/04-xiao-installed-closeup.jpg)
5. [Distribución interna final](../docs/images/hardware/05-final-internal-layout.jpg)

Los colores visibles pertenecen a ese prototipo concreto y **no son una
convención eléctrica**. Sigue siempre los nombres de señal y verifica tu placa.

# EVA1 – Prototipo Arduino con conexión inalámbrica: riego por humedad de suelo

**Ramo:** TI3V42 Aplicaciones Móviles para IoT · INACAP
**Evaluación:** EVA1 (20 %)

## 1. Integrante y caso

**Integrante:**
- Winderson Castrillo

**Caso:** agricultura / riego. Un sensor de humedad de suelo (YL-69) mide el sustrato y, cuando la humedad baja del umbral, activa una bomba mediante un relé. Un segundo Arduino (nodo control) recibe las lecturas por Bluetooth y permite, de forma remota, cambiar el umbral y accionar la bomba en modo manual. El nodo sensor confirma cada comando recibido.

## 2. Estructura del repositorio

```
eva1-iot-riego-arduino/
├── README.md
├── firmware/
│   ├── nodo_sensor/nodo_sensor.ino     (Arduino UNO #1: sensor + relé + HC-05 esclavo)
│   └── nodo_control/nodo_control.ino   (Arduino UNO #2: operador + LED + HC-05 maestro)
├── docs/
│   └── diagrama.png                    (diagrama de conexión, en elaboración)
└── mockup/
    ├── mockup_principal.png
    ├── mockup_sin_enlace.png
    ├── mockup_conexion.png
    ├── mockup_eventos.png
    └── xml/
        ├── activity_main.xml
        ├── activity_conexion.xml
        └── activity_eventos.xml
```

## 3. Tabla de componentes

| Componente | Cant. | Voltaje | Interfaz | Alcance / capacidad | Por qué sirve en este caso |
|---|---|---|---|---|---|
| Arduino UNO R3 (ATmega328P) | 2 | 5 V lógica; 7–12 V entrada | UART, I/O digital, ADC 10 bits | 14 pines digitales, 6 analógicos | Un nodo mide y actúa; el otro opera. Entrega `millis()`, ADC y `SoftwareSerial` sin hardware extra. |
| Sensor de humedad YL-69 + módulo LM393 | 1 | 3.3–5 V | Salida analógica (A0) | Contacto directo con el suelo | Mide humedad de forma continua y barata; la salida analógica permite calibrar y usar umbral con histéresis. |
| Módulo relé SRD-05VDC | 1 | Bobina 5 V (~70 mA) | Digital (D8), activo en bajo | Contactos hasta 10 A / 250 V AC | Aísla la bomba del Arduino y permite conmutar una carga que el pin no soporta. |
| Mini bomba DC sumergible | 1 | 3–6 V | Carga conmutada por relé | Riego puntual de maceta o jardinera | Es el actuador del caso: mueve agua cuando el suelo está seco. |
| Módulo Bluetooth HC-05 | 2 | Alimentación 5 V; lógica 3.3 V | UART (9600 baudios), SPP | ~10 m en espacio abierto | Enlace inalámbrico punto a punto con roles maestro/esclavo, emparejamiento por PIN y consumo bajo. |
| Divisor de tensión 1 kΩ / 2 kΩ | 2 | 5 V → 3.3 V | Resistivo | — | El RX del HC-05 tolera 3.3 V; el divisor protege el módulo del TX de 5 V del Arduino. |
| LED + resistencia 220 Ω | 1 | ~2 V, ~10 mA | Digital (D13) | — | Indicador en el nodo control: avisa al operador de enlace perdido. |
| Protoboard y cables | — | — | — | — | Montaje sin soldadura. |

## 4. Diagrama de conexión

<!-- ![Diagrama de conexión](docs/diagrama.png) -->
*Diagrama en elaboración.*

**Pines usados**

| Nodo sensor | Pin |
|---|---|
| YL-69 salida analógica | A0 |
| Relé (IN) | D8 |
| HC-05 TX → Arduino | D10 (RX de SoftwareSerial) |
| HC-05 RX ← Arduino (con divisor 1k/2k) | D11 (TX de SoftwareSerial) |

| Nodo control | Pin |
|---|---|
| HC-05 TX → Arduino | D10 |
| HC-05 RX ← Arduino (con divisor 1k/2k) | D11 |
| LED de enlace | D13 |

## 5. Formato del mensaje serial

Cada trama termina con `*` y un checksum de 2 dígitos hexadecimales. El checksum es el XOR de todos los caracteres entre el marcador inicial (`$` o `#`) y el `*`.

- `$` = trama enviada por el nodo sensor (telemetría o respuesta).
- `#` = comando enviado por el nodo control.

| Trama | Dirección | Significado | Ejemplo |
|---|---|---|---|
| `$D,<hum>,<bomba>,<modo>*CS` | sensor → control | Telemetría: humedad %, bomba 0/1, modo A/M. Humedad `-1` indica falla del sensor. | `$D,45,0,A*18` |
| `#U,<n>*CS` | control → sensor | Cambia el umbral (10–90 %) | `#U,40*7D` |
| `#M,1*CS` / `#M,0*CS` | control → sensor | Modo manual: bomba encendida / apagada | `#M,1*50` |
| `#A*CS` | control → sensor | Vuelve a modo automático | `#A*41` |
| `#P*CS` | control → sensor | Latido (heartbeat) cada 2 s | `#P*50` |
| `$OK,<cmd>*CS` | sensor → control | Confirmación del comando | `$OK,U40*79` |
| `$ER,<motivo>*CS` | sensor → control | Error: `RANGO`, `FORMATO`, `CHECKSUM`, `COMANDO`, `LARGO`, `SIN_ENLACE` | `$ER,RANGO*6E` |

El sensor envía telemetría cada 1 s. El nodo sensor confirma con `$OK` cada comando válido y responde con `$ER` a los inválidos.

## 6. Configuración del HC-05

1. Entrar en modo AT (pin KEY a 5 V al energizar; baudios 38400).
2. Esclavo (nodo sensor): `AT+ROLE=0`, `AT+PSWD=4826`, `AT+ADDR?` (anotar la dirección).
3. Maestro (nodo control): `AT+ROLE=1`, `AT+PSWD=4826`, `AT+CMODE=0`, `AT+BIND=<dirección del esclavo>`.
4. Volver a modo normal (9600 baudios).

## 7. Brechas de seguridad y medidas

| # | Brecha | Medida aplicada | Ubicación en el código |
|---|---|---|---|
| 1 | PIN por defecto del HC-05 (1234) | Cambio de PIN y emparejamiento fijo al esclavo (`AT+PSWD`, `AT+CMODE=0`, `AT+BIND`) | Sección 6; configuración previa a cargar el firmware |
| 2 | Comandos sin validar (valores fuera de rango, comandos desconocidos) | Solo 4 comandos permitidos (U, M, A, P); umbral validado entre 10 y 90 con `strtol()`; rechazo con `ER` | `procesarComando()` y `rechazar()` en `nodo_sensor.ino` |
| 3 | Mensajes sin integridad (ruido o alteración) | Checksum XOR por trama; si no coincide se rechaza con `ER,CHECKSUM` | `checksum()` en ambos `.ino`; `esHexa()` y `procesarComando()` en `nodo_sensor.ino`; `procesarTrama()` en `nodo_control.ino` |
| 4 | Desbordamiento de buffer por mensajes largos | Buffers acotados (`comando[24]` en el sensor, `trama[32]` en el control); si se excede, el mensaje se descarta entero y el sensor responde `ER,LARGO` | `leerBluetooth()` en ambos `.ino` |
| 5 | Modo manual sin supervisión (el operador pierde el enlace con la bomba encendida) | Latido cada 2 s; sin latido por más de 5 s el nodo vuelve a AUTO y apaga la bomba | `loop()` del nodo sensor (`LIMITE_SIN_ENLACE`) |

## 8. Prácticas OT

- **Estado seguro (fail-safe):** en riego, el estado menos dañino es la bomba apagada (evita inundar y que la bomba trabaje en seco). Si el sensor falla (`-1`), la bomba queda apagada en modo automático para no regar a ciegas. Si se pierde el enlace por más de 5 s, el nodo vuelve a automático y apaga la bomba. El nodo control enciende el LED del pin 13 para avisar al operador.
- **Principios de ISA/IEC 62443:**
  - *Privilegio mínimo:* el control solo puede enviar 4 comandos acotados.
  - *Defensa en profundidad:* PIN propio, checksum, validación de rango y límites de buffer.
  - *Zonas y conductos:* el enlace Bluetooth es el único conducto entre el nodo de campo (sensor + actuador) y el nodo de operación.
- **Temporización sin bloqueo:** todo se maneja con `millis()` (lectura, latido, tiempos de enlace); no se usan `delay()` largos.
- **Modo AUTO/MANUAL:** equivalente al selector de un tablero de control. En AUTO decide el umbral con histéresis de 5 puntos; en MANUAL manda el operador.

## 9. Cómo compilar y probar

1. Abrir `firmware/nodo_sensor/nodo_sensor.ino` y `firmware/nodo_control/nodo_control.ino` en Arduino IDE y presionar **Verificar**.
2. Cargar cada sketch en su placa (UNO #1 sensor, UNO #2 control).
3. Abrir el monitor serie del nodo control a 9600 baudios y probar: `u 40`, `on`, `off`, `auto`.
4. Probar la pérdida de enlace: desconectar el nodo control más de 5 s y verificar que la bomba se apaga y el modo vuelve a AUTO.
5. Calibrar `RAW_SECO` y `RAW_HUMEDO` en `nodo_sensor.ino` con el suelo real.

## 10. Mockup de la app (Android Studio)

Maqueta solo de interfaz (sin lógica), con el mismo vocabulario del prototipo. Pantallas:

| Pantalla | Archivo |
|---|---|
| Principal (lectura, umbral, AUTO/MANUAL) | `mockup/mockup_principal.png` |
| Principal en estado sin enlace | `mockup/mockup_sin_enlace.png` |
| Conexión Bluetooth | `mockup/mockup_conexion.png` |
| Eventos de seguridad | `mockup/mockup_eventos.png` |

Las capturas se exportaron del diseño en Claude Design. Los layouts equivalentes para Android Studio están en `mockup/xml/`. La pantalla "sin enlace" es `activity_main.xml` con el banner `bannerSinEnlace` en `visible` y los textos del estado seguro.
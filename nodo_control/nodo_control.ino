// ============================================================
// NODO CONTROL: muestra las lecturas del nodo sensor y envía
// los comandos del operador (HC-05 maestro).
// El operador escribe en el Monitor Serie: u 40 | on | off | auto
// ============================================================
#include <SoftwareSerial.h>

const byte BT_RX = 10;           // va al TX del HC-05
const byte BT_TX = 11;           // va al RX del HC-05, con divisor de voltaje
const byte PIN_LED_ENLACE = 13;  // se enciende si se pierde el enlace
SoftwareSerial bt(BT_RX, BT_TX);

const unsigned long PERIODO_LATIDO   = 2000;  // ms entre latidos
const unsigned long LIMITE_SIN_DATOS = 3000;  // ms sin lecturas del nodo sensor

unsigned long ultimoLatido = 0;
unsigned long ultimoDato   = 0;
bool sinEnlace = false;

// Buffers con largo máximo (evitan desbordamientos)
char trama[32];
byte largoTrama = 0;
bool recibiendo = false;
char orden[24];
byte largoOrden = 0;

byte checksum(const char *texto) {
  byte cs = 0;
  for (const char *p = texto; *p; p++) cs ^= *p;
  return cs;
}

// Arma "#contenido*CS" y la manda al nodo sensor.
void enviarComando(const char *contenido) {
  char salida[32];
  snprintf(salida, sizeof(salida), "#%s*%02X", contenido, checksum(contenido));
  bt.println(salida);
}

// Interpreta "45,1,A" (humedad, bomba, modo) y lo muestra legible.
void mostrarLectura(const char *datos) {
  int humedad, bomba;
  char modo;
  if (sscanf(datos, "%d,%d,%c", &humedad, &bomba, &modo) != 3) {
    Serial.println(F("Lectura con formato raro, descartada"));
    return;
  }
  if (humedad < 0) Serial.print(F("Humedad: SENSOR CON FALLA"));
  else { Serial.print(F("Humedad: ")); Serial.print(humedad); Serial.print(F(" %")); }
  Serial.print(bomba ? F(" | Bomba: ENCENDIDA") : F(" | Bomba: apagada"));
  Serial.println(modo == 'M' ? F(" | Modo: MANUAL") : F(" | Modo: AUTO"));
}

// Llega por ejemplo "D,45,0,A*18", ya sin el '$' inicial.
void procesarTrama(char *linea) {
  char *asterisco = strchr(linea, '*');
  if (asterisco == NULL) { Serial.println(F("Trama sin checksum, descartada")); return; }
  *asterisco = '\0';
  const char *recibido = asterisco + 1;

  // Integridad: si el checksum no calza, se descarta la trama.
  if (strlen(recibido) != 2 || (byte)strtol(recibido, NULL, 16) != checksum(linea)) {
    Serial.println(F("Checksum incorrecto, trama descartada"));
    return;
  }

  ultimoDato = millis();
  if (sinEnlace) {
    sinEnlace = false;
    digitalWrite(PIN_LED_ENLACE, LOW);
    Serial.println(F("Enlace recuperado"));
  }

  if (strncmp(linea, "D,", 2) == 0) {
    mostrarLectura(linea + 2);
  } else if (strncmp(linea, "OK,", 3) == 0) {
    Serial.print(F("El sensor confirmo: "));
    Serial.println(linea + 3);
  } else if (strncmp(linea, "ER,", 3) == 0) {
    Serial.print(F("El sensor rechazo o aviso: "));
    Serial.println(linea + 3);
  }
}

void leerBluetooth() {
  while (bt.available()) {
    char c = bt.read();
    if (c == '$') { largoTrama = 0; recibiendo = true; continue; }  // empieza una trama
    if (!recibiendo || c == '\r') continue;
    if (c == '\n') {
      trama[largoTrama] = '\0';
      recibiendo = false;
      procesarTrama(trama);
    } else if (largoTrama >= sizeof(trama) - 1) {
      recibiendo = false;  // demasiado larga: se descarta
    } else {
      trama[largoTrama++] = c;
    }
  }
}

// Traduce lo que escribe el operador a un comando del protocolo.
void procesarOrden(char *texto) {
  for (char *p = texto; *p; p++) *p = tolower(*p);
  if (strcmp(texto, "on") == 0) enviarComando("M,1");
  else if (strcmp(texto, "off") == 0) enviarComando("M,0");
  else if (strcmp(texto, "auto") == 0) enviarComando("A");
  else if (strncmp(texto, "u ", 2) == 0) {
    int pct = atoi(texto + 2);
    char contenido[12];
    snprintf(contenido, sizeof(contenido), "U,%d", pct);
    enviarComando(contenido);  // el nodo sensor decide si el valor es válido
  } else {
    Serial.println(F("Ordenes: u 40 | on | off | auto"));
  }
}

void leerTeclado() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      orden[largoOrden] = '\0';
      largoOrden = 0;
      if (orden[0] != '\0') procesarOrden(orden);
    } else if (largoOrden < sizeof(orden) - 1) {
      orden[largoOrden++] = c;
    }
  }
}

void setup() {
  pinMode(PIN_LED_ENLACE, OUTPUT);
  Serial.begin(9600);
  bt.begin(9600);
  Serial.println(F("Control listo. Ordenes: u 40 | on | off | auto"));
}

void loop() {
  leerBluetooth();
  leerTeclado();
  unsigned long ahora = millis();

  // Latido cada 2 s: le avisa al nodo sensor que seguimos conectados.
  if (ahora - ultimoLatido >= PERIODO_LATIDO) {
    ultimoLatido = ahora;
    enviarComando("P");
  }

  // Si dejan de llegar lecturas, se enciende el LED del pin 13.
  if (!sinEnlace && ahora - ultimoDato > LIMITE_SIN_DATOS) {
    sinEnlace = true;
    digitalWrite(PIN_LED_ENLACE, HIGH);
    Serial.println(F("SIN ENLACE CON EL NODO SENSOR"));
  }
}
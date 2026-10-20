// ============================================================
// NODO SENSOR: mide la humedad del suelo y controla la bomba.
// Envía cada lectura por Bluetooth (HC-05 esclavo) y obedece
// solo comandos validados (checksum, formato y rango).
// ============================================================
#include <SoftwareSerial.h>

// ---------- Pines ----------
const byte PIN_SENSOR = A0;   // salida analógica (AO) del YL-69
const byte PIN_RELE   = 8;    // relé que conmuta la bomba
const byte BT_RX      = 10;   // va al TX del HC-05
const byte BT_TX      = 11;   // va al RX del HC-05, con divisor de voltaje
SoftwareSerial bt(BT_RX, BT_TX);

// La mayoría de los módulos relé se activan con LOW. Si el tuyo
// activa con HIGH, cambia esto a false.
const bool RELE_ACTIVO_BAJO = true;

// ---------- Calibración y límites ----------
const int RAW_SECO   = 1000;  // lectura con el sensor al aire (seco); ajústala a tu sensor
const int RAW_HUMEDO = 350;   // lectura con el sensor en agua; ajústala a tu sensor
const int UMBRAL_MIN = 10;    // % mínimo aceptado para el umbral
const int UMBRAL_MAX = 90;    // % máximo aceptado para el umbral
const int HISTERESIS = 5;     // evita que la bomba prenda y apague sin parar cerca del umbral

const unsigned long PERIODO_LECTURA   = 1000;  // ms entre lecturas
const unsigned long LIMITE_SIN_ENLACE = 5000;  // ms sin latido del nodo control

// ---------- Estado ----------
int  umbralPct       = 40;     // riega si la humedad baja de este valor
bool modoManual      = false;
bool bombaManual     = false;  // lo que pidió el operador en modo manual
bool bombaEncendida  = false;  // estado real de la bomba
bool enlaceCaido     = false;
unsigned long ultimaLectura  = 0;
unsigned long ultimoContacto = 0;

// Buffer de recepción con largo máximo: evita desbordamientos de memoria
char comando[24];
byte largo = 0;
bool recibiendo = false;

// XOR de todos los caracteres: si un byte cambia en el aire, no calza.
byte checksum(const char *texto) {
  byte cs = 0;
  for (const char *p = texto; *p; p++) cs ^= *p;
  return cs;
}

// Arma "$contenido*CS" y la manda por Bluetooth (y copia al Monitor Serie).
void enviar(const char *contenido) {
  char trama[32];
  snprintf(trama, sizeof(trama), "$%s*%02X", contenido, checksum(contenido));
  bt.println(trama);
  Serial.println(trama);
}

// Rechaza un mensaje: lo deja registrado y avisa al nodo control.
// No ejecuta ningún comando.
void rechazar(const char *motivo, const char *detalle) {
  Serial.print(F("[RECHAZADO] "));
  Serial.print(motivo);
  Serial.print(F(": "));
  Serial.println(detalle);
  char msg[20];
  snprintf(msg, sizeof(msg), "ER,%s", motivo);
  enviar(msg);
}

// Enciende o apaga la bomba respetando el tipo de relé.
void ponerBomba(bool encender) {
  bombaEncendida = encender;
  bool nivel = RELE_ACTIVO_BAJO ? !encender : encender;
  digitalWrite(PIN_RELE, nivel ? HIGH : LOW);
}

// Devuelve la humedad en % (0-100), o -1 si el sensor parece desconectado.
int leerHumedadPct() {
  int raw = analogRead(PIN_SENSOR);
  if (raw < 5) return -1;  // casi 0: sensor desconectado o en corto
  long pct = map(raw, RAW_SECO, RAW_HUMEDO, 0, 100);  // más húmedo = lectura más baja
  return (int)constrain(pct, 0, 100);
}

bool esHexa(const char *texto) {
  return strlen(texto) == 2 && isxdigit(texto[0]) && isxdigit(texto[1]);
}

// Llega por ejemplo "U,40*7D", ya sin el carácter de inicio '#'.
void procesarComando(char *linea) {
  char *asterisco = strchr(linea, '*');
  if (asterisco == NULL) { rechazar("FORMATO", linea); return; }
  *asterisco = '\0';
  const char *recibido = asterisco + 1;

  // Defensa 1: integridad. Si el checksum no calza, no se obedece.
  if (!esHexa(recibido) || (byte)strtol(recibido, NULL, 16) != checksum(linea)) {
    rechazar("CHECKSUM", linea);
    return;
  }

  // Trama válida: el nodo control sigue vivo.
  ultimoContacto = millis();
  enlaceCaido = false;

  if (strcmp(linea, "P") == 0) return;  // latido, no se responde

  // Defensa 2: solo 4 comandos permitidos (permisos mínimos, ISA/IEC 62443).
  if (strcmp(linea, "A") == 0) {
    modoManual = false;
    enviar("OK,A");
  } else if (strcmp(linea, "M,1") == 0) {
    modoManual = true;
    bombaManual = true;
    enviar("OK,M1");
  } else if (strcmp(linea, "M,0") == 0) {
    modoManual = true;
    bombaManual = false;
    enviar("OK,M0");
  } else if (strncmp(linea, "U,", 2) == 0) {
    char *fin;
    long valor = strtol(linea + 2, &fin, 10);
    // Defensa 3: el valor debe ser un número completo y estar en rango físico.
    if (fin == linea + 2 || *fin != '\0' || valor < UMBRAL_MIN || valor > UMBRAL_MAX) {
      rechazar("RANGO", linea);
      return;
    }
    umbralPct = (int)valor;
    char respuesta[12];
    snprintf(respuesta, sizeof(respuesta), "OK,U%d", umbralPct);
    enviar(respuesta);
  } else {
    rechazar("COMANDO", linea);
  }
}

// Lee los bytes que llegan sin esperar: arma el comando de a un carácter.
void leerBluetooth() {
  while (bt.available()) {
    char c = bt.read();
    if (c == '#') { largo = 0; recibiendo = true; continue; }  // empieza un comando
    if (!recibiendo || c == '\r') continue;
    if (c == '\n') {
      comando[largo] = '\0';
      recibiendo = false;
      procesarComando(comando);
    } else if (largo >= sizeof(comando) - 1) {
      recibiendo = false;  // demasiado largo: se descarta entero
      rechazar("LARGO", "mensaje demasiado largo");
    } else {
      comando[largo++] = c;
    }
  }
}

void setup() {
  // Se fija el nivel "apagado" ANTES de configurar el pin como salida,
  // para que la bomba no dé un golpe al encender el Arduino.
  digitalWrite(PIN_RELE, RELE_ACTIVO_BAJO ? HIGH : LOW);
  pinMode(PIN_RELE, OUTPUT);
  ponerBomba(false);
  Serial.begin(9600);
  bt.begin(9600);
}

void loop() {
  leerBluetooth();  // siempre atento a los comandos, nunca bloquea
  unsigned long ahora = millis();

  // ESTADO SEGURO: sin latido por 5 s -> modo automático y bomba apagada.
  if (!enlaceCaido && ahora - ultimoContacto > LIMITE_SIN_ENLACE) {
    enlaceCaido = true;
    modoManual = false;
    ponerBomba(false);
    enviar("ER,SIN_ENLACE");
  }

  // Lectura periódica con millis(), sin delay().
  if (ahora - ultimaLectura >= PERIODO_LECTURA) {
    ultimaLectura = ahora;
    int humedad = leerHumedadPct();
    bool encender;

    if (enlaceCaido) {
      encender = false;                     // sin enlace: estado seguro
    } else if (modoManual) {
      encender = bombaManual;               // manda el operador
    } else if (humedad < 0) {
      encender = false;                     // sensor con falla: no regar a ciegas
    } else if (humedad < umbralPct) {
      encender = true;                      // suelo seco: riega
    } else if (humedad >= umbralPct + HISTERESIS) {
      encender = false;                     // ya se humedeció: corta
    } else {
      encender = bombaEncendida;            // zona intermedia: mantiene el estado
    }
    ponerBomba(encender);

    char lectura[24];
    snprintf(lectura, sizeof(lectura), "D,%d,%d,%c",
             humedad, bombaEncendida ? 1 : 0, modoManual ? 'M' : 'A');
    enviar(lectura);
  }
}
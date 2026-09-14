#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

const char* WIFI_SSID = "iPhoneDvKingDev";
const char* WIFI_PASSWORD = "12345678*";

const char* FIREBASE_HOST =
    "https://fonde-pantalla-septiembre-default-rtdb.firebaseio.com";

const char* FIREBASE_AUTH = "";

const char* HOGAR_ID = "hogar_001";
const char* AMBIENTE_ID = "sala_principal";
const char* LUZ_ID = "luz_principal";

const uint8_t PIN_WW = D1;
const uint8_t PIN_CW = D2;
const uint8_t PIN_TOUCH = D5;
const uint8_t PIN_LDR = D6;
const uint8_t PIN_LED = LED_BUILTIN;

const int PWM_MAX = 1023;

const int INTENSIDAD = 70;
const int TEMPERATURA_COLOR = 4000;

/*
  true:
  LOW  = LDR detecta luz
  HIGH = no detecta luz

  Si tu módulo trabaja al revés,
  cambia true por false.
*/
const bool LDR_ACTIVO_EN_LOW = true;

bool salidaEncendida = false;
bool estadoFirebase = false;

bool ultimoTouch = false;

bool estadoLdr = false;
bool ldrPendiente = false;

bool firebaseConectado = false;

bool esperandoValidacion = false;
bool ignorarEscrituraPropia = false;
bool valorEscritoPropio = false;

bool ledParpadeando = false;

unsigned long tiempoTouch = 0;
unsigned long tiempoFirebase = 0;
unsigned long tiempoLdr = 0;
unsigned long tiempoCambioLdr = 0;
unsigned long tiempoValidacion = 0;
unsigned long tiempoConexion = 0;
unsigned long finParpadeo = 0;

const unsigned long DEBOUNCE_TOUCH = 300;
const unsigned long INTERVALO_FIREBASE = 1000;
const unsigned long INTERVALO_LDR = 100;
const unsigned long ESTABILIDAD_LDR = 500;
const unsigned long ESPERA_VALIDACION = 1200;
const unsigned long INTERVALO_CONEXION = 15000;
const unsigned long PARPADEO_LED = 120;

String rutaEncendida() {
  return String("hogares/") +
         HOGAR_ID +
         "/ambientes/" +
         AMBIENTE_ID +
         "/luces/" +
         LUZ_ID +
         "/encendida";
}

String urlFirebase(const String& ruta) {
  String url =
      String(FIREBASE_HOST) +
      "/" +
      ruta +
      ".json";

  if (strlen(FIREBASE_AUTH) > 0) {
    url += "?auth=";
    url += FIREBASE_AUTH;
  }

  return url;
}

void controlarLed(bool encender) {
  /*
    LED_BUILTIN del ESP8266 trabaja
    normalmente con lógica invertida.
  */
  digitalWrite(
    PIN_LED,
    encender ? LOW : HIGH
  );
}

void actualizarLed() {
  if (ledParpadeando) {
    if (millis() < finParpadeo) {
      return;
    }

    ledParpadeando = false;
  }

  bool conectado =
      WiFi.status() == WL_CONNECTED &&
      firebaseConectado;

  controlarLed(conectado);
}

void actividadLed() {
  if (
    WiFi.status() != WL_CONNECTED ||
    !firebaseConectado
  ) {
    controlarLed(false);
    return;
  }

  ledParpadeando = true;

  /*
    Como normalmente está encendido,
    lo apagamos brevemente.
  */
  controlarLed(false);

  finParpadeo =
      millis() + PARPADEO_LED;
}

void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  firebaseConectado = false;
  controlarLed(false);

  Serial.println();
  Serial.println("Conectando WiFi...");

  WiFi.mode(WIFI_STA);

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  unsigned long inicio =
      millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - inicio < 15000
  ) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi conectado");

    Serial.print("IP: ");
    Serial.println(
      WiFi.localIP()
    );
  } else {
    Serial.println(
      "Sin conexion WiFi"
    );
  }
}

String firebaseGet() {
  if (WiFi.status() != WL_CONNECTED) {
    firebaseConectado = false;
    actualizarLed();
    return "";
  }

  std::unique_ptr<BearSSL::WiFiClientSecure> client(
    new BearSSL::WiFiClientSecure
  );

  client->setInsecure();

  HTTPClient https;

  if (!https.begin(
    *client,
    urlFirebase(rutaEncendida())
  )) {
    firebaseConectado = false;
    actualizarLed();
    return "";
  }

  int codigo =
      https.GET();

  String respuesta = "";

  if (
    codigo >= 200 &&
    codigo < 300
  ) {
    respuesta =
        https.getString();

    firebaseConectado = true;
  } else {
    firebaseConectado = false;

    Serial.print(
      "Firebase GET error: "
    );

    Serial.println(codigo);
  }

  https.end();

  actualizarLed();

  return respuesta;
}

bool firebaseSet(
  bool valor
) {
  if (WiFi.status() != WL_CONNECTED) {
    firebaseConectado = false;
    actualizarLed();
    return false;
  }

  std::unique_ptr<BearSSL::WiFiClientSecure> client(
    new BearSSL::WiFiClientSecure
  );

  client->setInsecure();

  HTTPClient https;

  if (!https.begin(
    *client,
    urlFirebase(rutaEncendida())
  )) {
    firebaseConectado = false;
    actualizarLed();
    return false;
  }

  https.addHeader(
    "Content-Type",
    "application/json"
  );

  int codigo =
      https.PUT(
        valor ? "true" : "false"
      );

  bool ok =
      codigo >= 200 &&
      codigo < 300;

  firebaseConectado = ok;

  if (!ok) {
    Serial.print(
      "Firebase PUT error: "
    );

    Serial.println(codigo);
  }

  https.end();

  actualizarLed();

  if (ok) {
    estadoFirebase =
        valor;

    ignorarEscrituraPropia =
        true;

    valorEscritoPropio =
        valor;

    actividadLed();

    Serial.print(
      "Firebase -> encendida = "
    );

    Serial.println(
      valor
          ? "TRUE"
          : "FALSE"
    );
  }

  return ok;
}

bool leerLdr() {
  int lectura =
      digitalRead(PIN_LDR);

  if (LDR_ACTIVO_EN_LOW) {
    return lectura == LOW;
  }

  return lectura == HIGH;
}

void aplicarIluminacion(
  bool encender
) {
  salidaEncendida =
      encender;

  if (!encender) {
    analogWrite(PIN_WW, 0);
    analogWrite(PIN_CW, 0);

    Serial.println();
    Serial.println(
      "CINTA -> APAGADA"
    );

    return;
  }

  float intensidad =
      INTENSIDAD / 100.0f;

  float frio =
      (
        TEMPERATURA_COLOR -
        2700.0f
      ) /
      3800.0f;

  frio =
      constrain(
        frio,
        0.0f,
        1.0f
      );

  float calido =
      1.0f - frio;

  int pwmWW =
      round(
        PWM_MAX *
        intensidad *
        calido
      );

  int pwmCW =
      round(
        PWM_MAX *
        intensidad *
        frio
      );

  analogWrite(
    PIN_WW,
    pwmWW
  );

  analogWrite(
    PIN_CW,
    pwmCW
  );

  Serial.println();
  Serial.println(
    "CINTA -> ENCENDIDA"
  );

  Serial.print(
    "Intensidad: "
  );

  Serial.print(
    INTENSIDAD
  );

  Serial.println("%");

  Serial.print(
    "Temperatura: "
  );

  Serial.print(
    TEMPERATURA_COLOR
  );

  Serial.println(" K");
}

void iniciarValidacion() {
  esperandoValidacion =
      true;

  tiempoValidacion =
      millis();

  Serial.println(
    "Esperando validacion del LDR..."
  );
}

void validarConLdr() {
  if (!esperandoValidacion) {
    return;
  }

  if (
    millis() - tiempoValidacion <
    ESPERA_VALIDACION
  ) {
    return;
  }

  esperandoValidacion =
      false;

  bool detectaLuz =
      leerLdr();

  Serial.println();
  Serial.println(
    "VALIDACION LDR"
  );

  Serial.print(
    "LDR detecta luz: "
  );

  Serial.println(
    detectaLuz
        ? "SI"
        : "NO"
  );

  /*
    ESTE ES EL PUNTO IMPORTANTE.

    Firebase queda exactamente
    como indique el LDR.
  */
  firebaseSet(
    detectaLuz
  );

  /*
    Si se intentó encender pero
    no hay luz, consideramos que
    hubo una falla física.
  */
  if (
    salidaEncendida &&
    !detectaLuz
  ) {
    Serial.println(
      "ALERTA: se intento encender "
      "pero el LDR NO detecta luz."
    );
  }
}

void ejecutarOrden(
  bool encender
) {
  Serial.println();

  Serial.print(
    "ORDEN FISICA -> "
  );

  Serial.println(
    encender
        ? "ENCENDER"
        : "APAGAR"
  );

  /*
    Primero ejecutamos físicamente.
  */
  aplicarIluminacion(
    encender
  );

  /*
    Después el LDR decide qué
    valor permanece en Firebase.
  */
  iniciarValidacion();

  actividadLed();
}

void controlarTouch() {
  bool touch =
      digitalRead(PIN_TOUCH);

  if (
    touch &&
    !ultimoTouch &&
    millis() - tiempoTouch >
        DEBOUNCE_TOUCH
  ) {
    tiempoTouch =
        millis();

    bool nuevoEstado =
        !salidaEncendida;

    Serial.println();
    Serial.println(
      "HTTM -> TOQUE"
    );

    /*
      El TOUCH NO decide el valor
      definitivo de Firebase.

      Solo ejecuta la orden.

      El LDR validará después.
    */
    ejecutarOrden(
      nuevoEstado
    );
  }

  ultimoTouch =
      touch;
}

void controlarLdr() {
  if (
    millis() - tiempoLdr <
    INTERVALO_LDR
  ) {
    return;
  }

  tiempoLdr =
      millis();

  bool lectura =
      leerLdr();

  if (
    lectura !=
    ldrPendiente
  ) {
    ldrPendiente =
        lectura;

    tiempoCambioLdr =
        millis();

    return;
  }

  if (
    lectura ==
    estadoLdr
  ) {
    return;
  }

  if (
    millis() - tiempoCambioLdr <
    ESTABILIDAD_LDR
  ) {
    return;
  }

  estadoLdr =
      lectura;

  Serial.println();

  Serial.print(
    "LDR -> "
  );

  Serial.println(
    estadoLdr
        ? "HAY LUZ"
        : "NO HAY LUZ"
  );

  actividadLed();

  /*
    Si estamos esperando una validación
    de una orden, dejamos que validarConLdr()
    haga la confirmación.

    Evitamos escribir antes de tiempo.
  */
  if (esperandoValidacion) {
    return;
  }

  /*
    Si la luz física cambia durante
    funcionamiento normal, el LDR
    actualiza directamente encendida.
  */
  firebaseSet(
    estadoLdr
  );
}

void leerFirebase() {
  String respuesta =
      firebaseGet();

  respuesta.trim();

  if (
    respuesta.isEmpty() ||
    respuesta == "null"
  ) {
    return;
  }

  bool nuevoEstado;

  if (respuesta == "true") {
    nuevoEstado = true;
  } else if (respuesta == "false") {
    nuevoEstado = false;
  } else {
    Serial.print(
      "Respuesta Firebase invalida: "
    );

    Serial.println(
      respuesta
    );

    return;
  }

  /*
    Ignoramos la lectura producida
    por una escritura hecha por
    nuestro propio ESP.
  */
  if (
    ignorarEscrituraPropia &&
    nuevoEstado ==
        valorEscritoPropio
  ) {
    ignorarEscrituraPropia =
        false;

    estadoFirebase =
        nuevoEstado;

    return;
  }

  ignorarEscrituraPropia =
      false;

  if (
    nuevoEstado ==
    estadoFirebase
  ) {
    return;
  }

  estadoFirebase =
      nuevoEstado;

  Serial.println();

  Serial.println(
    "FIREBASE / APP -> NUEVA ORDEN"
  );

  Serial.print(
    "encendida = "
  );

  Serial.println(
    nuevoEstado
        ? "TRUE"
        : "FALSE"
  );

  /*
    Firebase representa inicialmente
    una orden de la APP/Darvix.

    Ejecutamos físicamente la orden
    y luego el LDR la valida.
  */
  ejecutarOrden(
    nuevoEstado
  );
}

void iniciarLdr() {
  estadoLdr =
      leerLdr();

  ldrPendiente =
      estadoLdr;

  tiempoCambioLdr =
      millis();

  Serial.print(
    "LDR inicial -> "
  );

  Serial.println(
    estadoLdr
        ? "HAY LUZ"
        : "NO HAY LUZ"
  );
}

void cargarEstadoFirebase() {
  String respuesta =
      firebaseGet();

  respuesta.trim();

  if (respuesta == "true") {
    estadoFirebase = true;

    Serial.println(
      "Firebase inicial = TRUE"
    );

    /*
      Se intenta encender y después
      el LDR valida.
    */
    ejecutarOrden(
      true
    );

    return;
  }

  if (respuesta == "false") {
    estadoFirebase = false;

    Serial.println(
      "Firebase inicial = FALSE"
    );

    ejecutarOrden(
      false
    );

    return;
  }

  Serial.print(
    "Firebase inicial invalido: "
  );

  Serial.println(
    respuesta
  );
}

void comprobarConexion() {
  firebaseGet();
}

void setup() {
  Serial.begin(115200);

  delay(300);

  Serial.println();
  Serial.println(
    "============================"
  );

  Serial.println(
    "DARVIX ESP8266"
  );

  Serial.println(
    "============================"
  );

  pinMode(
    PIN_WW,
    OUTPUT
  );

  pinMode(
    PIN_CW,
    OUTPUT
  );

  pinMode(
    PIN_TOUCH,
    INPUT
  );

  pinMode(
    PIN_LDR,
    INPUT
  );

  pinMode(
    PIN_LED,
    OUTPUT
  );

  controlarLed(false);

  analogWriteRange(
    PWM_MAX
  );

  analogWriteFreq(
    1000
  );

  analogWrite(
    PIN_WW,
    0
  );

  analogWrite(
    PIN_CW,
    0
  );

  conectarWiFi();

  iniciarLdr();

  cargarEstadoFirebase();

  actualizarLed();

  Serial.println();
  Serial.println(
    "DARVIX LISTO"
  );

  Serial.println(
    "D1 / GPIO5  -> WW CALIDO"
  );

  Serial.println(
    "D2 / GPIO4  -> CW FRIO"
  );

  Serial.println(
    "D5 / GPIO14 -> HTTM"
  );

  Serial.println(
    "D6 / GPIO12 -> LDR DIGITAL"
  );

  Serial.println(
    "LED INTERNO -> CONEXION / ACTIVIDAD"
  );

  Serial.println();
}

void loop() {
  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {
    firebaseConectado =
        false;

    actualizarLed();

    conectarWiFi();
  }

  controlarTouch();

  controlarLdr();

  validarConLdr();

  if (
    millis() - tiempoFirebase >=
    INTERVALO_FIREBASE
  ) {
    tiempoFirebase =
        millis();

    leerFirebase();
  }

  if (
    millis() - tiempoConexion >=
    INTERVALO_CONEXION
  ) {
    tiempoConexion =
        millis();

    comprobarConexion();
  }

  actualizarLed();

  delay(5);
}
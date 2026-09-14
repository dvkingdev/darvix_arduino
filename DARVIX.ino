#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>

const char* WIFI_SSID = "DV_KING_CODE";
const char* WIFI_PASSWORD = "@tib585f3x139";

const char* FIREBASE_HOST =
  "https://fonde-pantalla-septiembre-default-rtdb.firebaseio.com";

const char* FIREBASE_AUTH = "";

const char* HOGAR_ID = "hogar_001";
const char* AMBIENTE_ID = "sala_principal";
const char* LUZ_ID = "luz_principal";

const uint8_t PIN_WW = D1;
const uint8_t PIN_CW = D2;
const uint8_t PIN_LED = D4;
const uint8_t PIN_TOUCH = D5;
const uint8_t PIN_LDR = D6;

const int PWM_MAX = 1023;

const bool LDR_ACTIVO_EN_LOW = true;

const unsigned long INTERVALO_FIREBASE = 1000;
const unsigned long INTERVALO_LDR = 100;
const unsigned long ESTABILIDAD_LDR = 400;
const unsigned long ESPERA_VALIDACION = 1200;
const unsigned long INTERVALO_RECONEXION = 10000;
const unsigned long PARPADEO_LED = 500;
const unsigned long TOUCH_MINIMO = 80;

enum OrigenOrden {
  ORIGEN_SISTEMA,
  ORIGEN_APP,
  ORIGEN_TOUCH
};

bool salidaEncendida = false;
bool estadoFirebase = false;
bool estadoLdr = false;
bool ldrPendiente = false;
bool touchAnterior = false;
bool esperandoValidacion = false;
bool ordenSolicitada = false;
bool firebaseConectado = false;
bool ledParpadeando = false;
bool snapshotInicializado = false;
bool ignorarSiguienteSnapshot = false;

int intensidadActual = 70;
int temperaturaActual = 4000;

String ultimoSnapshot;

unsigned long inicioTouch = 0;
unsigned long tiempoFirebase = 0;
unsigned long tiempoLdr = 0;
unsigned long tiempoCambioLdr = 0;
unsigned long tiempoValidacion = 0;
unsigned long tiempoReconexion = 0;
unsigned long finParpadeo = 0;

String rutaLuz() {
  return String("hogares/") + HOGAR_ID +
         "/ambientes/" + AMBIENTE_ID +
         "/luces/" + LUZ_ID;
}

String urlFirebase() {
  String url = String(FIREBASE_HOST) + "/" + rutaLuz() + ".json";

  if (strlen(FIREBASE_AUTH)) {
    url += "?auth=";
    url += FIREBASE_AUTH;
  }

  return url;
}

void controlarLed(bool encender) {
  digitalWrite(PIN_LED, encender ? LOW : HIGH);
}

void actualizarLed() {
  if (ledParpadeando) {
    if ((long)(millis() - finParpadeo) < 0) return;

    ledParpadeando = false;
    Serial.println("LED -> FIJO");
  }

  controlarLed(
    WiFi.status() == WL_CONNECTED &&
    firebaseConectado
  );
}

void actividadLed() {
  if (WiFi.status() != WL_CONNECTED || !firebaseConectado) return;

  Serial.println("LED -> PARPADEO");

  ledParpadeando = true;
  controlarLed(false);

  finParpadeo = millis() + PARPADEO_LED;
}

void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  firebaseConectado = false;
  controlarLed(false);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println();
  Serial.println("============================");
  Serial.println("DARVIX WIFI");
  Serial.println("============================");

  Serial.print("Conectando a ");
  Serial.println(WIFI_SSID);

  unsigned long inicio = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - inicio < 20000
  ) {
    Serial.print(".");
    delay(500);
    yield();
  }

  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi -> NO CONECTADO");
    return;
  }

  Serial.println("WiFi -> CONECTADO");

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

void reconectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  firebaseConectado = false;
  controlarLed(false);

  Serial.println("WiFi -> RECONECTANDO");

  WiFi.disconnect();
  delay(100);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

String firebaseGet() {
  if (WiFi.status() != WL_CONNECTED) {
    firebaseConectado = false;
    return "";
  }

  std::unique_ptr<BearSSL::WiFiClientSecure> client(
    new BearSSL::WiFiClientSecure
  );

  client->setInsecure();

  HTTPClient https;

  if (!https.begin(*client, urlFirebase())) {
    firebaseConectado = false;
    return "";
  }

  https.setTimeout(5000);

  int codigo = https.GET();
  String respuesta;

  if (codigo >= 200 && codigo < 300) {
    respuesta = https.getString();
    firebaseConectado = true;
  } else {
    firebaseConectado = false;

    Serial.print("Firebase GET error: ");
    Serial.println(codigo);
  }

  https.end();
  actualizarLed();

  return respuesta;
}

bool firebasePatch(const String& payload) {
  if (WiFi.status() != WL_CONNECTED) {
    firebaseConectado = false;
    return false;
  }

  std::unique_ptr<BearSSL::WiFiClientSecure> client(
    new BearSSL::WiFiClientSecure
  );

  client->setInsecure();

  HTTPClient https;

  if (!https.begin(*client, urlFirebase())) {
    firebaseConectado = false;
    return false;
  }

  https.setTimeout(5000);
  https.addHeader("Content-Type", "application/json");

  int codigo = https.sendRequest("PATCH", payload);

  bool ok =
    codigo >= 200 &&
    codigo < 300;

  firebaseConectado = ok;

  if (!ok) {
    Serial.print("Firebase PATCH error: ");
    Serial.println(codigo);
  }

  https.end();
  actualizarLed();

  if (ok) actividadLed();

  return ok;
}

bool leerLdr() {
  int lectura = digitalRead(PIN_LDR);

  return LDR_ACTIVO_EN_LOW
    ? lectura == LOW
    : lectura == HIGH;
}

void aplicarIluminacion(bool encender) {
  salidaEncendida = encender;

  if (!encender) {
    analogWrite(PIN_WW, 0);
    analogWrite(PIN_CW, 0);

    Serial.println("LUZ -> APAGADA");
    return;
  }

  float intensidad =
    intensidadActual / 100.0f;

  float frio = constrain(
    (temperaturaActual - 2700.0f) / 3800.0f,
    0.0f,
    1.0f
  );

  analogWrite(
    PIN_WW,
    round(
      PWM_MAX *
      intensidad *
      (1.0f - frio)
    )
  );

  analogWrite(
    PIN_CW,
    round(
      PWM_MAX *
      intensidad *
      frio
    )
  );

  Serial.println("LUZ -> ENCENDIDA");

  Serial.print("Intensidad: ");
  Serial.print(intensidadActual);
  Serial.println("%");

  Serial.print("Temperatura: ");
  Serial.print(temperaturaActual);
  Serial.println(" K");
}

void ejecutarOrden(
  bool encender,
  OrigenOrden origen
) {
  ordenSolicitada = encender;

  Serial.println();

  if (origen == ORIGEN_APP) {
    Serial.print("APP");
  } else if (origen == ORIGEN_TOUCH) {
    Serial.print("TOUCH");
  } else {
    Serial.print("SISTEMA");
  }

  Serial.print(" -> ");
  Serial.println(
    encender
      ? "ENCENDER"
      : "APAGAR"
  );

  aplicarIluminacion(encender);

  esperandoValidacion = true;
  tiempoValidacion = millis();

  Serial.println("LDR -> ESPERANDO VALIDACION");
}

void enviarEstadoLdr(bool hayLuz) {
  String payload =
    String("{\"encendida\":") +
    (hayLuz ? "true" : "false") +
    "}";

  Serial.println();
  Serial.print("LDR -> FIREBASE encendida = ");
  Serial.println(
    hayLuz
      ? "TRUE"
      : "FALSE"
  );

  if (!firebasePatch(payload)) {
    Serial.println("LDR -> ERROR FIREBASE");
    return;
  }

  estadoFirebase = hayLuz;
  ignorarSiguienteSnapshot = true;

  Serial.println("LDR -> FIREBASE OK");
}

void validarConLdr() {
  if (!esperandoValidacion) return;

  if (
    millis() - tiempoValidacion <
    ESPERA_VALIDACION
  ) {
    return;
  }

  esperandoValidacion = false;

  bool hayLuz = leerLdr();

  estadoLdr = hayLuz;
  ldrPendiente = hayLuz;

  Serial.println();
  Serial.println("============================");
  Serial.println("VALIDACION LDR");
  Serial.println("============================");

  Serial.print("Orden solicitada: ");
  Serial.println(
    ordenSolicitada
      ? "ENCENDER"
      : "APAGAR"
  );

  Serial.print("LDR detecta: ");
  Serial.println(
    hayLuz
      ? "HAY LUZ"
      : "NO HAY LUZ"
  );

  Serial.println(
    ordenSolicitada == hayLuz
      ? "RESULTADO -> CORRECTO"
      : "RESULTADO -> FALLO"
  );

  enviarEstadoLdr(hayLuz);
}

void controlarLdr() {
  if (
    millis() - tiempoLdr <
    INTERVALO_LDR
  ) {
    return;
  }

  tiempoLdr = millis();

  bool lectura = leerLdr();

  if (lectura != ldrPendiente) {
    ldrPendiente = lectura;
    tiempoCambioLdr = millis();

    return;
  }

  if (lectura == estadoLdr) return;

  if (
    millis() - tiempoCambioLdr <
    ESTABILIDAD_LDR
  ) {
    return;
  }

  estadoLdr = lectura;

  Serial.println();

  Serial.print("LDR ESTABLE -> ");
  Serial.println(
    estadoLdr
      ? "HAY LUZ"
      : "NO HAY LUZ"
  );

  if (esperandoValidacion) return;

  enviarEstadoLdr(
    estadoLdr
  );
}

void controlarTouch() {
  bool touch =
    digitalRead(PIN_TOUCH) ==
    HIGH;

  if (touch && !touchAnterior) {
    inicioTouch = millis();
  }

  if (!touch && touchAnterior) {
    unsigned long duracion =
      millis() - inicioTouch;

    if (duracion >= TOUCH_MINIMO) {
      Serial.println();
      Serial.println("HTTM -> TOQUE");

      actividadLed();

      ejecutarOrden(
        !salidaEncendida,
        ORIGEN_TOUCH
      );
    }
  }

  touchAnterior = touch;
}

bool leerNodo(
  const String& json,
  bool& encendida,
  int& intensidad,
  int& temperatura
) {
  DynamicJsonDocument doc(4096);

  DeserializationError error =
    deserializeJson(
      doc,
      json
    );

  if (error) {
    Serial.print("Firebase JSON error: ");
    Serial.println(error.c_str());

    return false;
  }

  encendida =
    doc["encendida"] |
    estadoFirebase;

  intensidad =
    constrain(
      doc["intensidad"] |
      intensidadActual,
      0,
      100
    );

  temperatura =
    constrain(
      doc["temperaturaColor"] |
      temperaturaActual,
      2700,
      6500
    );

  return true;
}

void procesarCambioFirebase(
  const String& respuesta
) {
  bool encendida;
  int intensidad;
  int temperatura;

  if (!leerNodo(
        respuesta,
        encendida,
        intensidad,
        temperatura
      )) {
    return;
  }

  bool cambioEstado =
    encendida != estadoFirebase;

  bool cambioIntensidad =
    intensidad != intensidadActual;

  bool cambioTemperatura =
    temperatura != temperaturaActual;

  intensidadActual = intensidad;
  temperaturaActual = temperatura;

  if (cambioIntensidad) {
    Serial.print("APP intensidad -> ");
    Serial.println(intensidadActual);
  }

  if (cambioTemperatura) {
    Serial.print("APP temperatura -> ");
    Serial.println(temperaturaActual);
  }

  if (cambioEstado) {
    estadoFirebase = encendida;

    Serial.print("APP encendida -> ");
    Serial.println(
      encendida
        ? "TRUE"
        : "FALSE"
    );

    ejecutarOrden(
      encendida,
      ORIGEN_APP
    );

    return;
  }

  if (
    salidaEncendida &&
    (cambioIntensidad ||
     cambioTemperatura)
  ) {
    aplicarIluminacion(true);

    ordenSolicitada = true;
    esperandoValidacion = true;
    tiempoValidacion = millis();
  }
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

  if (!snapshotInicializado) {
    ultimoSnapshot = respuesta;
    snapshotInicializado = true;

    return;
  }

  if (
    respuesta ==
    ultimoSnapshot
  ) {
    return;
  }

  ultimoSnapshot = respuesta;

  Serial.println();
  Serial.println(
    "FIREBASE -> CAMBIO DETECTADO"
  );

  bool encendida;
  int intensidad;
  int temperatura;

  if (!leerNodo(
        respuesta,
        encendida,
        intensidad,
        temperatura
      )) {
    return;
  }

  if (ignorarSiguienteSnapshot) {
    ignorarSiguienteSnapshot = false;

    estadoFirebase = encendida;
    intensidadActual = intensidad;
    temperaturaActual = temperatura;

    Serial.println(
      "Origen -> ESP8266 / LDR"
    );

    return;
  }

  Serial.println(
    "Origen -> APP / EXTERNO"
  );

  actividadLed();

  procesarCambioFirebase(
    respuesta
  );
}

void iniciarLdr() {
  estadoLdr = leerLdr();
  ldrPendiente = estadoLdr;
  tiempoCambioLdr = millis();

  Serial.print("LDR inicial -> ");

  Serial.println(
    estadoLdr
      ? "HAY LUZ"
      : "NO HAY LUZ"
  );
}

void cargarFirebase() {
  Serial.println(
    "Firebase -> CONECTANDO"
  );

  String respuesta =
    firebaseGet();

  respuesta.trim();

  if (
    respuesta.isEmpty() ||
    respuesta == "null"
  ) {
    Serial.println(
      "Firebase -> SIN DATOS"
    );

    return;
  }

  bool encendida;
  int intensidad;
  int temperatura;

  if (!leerNodo(
        respuesta,
        encendida,
        intensidad,
        temperatura
      )) {
    return;
  }

  estadoFirebase = encendida;
  intensidadActual = intensidad;
  temperaturaActual = temperatura;

  ultimoSnapshot = respuesta;
  snapshotInicializado = true;

  Serial.println(
    "Firebase -> CONECTADO"
  );

  Serial.print("encendida = ");
  Serial.println(
    estadoFirebase
      ? "TRUE"
      : "FALSE"
  );

  Serial.print("intensidad = ");
  Serial.println(
    intensidadActual
  );

  Serial.print("temperatura = ");
  Serial.println(
    temperaturaActual
  );

  aplicarIluminacion(
    estadoFirebase
  );

  ordenSolicitada =
    estadoFirebase;

  esperandoValidacion =
    true;

  tiempoValidacion =
    millis();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("============================");
  Serial.println("DARVIX ESP8266");
  Serial.println("============================");

  pinMode(PIN_WW, OUTPUT);
  pinMode(PIN_CW, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_TOUCH, INPUT);
  pinMode(PIN_LDR, INPUT);

  controlarLed(false);

  analogWriteRange(PWM_MAX);
  analogWriteFreq(1000);

  analogWrite(PIN_WW, 0);
  analogWrite(PIN_CW, 0);

  conectarWiFi();
  iniciarLdr();
  cargarFirebase();
  actualizarLed();

  Serial.println();
  Serial.println("============================");
  Serial.println("DARVIX LISTO");
  Serial.println("============================");

  Serial.println("D1 -> WW");
  Serial.println("D2 -> CW");
  Serial.println("D4 -> LED INTERNO");
  Serial.println("D5 -> HTTM");
  Serial.println("D6 -> LDR");
}

void loop() {
  controlarTouch();
  controlarLdr();
  validarConLdr();

  if (WiFi.status() != WL_CONNECTED) {
    firebaseConectado = false;
    actualizarLed();

    if (
      millis() - tiempoReconexion >=
      INTERVALO_RECONEXION
    ) {
      tiempoReconexion =
        millis();

      reconectarWiFi();
    }
  }

  if (
    WiFi.status() == WL_CONNECTED &&
    millis() - tiempoFirebase >=
    INTERVALO_FIREBASE
  ) {
    tiempoFirebase = millis();

    leerFirebase();
  }

  actualizarLed();

  delay(5);
}
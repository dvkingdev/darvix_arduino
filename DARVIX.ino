#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>

#define DEBUG_SERIAL true

const char* WIFI_SSID = "iPhoneDvKingDev";
const char* WIFI_PASSWORD = "12345678*";

const char* FIREBASE_HOST =
  "https://fonde-pantalla-septiembre-default-rtdb.firebaseio.com";

const char* FIREBASE_AUTH = "";
const char* HOGAR_ID = "hogar_001";

const int PWM_MAX = 1023;

const unsigned long INTERVALO_FIREBASE = 400;
const unsigned long INTERVALO_LDR = 100;
const unsigned long ESTABILIDAD_LDR = 400;
const unsigned long ESPERA_VALIDACION = 1200;
const unsigned long INTERVALO_RECONEXION = 10000;
const unsigned long TOUCH_MINIMO = 80;

const bool LDR_ACTIVO_EN_LOW = true;

enum OrigenOrden {
  ORIGEN_SISTEMA,
  ORIGEN_APP,
  ORIGEN_TOUCH
};

struct Ambiente {
  const char* id;
  const char* nombre;
  const char* luzId;

  uint8_t pinWW;
  uint8_t pinCW;
  int8_t pinTouch;
  int8_t pinLdr;

  bool ldrAnalogico;
  bool touchDisponible;

  bool salidaEncendida;
  bool estadoFirebase;
  bool touchAnterior;

  bool estadoLdr;
  bool ldrPendiente;

  bool esperandoValidacion;
  bool ordenSolicitada;

  int intensidad;
  int temperatura;

  unsigned long inicioTouch;
  unsigned long tiempoCambioLdr;
  unsigned long tiempoValidacion;

  String ultimoSnapshot;
};

Ambiente ambientes[] = {
  {
    "sala_principal",
    "SALA PRINCIPAL",
    "luz_principal",

    D1,
    D2,
    D0,
    D6,

    false,
    true,

    false,
    false,
    false,

    false,
    false,

    false,
    false,

    70,
    4000,

    0,
    0,
    0,

    ""
  },

  {
    "sala_secundaria",
    "SALA SECUNDARIA",
    "luz_principal",

    D3,
    D4,
    D7,
    3,

    false,
    true,

    false,
    false,
    false,

    false,
    false,

    false,
    false,

    70,
    4000,

    0,
    0,
    0,

    ""
  },

  {
    "cocina",
    "COCINA",
    "luz_principal",

    D5,
    D8,
    1,
    A0,

    true,
    !DEBUG_SERIAL,

    false,
    false,
    false,

    false,
    false,

    false,
    false,

    70,
    4000,

    0,
    0,
    0,

    ""
  }
};

const uint8_t TOTAL_AMBIENTES =
  sizeof(ambientes) / sizeof(ambientes[0]);

bool firebaseConectado = false;

uint8_t ambienteFirebase = 0;

unsigned long tiempoFirebase = 0;
unsigned long tiempoLdr = 0;
unsigned long tiempoReconexion = 0;

void log(const String& texto) {
  if (DEBUG_SERIAL)
    Serial.println(texto);
}

void log(const char* texto) {
  if (DEBUG_SERIAL)
    Serial.println(texto);
}

String boolTexto(bool valor) {
  return valor ? "TRUE" : "FALSE";
}

String origenTexto(OrigenOrden origen) {
  if (origen == ORIGEN_APP)
    return "APP";

  if (origen == ORIGEN_TOUCH)
    return "TOUCH";

  return "SISTEMA";
}

String rutaLuz(const Ambiente& a) {
  return String("hogares/") +
         HOGAR_ID +
         "/ambientes/" +
         a.id +
         "/luces/" +
         a.luzId;
}

String urlFirebase(const String& ruta) {
  String url =
    String(FIREBASE_HOST) +
    "/" +
    ruta +
    ".json";

  if (strlen(FIREBASE_AUTH)) {
    url += "?auth=";
    url += FIREBASE_AUTH;
  }

  return url;
}

void imprimirSeparador() {
  log("----------------------------------------");
}

void imprimirAmbiente(const Ambiente& a) {
  if (!DEBUG_SERIAL)
    return;

  Serial.print("[");
  Serial.print(a.nombre);
  Serial.print("] ");
}

void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED)
    return;

  firebaseConectado = false;

  log("");
  log("========================================");
  log("DARVIX -> CONECTANDO WIFI");
  log("========================================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long inicio = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - inicio < 20000
  ) {
    if (DEBUG_SERIAL)
      Serial.print(".");

    delay(250);
    yield();
  }

  if (DEBUG_SERIAL)
    Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    log("WIFI -> CONECTADO");

    if (DEBUG_SERIAL) {
      Serial.print("SSID -> ");
      Serial.println(WiFi.SSID());

      Serial.print("IP -> ");
      Serial.println(WiFi.localIP());

      Serial.print("RSSI -> ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
    }
  } else {
    log("WIFI -> ERROR DE CONEXION");
  }

  imprimirSeparador();
}

void reconectarWiFi() {
  if (WiFi.status() == WL_CONNECTED)
    return;

  firebaseConectado = false;

  log("");
  log("[WIFI] DESCONectado -> RECONECTANDO");

  WiFi.disconnect();
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

String firebaseGet(const String& ruta) {
  if (WiFi.status() != WL_CONNECTED) {
    firebaseConectado = false;
    return "";
  }

  std::unique_ptr<BearSSL::WiFiClientSecure> client(
    new BearSSL::WiFiClientSecure
  );

  client->setInsecure();

  HTTPClient https;

  if (!https.begin(*client, urlFirebase(ruta))) {
    firebaseConectado = false;
    log("[FIREBASE] ERROR AL INICIAR HTTPS");
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

    if (DEBUG_SERIAL) {
      Serial.print("[FIREBASE] GET ERROR -> ");
      Serial.println(codigo);
    }
  }

  https.end();

  return respuesta;
}

bool firebasePatch(
  const String& ruta,
  const String& payload
) {
  if (WiFi.status() != WL_CONNECTED) {
    firebaseConectado = false;

    log("[FIREBASE] PATCH CANCELADO -> SIN WIFI");

    return false;
  }

  std::unique_ptr<BearSSL::WiFiClientSecure> client(
    new BearSSL::WiFiClientSecure
  );

  client->setInsecure();

  HTTPClient https;

  if (!https.begin(*client, urlFirebase(ruta))) {
    firebaseConectado = false;
    log("[FIREBASE] ERROR AL INICIAR PATCH");

    return false;
  }

  https.setTimeout(5000);

  https.addHeader(
    "Content-Type",
    "application/json"
  );

  int codigo =
    https.sendRequest(
      "PATCH",
      payload
    );

  bool ok =
    codigo >= 200 &&
    codigo < 300;

  firebaseConectado = ok;

  if (DEBUG_SERIAL) {
    Serial.print("[FIREBASE] PATCH -> ");
    Serial.print(ok ? "OK" : "ERROR");
    Serial.print(" | HTTP ");
    Serial.println(codigo);
  }

  https.end();

  return ok;
}

bool leerLdr(const Ambiente& a) {
  if (a.ldrAnalogico) {
    int lectura = analogRead(A0);

    bool alto =
      lectura > 500;

    return LDR_ACTIVO_EN_LOW
      ? !alto
      : alto;
  }

  bool alto =
    digitalRead(
      a.pinLdr
    ) == HIGH;

  return LDR_ACTIVO_EN_LOW
    ? !alto
    : alto;
}

bool leerTouch(const Ambiente& a) {
  if (!a.touchDisponible)
    return false;

  return digitalRead(
    a.pinTouch
  ) == HIGH;
}

void aplicarIluminacion(
  Ambiente& a,
  bool encender
) {
  a.salidaEncendida =
    encender;

  if (!encender) {
    analogWrite(
      a.pinWW,
      0
    );

    analogWrite(
      a.pinCW,
      0
    );

    imprimirAmbiente(a);
    log("SALIDA -> APAGADA");

    return;
  }

  float intensidad =
    constrain(
      a.intensidad,
      0,
      100
    ) /
    100.0f;

  float frio =
    constrain(
      (
        a.temperatura -
        2700.0f
      ) /
      3800.0f,
      0.0f,
      1.0f
    );

  float calido =
    1.0f -
    frio;

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
    a.pinWW,
    pwmWW
  );

  analogWrite(
    a.pinCW,
    pwmCW
  );

  if (DEBUG_SERIAL) {
    imprimirAmbiente(a);
    Serial.println("SALIDA -> ENCENDIDA");

    imprimirAmbiente(a);
    Serial.print("INTENSIDAD -> ");
    Serial.print(a.intensidad);
    Serial.println("%");

    imprimirAmbiente(a);
    Serial.print("TEMPERATURA -> ");
    Serial.print(a.temperatura);
    Serial.println(" K");

    imprimirAmbiente(a);
    Serial.print("PWM WW -> ");
    Serial.println(pwmWW);

    imprimirAmbiente(a);
    Serial.print("PWM CW -> ");
    Serial.println(pwmCW);
  }
}

void ejecutarOrden(
  Ambiente& a,
  bool encender,
  OrigenOrden origen
) {
  a.ordenSolicitada =
    encender;

  log("");
  imprimirSeparador();

  imprimirAmbiente(a);

  if (DEBUG_SERIAL) {
    Serial.print("ORDEN ");
    Serial.print(origenTexto(origen));
    Serial.print(" -> ");
    Serial.println(
      encender
        ? "ENCENDER"
        : "APAGAR"
    );
  }

  aplicarIluminacion(
    a,
    encender
  );

  a.esperandoValidacion =
    true;

  a.tiempoValidacion =
    millis();

  imprimirAmbiente(a);
  log("LDR -> ESPERANDO VALIDACION...");
}

void enviarEstadoLdr(Ambiente& a, bool hayLuz) {

  if (!hayLuz && a.salidaEncendida) {
    imprimirAmbiente(a);
    log("LDR -> NO HAY LUZ | CORTANDO SALIDAS");

    aplicarIluminacion(a, false);

    a.ordenSolicitada = false;

    imprimirAmbiente(a);
    log("SEGURIDAD -> PWM WW = 0 | PWM CW = 0");
  }

  String payload =
    String("{\"encendida\":") +
    (hayLuz ? "true" : "false") +
    "}";

  imprimirAmbiente(a);

  if (DEBUG_SERIAL) {
    Serial.print("FIREBASE <- encendida = ");
    Serial.println(boolTexto(hayLuz));
  }

  if (firebasePatch(rutaLuz(a), payload)) {
    a.estadoFirebase = hayLuz;

    imprimirAmbiente(a);
    log("SINCRONIZACION FIREBASE -> OK");
  } else {
    imprimirAmbiente(a);
    log("SINCRONIZACION FIREBASE -> ERROR");
  }
}

void validarConLdr(
  Ambiente& a
) {
  if (!a.esperandoValidacion)
    return;

  if (
    millis() -
    a.tiempoValidacion <
    ESPERA_VALIDACION
  )
    return;

  a.esperandoValidacion =
    false;

  bool hayLuz =
    leerLdr(a);

  a.estadoLdr =
    hayLuz;

  a.ldrPendiente =
    hayLuz;

  imprimirAmbiente(a);

  if (DEBUG_SERIAL) {
    Serial.print("LDR -> ");
    Serial.println(
      hayLuz
        ? "HAY LUZ"
        : "NO HAY LUZ"
    );

    imprimirAmbiente(a);

    Serial.print("ORDEN SOLICITADA -> ");
    Serial.println(
      a.ordenSolicitada
        ? "ENCENDER"
        : "APAGAR"
    );

    imprimirAmbiente(a);

    Serial.print("VALIDACION -> ");

    Serial.println(
      hayLuz ==
      a.ordenSolicitada
        ? "CORRECTA"
        : "FALLO FISICO"
    );
  }

  enviarEstadoLdr(
    a,
    hayLuz
  );

  imprimirSeparador();
}

void controlarLdr(
  Ambiente& a
) {
  bool lectura =
    leerLdr(a);

  if (
    lectura !=
    a.ldrPendiente
  ) {
    a.ldrPendiente =
      lectura;

    a.tiempoCambioLdr =
      millis();

    return;
  }

  if (
    lectura ==
    a.estadoLdr
  )
    return;

  if (
    millis() -
    a.tiempoCambioLdr <
    ESTABILIDAD_LDR
  )
    return;

  a.estadoLdr =
    lectura;

  imprimirAmbiente(a);

  if (DEBUG_SERIAL) {
    Serial.print("LDR CAMBIO ESTABLE -> ");

    Serial.println(
      lectura
        ? "HAY LUZ"
        : "NO HAY LUZ"
    );
  }

  if (a.esperandoValidacion)
    return;

  enviarEstadoLdr(
    a,
    lectura
  );
}

void controlarTouch(
  Ambiente& a
) {
  if (!a.touchDisponible)
    return;

  bool touch =
    leerTouch(a);

  if (
    touch &&
    !a.touchAnterior
  ) {
    a.inicioTouch =
      millis();

    imprimirAmbiente(a);
    log("TOUCH -> PRESIONADO");
  }

  if (
    !touch &&
    a.touchAnterior
  ) {
    unsigned long duracion =
      millis() -
      a.inicioTouch;

    imprimirAmbiente(a);

    if (DEBUG_SERIAL) {
      Serial.print("TOUCH -> LIBERADO | ");
      Serial.print(duracion);
      Serial.println(" ms");
    }

    if (
      duracion >=
      TOUCH_MINIMO
    ) {
      ejecutarOrden(
        a,
        !a.salidaEncendida,
        ORIGEN_TOUCH
      );
    }
  }

  a.touchAnterior =
    touch;
}

bool leerNodoFirebase(
  const String& json,
  bool& encendida,
  int& intensidad,
  int& temperatura
) {
  DynamicJsonDocument doc(1536);

  DeserializationError error =
    deserializeJson(
      doc,
      json
    );

  if (error) {
    if (DEBUG_SERIAL) {
      Serial.print("[JSON] ERROR -> ");
      Serial.println(
        error.c_str()
      );
    }

    return false;
  }

  encendida =
    doc["encendida"] |
    false;

  intensidad =
    constrain(
      doc["intensidad"] |
      70,
      0,
      100
    );

  temperatura =
    constrain(
      doc["temperaturaColor"] |
      4000,
      2700,
      6500
    );

  return true;
}

void procesarFirebase(
  Ambiente& a,
  const String& respuesta
) {
  bool encendida;

  int intensidad;
  int temperatura;

  if (
    !leerNodoFirebase(
      respuesta,
      encendida,
      intensidad,
      temperatura
    )
  )
    return;

  bool cambioEstado =
    encendida !=
    a.estadoFirebase;

  bool cambioIntensidad =
    intensidad !=
    a.intensidad;

  bool cambioTemperatura =
    temperatura !=
    a.temperatura;

  if (
    !cambioEstado &&
    !cambioIntensidad &&
    !cambioTemperatura
  )
    return;

  log("");
  imprimirAmbiente(a);
  log("FIREBASE -> CAMBIO DETECTADO");

  if (DEBUG_SERIAL) {
    imprimirAmbiente(a);
    Serial.print("encendida -> ");
    Serial.println(
      boolTexto(encendida)
    );

    imprimirAmbiente(a);
    Serial.print("intensidad -> ");
    Serial.print(intensidad);
    Serial.println("%");

    imprimirAmbiente(a);
    Serial.print("temperaturaColor -> ");
    Serial.print(temperatura);
    Serial.println(" K");
  }

  a.intensidad =
    intensidad;

  a.temperatura =
    temperatura;

  if (cambioEstado) {
    a.estadoFirebase =
      encendida;

    ejecutarOrden(
      a,
      encendida,
      ORIGEN_APP
    );

    return;
  }

  if (
    a.salidaEncendida &&
    (
      cambioIntensidad ||
      cambioTemperatura
    )
  ) {
    aplicarIluminacion(
      a,
      true
    );

    a.ordenSolicitada =
      true;

    a.esperandoValidacion =
      true;

    a.tiempoValidacion =
      millis();

    imprimirAmbiente(a);
    log("PWM ACTUALIZADO -> ESPERANDO LDR");
  }
}

void leerFirebaseAmbiente(
  Ambiente& a
) {
  String respuesta =
    firebaseGet(
      rutaLuz(a)
    );

  respuesta.trim();

  if (
    respuesta.isEmpty() ||
    respuesta == "null"
  )
    return;

  if (
    respuesta ==
    a.ultimoSnapshot
  )
    return;

  a.ultimoSnapshot =
    respuesta;

  procesarFirebase(
    a,
    respuesta
  );
}

void cargarEstadoInicial(
  Ambiente& a
) {
  imprimirAmbiente(a);
  log("CARGANDO FIREBASE...");

  String respuesta =
    firebaseGet(
      rutaLuz(a)
    );

  respuesta.trim();

  if (
    respuesta.isEmpty() ||
    respuesta == "null"
  ) {
    imprimirAmbiente(a);
    log("FIREBASE -> SIN DATOS");

    return;
  }

  bool encendida;
  int intensidad;
  int temperatura;

  if (
    !leerNodoFirebase(
      respuesta,
      encendida,
      intensidad,
      temperatura
    )
  )
    return;

  a.estadoFirebase =
    encendida;

  a.intensidad =
    intensidad;

  a.temperatura =
    temperatura;

  a.ultimoSnapshot =
    respuesta;

  if (DEBUG_SERIAL) {
    imprimirAmbiente(a);
    Serial.print("ESTADO INICIAL -> ");
    Serial.println(
      encendida ? "ON" : "OFF"
    );

    imprimirAmbiente(a);
    Serial.print("INTENSIDAD -> ");
    Serial.print(intensidad);
    Serial.println("%");

    imprimirAmbiente(a);
    Serial.print("TEMPERATURA -> ");
    Serial.print(temperatura);
    Serial.println(" K");
  }

  aplicarIluminacion(
    a,
    encendida
  );

  a.ordenSolicitada =
    encendida;

  a.esperandoValidacion =
    true;

  a.tiempoValidacion =
    millis();
}

void iniciarAmbiente(
  Ambiente& a
) {
  pinMode(
    a.pinWW,
    OUTPUT
  );

  pinMode(
    a.pinCW,
    OUTPUT
  );

  if (a.touchDisponible) {
    pinMode(
      a.pinTouch,
      INPUT
    );
  }

  if (!a.ldrAnalogico) {
    pinMode(
      a.pinLdr,
      INPUT
    );
  }

  analogWrite(
    a.pinWW,
    0
  );

  analogWrite(
    a.pinCW,
    0
  );

  a.touchAnterior =
    a.touchDisponible
      ? leerTouch(a)
      : false;

  a.estadoLdr =
    leerLdr(a);

  a.ldrPendiente =
    a.estadoLdr;

  a.tiempoCambioLdr =
    millis();

  imprimirAmbiente(a);
  log("HARDWARE -> INICIADO");
}

void setup() {
  if (DEBUG_SERIAL) {
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println();
    Serial.println("========================================");
    Serial.println("        DARVIX ESP8266");
    Serial.println("     3 AMBIENTES / DEBUG");
    Serial.println("========================================");

    Serial.println();
    Serial.println("DEBUG SERIAL -> ACTIVADO");

    Serial.println(
      "NOTA: TOUCH COCINA DESACTIVADO EN DEBUG"
    );

    Serial.println(
      "GPIO1/TX ESTA RESERVADO PARA SERIAL"
    );

    Serial.println();
  }

  analogWriteRange(
    PWM_MAX
  );

  analogWriteFreq(
    1000
  );

  log("PWM -> 0-1023 / 1000 Hz");
  log("");

  for (
    uint8_t i = 0;
    i < TOTAL_AMBIENTES;
    i++
  ) {
    iniciarAmbiente(
      ambientes[i]
    );
  }

  conectarWiFi();

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {
    log("");
    log("FIREBASE -> CARGANDO AMBIENTES");
    imprimirSeparador();

    for (
      uint8_t i = 0;
      i < TOTAL_AMBIENTES;
      i++
    ) {
      cargarEstadoInicial(
        ambientes[i]
      );

      delay(100);
    }
  }

  log("");
  log("========================================");
  log("DARVIX LISTO");
  log("========================================");
  log("");
}

void loop() {
  for (
    uint8_t i = 0;
    i < TOTAL_AMBIENTES;
    i++
  ) {
    controlarTouch(
      ambientes[i]
    );
  }

  if (
    millis() -
    tiempoLdr >=
    INTERVALO_LDR
  ) {
    tiempoLdr =
      millis();

    for (
      uint8_t i = 0;
      i < TOTAL_AMBIENTES;
      i++
    ) {
      controlarLdr(
        ambientes[i]
      );

      validarConLdr(
        ambientes[i]
      );
    }
  }

  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {
    firebaseConectado =
      false;

    if (
      millis() -
      tiempoReconexion >=
      INTERVALO_RECONEXION
    ) {
      tiempoReconexion =
        millis();

      reconectarWiFi();
    }

  } else if (
    millis() -
    tiempoFirebase >=
    INTERVALO_FIREBASE
  ) {
    tiempoFirebase =
      millis();

    leerFirebaseAmbiente(
      ambientes[
        ambienteFirebase
      ]
    );

    ambienteFirebase++;

    if (
      ambienteFirebase >=
      TOTAL_AMBIENTES
    ) {
      ambienteFirebase = 0;
    }
  }

  delay(5);
}
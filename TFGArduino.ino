#include <WiFi.h>
#include <SPI.h>
#include <MFRC522.h>
#include <FirebaseESP32.h>
#include <queue>
#include <time.h>

// WIFI Y FIREBASE
const char* mi_ssid = "MikroTik-TFG";
const char* mi_pass = "TFGISATFG";

#define URL_FIREBASE "https://trazabilidad-muestras-tfg-default-rtdb.europe-west1.firebasedatabase.app/"
#define CLAVE_FIREBASE "LNwuVydObX8JzrqAyBGz6Tupbx8W8vXXD8JHdMh"

// HORA ESPAÑA
const char* servidor_ntp = "pool.ntp.org";
const long gmt_offset_sec = 3600;
const int daylight_offset_sec = 3600;

// PINES
int pin_rojo = 25;
int pin_verde = 26;
int pin_azul = 27;
int pin_pito = 14;

int ss_lectores[] = {16, 21, 32, 5};

// UID TARJETAS
String uids_permitidos[] = {
  "07E3FEA3",
  "BAAFF734",
  "C40802E5",
  "27FB40A3",
  "C49514E5",
  "FAAFEF34",
  "FA920C35",
  "E79B1125"
};

String info_tarjetas[] = {
  "Muestra 1 - Tipo A",
  "Muestra 2 - Tipo B",
  "Muestra 3 - Tipo C",
  "Muestra 4 - Tipo D",
  "Muestra 5 - Tipo E",
  "Muestra 6 - Tipo F",
  "Muestra 7 - Tipo G",
  "Muestra 8 - Tipo H"
};

// ESTRUCTURA
struct Registro {
  String uid_tarjeta;
  int numero_lector;
  String informacion;
  String contenido;
};

std::queue<Registro> cola_subida;

// FIREBASE
FirebaseData fb_data;
FirebaseConfig fb_config;
FirebaseAuth fb_auth;

// RFID
MFRC522 l1(16, 4);
MFRC522 l2(21, 17);
MFRC522 l3(32, 33);
MFRC522 l4(5, 22);

MFRC522* lectores[] = {
  &l1,
  &l2,
  &l3,
  &l4
};

// CONTROL DUPLICADOS
String ultima_uid[4];
unsigned long ultimo_tiempo[4];

// =========================
// LEER TARJETA (SECTOR 1)
// =========================
String leer_contenido_tarjeta(int i) {

  MFRC522::MIFARE_Key key;

  for (byte j = 0; j < 6; j++) key.keyByte[j] = 0xFF;

  byte bloques[] = {4, 5, 6, 7}; // sector 1

  String texto = "";

  for (byte b = 0; b < 4; b++) {

    byte buffer[18];
    byte size = sizeof(buffer);

    MFRC522::StatusCode status;

    status = lectores[i]->PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_A,
      bloques[b],
      &key,
      &(lectores[i]->uid)
    );

    if (status != MFRC522::STATUS_OK) {
      continue;
    }

    status = lectores[i]->MIFARE_Read(bloques[b], buffer, &size);

    if (status != MFRC522::STATUS_OK) {
      continue;
    }

    for (int j = 0; j < 16; j++) {
      if (buffer[j] != 0 && buffer[j] != 255) {
        texto += (char)buffer[j];
      }
    }

    texto += " ";
  }

  texto.trim();
  return texto;
}

// =========================
// HORA
// =========================
String dame_la_hora() {

  struct tm t;

  if (!getLocalTime(&t)) return "Error_Hora";

  char b[25];

  sprintf(
    b,
    "%02d/%02d/%04d %02d:%02d:%02d",
    t.tm_mday,
    t.tm_mon + 1,
    t.tm_year + 1900,
    t.tm_hour,
    t.tm_min,
    t.tm_sec
  );

  return String(b);
}

// =========================
// SONIDO Y LEDS
// =========================
void sonar(int ms) {
  for (int i = 0; i < ms / 2; i++) {
    digitalWrite(pin_pito, HIGH);
    delay(1);
    digitalWrite(pin_pito, LOW);
    delay(1);
  }
}

void ok() {
  digitalWrite(pin_verde, HIGH);
  sonar(200);
  digitalWrite(pin_verde, LOW);
}

void error_tarjeta() {
  digitalWrite(pin_rojo, HIGH);
  sonar(150);
  delay(100);
  sonar(150);
  digitalWrite(pin_rojo, LOW);
}

// =========================
// SETUP
// =========================
void setup() {

  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(ss_lectores[i], OUTPUT);
    digitalWrite(ss_lectores[i], HIGH);
  }

  pinMode(pin_rojo, OUTPUT);
  pinMode(pin_verde, OUTPUT);
  pinMode(pin_azul, OUTPUT);
  pinMode(pin_pito, OUTPUT);

  digitalWrite(pin_azul, HIGH);

  SPI.begin(18, 19, 23);

  WiFi.begin(mi_ssid, mi_pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  configTime(gmt_offset_sec, daylight_offset_sec, servidor_ntp);

  fb_config.host = URL_FIREBASE;
  fb_config.signer.tokens.legacy_token = CLAVE_FIREBASE;

  Firebase.begin(&fb_config, &fb_auth);
  Firebase.reconnectWiFi(true);

  for (int i = 0; i < 4; i++) {
    digitalWrite(ss_lectores[i], LOW);
    lectores[i]->PCD_Init();
    digitalWrite(ss_lectores[i], HIGH);
  }

  digitalWrite(pin_azul, LOW);
  ok();
}

// =========================
// LOOP
// =========================
void loop() {

  for (int i = 0; i < 4; i++) {

    digitalWrite(ss_lectores[i], LOW);

    if (
      lectores[i]->PICC_IsNewCardPresent() &&
      lectores[i]->PICC_ReadCardSerial()
    ) {

      String uid = "";

      for (byte j = 0; j < lectores[i]->uid.size; j++) {
        if (lectores[i]->uid.uidByte[j] < 0x10) uid += "0";
        uid += String(lectores[i]->uid.uidByte[j], HEX);
      }

      uid.toUpperCase();

      if (
        uid == ultima_uid[i] &&
        millis() - ultimo_tiempo[i] < 2000
      ) {
        lectores[i]->PICC_HaltA();
        lectores[i]->PCD_StopCrypto1();
        digitalWrite(ss_lectores[i], HIGH);
        continue;
      }

      ultima_uid[i] = uid;
      ultimo_tiempo[i] = millis();

      bool valido = false;
      String info = "";

      for (int k = 0; k < 8; k++) {
        if (uid == uids_permitidos[k]) {
          valido = true;
          info = info_tarjetas[k];
          break;
        }
      }

      String contenido = leer_contenido_tarjeta(i);

      Serial.println("CONTENIDO: " + contenido);

      if (valido) {

        ok();

        Registro r;
        r.uid_tarjeta = uid;
        r.numero_lector = i + 1;
        r.informacion = info;
        r.contenido = contenido;

        cola_subida.push(r);

        Serial.println("Tarjeta valida");
      }
      else {
        error_tarjeta();
        Serial.println("Tarjeta NO valida");
      }

      lectores[i]->PICC_HaltA();
      lectores[i]->PCD_StopCrypto1();
    }

    digitalWrite(ss_lectores[i], HIGH);
  }

  // =========================
  // FIREBASE
  // =========================
  if (!cola_subida.empty() && WiFi.status() == WL_CONNECTED) {

    Registro r = cola_subida.front();

    FirebaseJson j;
    j.add("num_lector", r.numero_lector);
    j.add("info", r.informacion);
    j.add("contenido_tarjeta", r.contenido);
    j.add("fecha_hora", dame_la_hora());

    String ruta =
      "/Muestras/" +
      r.uid_tarjeta +
      "/Historial";

    if (Firebase.pushJSON(fb_data, ruta, j)) {

      Serial.println("Subido: " + r.uid_tarjeta);
      cola_subida.pop();
    }
    else {
      Serial.println(fb_data.errorReason());
    }
  }

  delay(10);
}
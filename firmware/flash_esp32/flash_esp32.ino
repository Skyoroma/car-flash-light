/*
  Contrôleur de flash 12 V par Bluetooth (BLE) - ESP32-S3
  - Apparaît sous le nom "Car-Flash-Light" (modifiable avec NOM_BLE)
  - 2 sorties : GAUCHE (GPIO20) et DROITE (GPIO21), à relier aux modules MOSFET
  - La LED RGB de la carte sert de témoin : ROUGE = gauche, BLEU = droite

  Commandes (texte, service UART Nordic) :
    "M:n"  -> mode n (voir liste ci-dessous)
    "V:n"  -> vitesse 1..100
    "L:n"  -> luminosité 0..100
    "P:n"  -> pause 0..100 (x 15 ms, donc 0 à 1,5 s), pour les modes 9 à 11
    "?"    -> renvoie l'état actuel "ETAT m v l p"

  Modes :
    0 Arrêt               6 Gauche seule (strobo)
    1 Fixe                7 Droite seule (strobo)
    2 Stroboscope         8 Fondu alterné
    3 Alterné G/D         9 Strobo x3 + pause
    4 Double flash alt.  10 Double alterné + pause (G G, pause, D D, pause)
    5 Triple flash alt.  11 Triple alterné + pause (G G G, pause, D D D, pause)
                         12 3 flashs, pause, 2 flashs, pause
                         13 2 flashs, pause, 2 flashs, pause
                         14 3 flashs, pause, 1 flash, pause
  (Les modes 9 à 14 utilisent la pause.)
*/

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define NOM_BLE    "Car-Flash-Light"   // nom visible en Bluetooth, change-le si tu veux
#define PIN_GAUCHE 20   // -> entrée TRIG du module MOSFET gauche
#define PIN_DROITE 21   // -> entrée TRIG du module MOSFET droite
#define PIN_RGB    48   // LED RGB témoin (essaie 38 si elle ne réagit pas)
#define NB_MODES   15

#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

volatile int mode = 0;         // modifiés depuis le Bluetooth
volatile int vitesse = 50;     // 1..100
volatile int luminosite = 100; // 0..100
volatile int dureePause = 30;       // 0..100 (x 15 ms)

BLECharacteristic *txChar;
bool connecte = false;

// ---------- Envoi vers le téléphone ----------
void envoyer(const String &msg) {
  Serial.println("Envoi : " + msg);
  if (connecte) {
    txChar->setValue(msg.c_str());
    txChar->notify();
  }
}

void envoyerEtat() {
  envoyer("ETAT " + String(mode) + " " + String(vitesse) + " " + String(luminosite) + " " + String(dureePause));
}

// ---------- Réception des commandes ----------
void traiterCommande(String msg) {
  msg.trim();
  Serial.println("Reçu : " + msg);

  if (msg == "?") {
    envoyerEtat();
  } else if (msg.startsWith("M:")) {
    mode = constrain(msg.substring(2).toInt(), 0, NB_MODES - 1);
    envoyerEtat();
  } else if (msg.startsWith("V:")) {
    vitesse = constrain(msg.substring(2).toInt(), 1, 100);
  } else if (msg.startsWith("L:")) {
    luminosite = constrain(msg.substring(2).toInt(), 0, 100);
  } else if (msg.startsWith("P:")) {
    dureePause = constrain(msg.substring(2).toInt(), 0, 100);
  } else {
    envoyer("Commande inconnue");
  }
}

class ServeurCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override {
    connecte = true;
    Serial.println(">> Téléphone connecté");
  }
  void onDisconnect(BLEServer *s) override {
    connecte = false;
    Serial.println(">> Téléphone déconnecté, nouvelle annonce...");
    BLEDevice::startAdvertising();
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    traiterCommande(String(c->getValue().c_str()));
  }
};

// ---------- Motifs de flash ----------
// Durée d'un cycle en ms selon la vitesse : 1 -> ~1160 ms, 100 -> ~34 ms
unsigned long periode() {
  return (unsigned long)(1200.0 * pow(0.965, vitesse));
}

// n flashs courts dans une fraction 0..1 d'un demi-cycle
float flashs(float frac, int n) {
  float slot = 1.0 / (n + 1);
  int i = (int)(frac / slot);
  if (i >= n) return 0;
  return (fmod(frac, slot) < slot * 0.5) ? 1 : 0;
}

// Une rafale de n flashs (durée P) suivie d'une pause (durée Q)
float rafale(unsigned long t, unsigned long P, unsigned long Q, int n) {
  unsigned long pos = t % (P + Q);
  return pos < P ? flashs(pos / (float)P, n) : 0;
}

// Rafales alternées : gauche, pause, droite, pause
void rafaleAlternee(unsigned long t, unsigned long P, unsigned long Q, int n, float &g, float &d) {
  unsigned long demi = P + Q;
  unsigned long pos = t % (2 * demi);
  float r = rafale(pos % demi, P, Q, n);
  if (pos < demi) { g = r; d = 0; }
  else            { g = 0; d = r; }
}

// Deux groupes de flashs séparés par une pause, ex. (3, 2) = 3 flashs, pause, 2 flashs, pause
float sequence(unsigned long t, unsigned long P, unsigned long Q, int a, int b) {
  unsigned long slot = P / 4;
  if (slot == 0) slot = 1;
  unsigned long total = (a + b) * slot + 2 * Q;
  unsigned long pos = t % total;
  int groupes[2] = { a, b };
  for (int i = 0; i < 2; i++) {
    unsigned long dur = groupes[i] * slot;
    if (pos < dur) return (pos % slot) < slot / 2 ? 1 : 0;
    pos -= dur;
    if (pos < Q) return 0;
    pos -= Q;
  }
  return 0;
}

// Calcule le niveau (0..1) des côtés gauche et droit à l'instant t
void calculer(unsigned long t, float &g, float &d) {
  unsigned long P = periode();
  unsigned long Q = dureePause * 15UL;
  float frac = (t % P) / (float)P;            // position dans un cycle
  unsigned long cyc = t % (2 * P);            // position dans un double cycle
  bool premiereMoitie = cyc < P;
  float fracDemi = (cyc % P) / (float)P;
  float strobo = frac < 0.3 ? 1 : 0;

  g = d = 0;
  switch (mode) {
    case 1: g = d = 1; break;
    case 2: g = d = strobo; break;
    case 3: g = premiereMoitie; d = !premiereMoitie; break;
    case 4: (premiereMoitie ? g : d) = flashs(fracDemi, 2); break;
    case 5: (premiereMoitie ? g : d) = flashs(fracDemi, 3); break;
    case 6: g = strobo; break;
    case 7: d = strobo; break;
    case 8: {
      float phase = cyc / (2.0 * P);
      g = (1 - cos(2 * PI * phase)) / 2;
      d = 1 - g;
      break;
    }
    case 9:  g = d = rafale(t, P, Q, 3); break;
    case 10: rafaleAlternee(t, P, Q, 2, g, d); break;
    case 11: rafaleAlternee(t, P, Q, 3, g, d); break;
    case 12: g = d = sequence(t, P, Q, 3, 2); break;
    case 13: g = d = sequence(t, P, Q, 2, 2); break;
    case 14: g = d = sequence(t, P, Q, 3, 1); break;
  }
}

int pwmG = -1, pwmD = -1;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_GAUCHE, OUTPUT);
  pinMode(PIN_DROITE, OUTPUT);
  analogWrite(PIN_GAUCHE, 0);
  analogWrite(PIN_DROITE, 0);
  rgbLedWrite(PIN_RGB, 0, 0, 0);

  BLEDevice::init(NOM_BLE);
  BLEServer *serveur = BLEDevice::createServer();
  serveur->setCallbacks(new ServeurCallbacks());

  BLEService *service = serveur->createService(SERVICE_UUID);

  txChar = service->createCharacteristic(TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());

  BLECharacteristic *rxChar = service->createCharacteristic(
      RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxChar->setCallbacks(new RxCallbacks());

  service->start();

  BLEAdvertising *annonce = BLEDevice::getAdvertising();
  annonce->addServiceUUID(SERVICE_UUID);
  annonce->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("BLE prêt : cherche \"" NOM_BLE "\"");
}

void loop() {
  float g, d;
  calculer(millis(), g, d);

  int nG = (int)(g * luminosite * 2.55);
  int nD = (int)(d * luminosite * 2.55);

  // On ne met à jour que si quelque chose a changé
  if (nG != pwmG || nD != pwmD) {
    pwmG = nG;
    pwmD = nD;
    analogWrite(PIN_GAUCHE, nG);
    analogWrite(PIN_DROITE, nD);
    rgbLedWrite(PIN_RGB, nG, 0, nD);  // témoin : rouge = gauche, bleu = droite
  }

  delay(2);
}

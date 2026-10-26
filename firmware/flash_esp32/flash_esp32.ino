/*
  Car Flash Light - contrôleur de feux flash 12 V par Bluetooth (BLE) - ESP32-S3

  - Apparaît sous le nom NOM_BLE (par défaut "Car-Flash-Light")
  - 2 sorties : GAUCHE (GPIO20) et DROITE (GPIO21), à relier aux modules MOSFET
  - La LED RGB de la carte sert de témoin : ROUGE = gauche, BLEU = droite
  - Les réglages (mode, vitesse, luminosité, pause) sont mémorisés :
    au prochain démarrage, la carte reprend directement le dernier mode
  - Connexion protégée par un code à 6 chiffres (CODE_PIN), demandé une seule
    fois par le téléphone lors du premier appairage

  Réinitialisation : maintenir le bouton BOOT de la carte enfoncé pendant la
  mise sous tension (environ 3 s) efface les appareils appairés et les réglages.
  La LED témoin clignote en blanc pour confirmer.

  Commandes (texte, service UART Nordic) :
    "M:n"  -> mode n (voir liste ci-dessous)
    "V:n"  -> vitesse 1..100
    "L:n"  -> luminosité 0..100
    "P:n"  -> pause 0..100 (x 15 ms, donc 0 à 1,5 s)
    "?"    -> renvoie l'état actuel "ETAT m v l p"

  Modes :
     0 Arrêt                    9 Strobo x3 + pause
     1 Fixe                    10 Double alterné + pause (G G, pause, D D, pause)
     2 Stroboscope             11 Triple alterné + pause (G G G, pause, D D D, pause)
     3 Alterné G/D             12 3 flashs, pause, 2 flashs, pause
     4 Double flash alterné    13 2 flashs, pause, 2 flashs, pause
     5 Triple flash alterné    14 3 flashs, pause, 1 flash, pause
     6 Gauche seule (strobo)   15 Respiration (les deux s'allument et s'éteignent doucement)
     7 Droite seule (strobo)   16 Gyrophare (4 flashs rapides G, puis 4 flashs rapides D)
     8 Fondu alterné           17 Accélération (strobo de plus en plus rapide, puis pause)
  (Les modes 9 à 14 et 17 utilisent la pause.)
*/

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLESecurity.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <nvs_flash.h>

// ======================= Réglages à personnaliser =======================
#define NOM_BLE        "Car-Flash-Light"  // nom visible en Bluetooth
#define CODE_PIN       123456             // code d'appairage à 6 chiffres : CHANGE-LE !
#define SECURITE       1                  // 1 = code demandé, 0 = connexion libre
#define PIN_GAUCHE     20                 // -> entrée TRIG du module MOSFET gauche
#define PIN_DROITE     21                 // -> entrée TRIG du module MOSFET droite
#define PIN_RGB        48                 // LED RGB témoin (essaie 38 si elle ne réagit pas)
#define PIN_BOOT       0                  // bouton BOOT de la carte
// ========================================================================

#define NB_MODES   18

#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

volatile int mode = 0;          // modifiés depuis le Bluetooth
volatile int vitesse = 50;      // 1..100
volatile int luminosite = 100;  // 0..100
volatile int dureePause = 30;   // 0..100 (x 15 ms)

BLECharacteristic *txChar;
bool connecte = false;

// ---------- Mémorisation des réglages ----------
Preferences prefs;
volatile bool aSauver = false;
volatile unsigned long dernierChangement = 0;

void chargerReglages() {
  prefs.begin("flash", true);
  mode       = constrain(prefs.getUChar("mode", 0), 0, NB_MODES - 1);
  vitesse    = constrain(prefs.getUChar("vit", 50), 1, 100);
  luminosite = constrain(prefs.getUChar("lum", 100), 0, 100);
  dureePause = constrain(prefs.getUChar("pause", 30), 0, 100);
  prefs.end();
}

// On attend 2 s sans changement avant d'écrire, pour ménager la mémoire flash
// quand on fait glisser un curseur
void marquerChangement() {
  aSauver = true;
  dernierChangement = millis();
}

void sauverSiBesoin() {
  if (!aSauver || millis() - dernierChangement < 2000) return;
  aSauver = false;
  prefs.begin("flash", false);
  prefs.putUChar("mode", mode);
  prefs.putUChar("vit", vitesse);
  prefs.putUChar("lum", luminosite);
  prefs.putUChar("pause", dureePause);
  prefs.end();
  Serial.println("Réglages mémorisés");
}

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
    return;
  }
  if (msg.startsWith("M:")) {
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
    return;
  }
  marquerChangement();
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

// Strobo qui accélère : de 6 fois plus lent jusqu'à la vitesse réglée en 4 s, puis pause
#define DUREE_ACCEL 4000.0
float acceleration(unsigned long t, unsigned long P, unsigned long Q) {
  unsigned long pos = t % ((unsigned long)DUREE_ACCEL + Q);
  if (pos >= DUREE_ACCEL) return 0;
  float f1 = 1.0 / P, f0 = f1 / 6.0;                       // fréquences de départ et d'arrivée
  float phase = f0 * pos + (f1 - f0) * pos * pos / (2.0 * DUREE_ACCEL);
  return (phase - floor(phase)) < 0.3 ? 1 : 0;
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
    case 15: {                                    // Respiration : cycle de 4 périodes
      float phase = (t % (4 * P)) / (4.0 * P);
      g = d = (1 - cos(2 * PI * phase)) / 2;
      break;
    }
    case 16: {                                    // Gyrophare : 4 flashs rapides de chaque côté
      unsigned long H = max(60UL, P * 6 / 10);
      unsigned long c = t % (2 * H);
      float f = flashs((c % H) / (float)H, 4);
      if (c < H) g = f; else d = f;
      break;
    }
    case 17: g = d = acceleration(t, P, Q); break;
  }
}

// ---------- Réinitialisation avec le bouton BOOT ----------
void verifierReinitialisation() {
  pinMode(PIN_BOOT, INPUT_PULLUP);
  if (digitalRead(PIN_BOOT) != LOW) return;

  // Le bouton doit rester appuyé 3 s
  unsigned long debut = millis();
  while (digitalRead(PIN_BOOT) == LOW) {
    if (millis() - debut > 3000) {
      Serial.println("Réinitialisation : appairages et réglages effacés");
      nvs_flash_erase();
      nvs_flash_init();
      for (int i = 0; i < 5; i++) {               // confirmation : 5 clignotements blancs
        rgbLedWrite(PIN_RGB, 60, 60, 60); delay(150);
        rgbLedWrite(PIN_RGB, 0, 0, 0);    delay(150);
      }
      return;
    }
    delay(10);
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

  verifierReinitialisation();
  chargerReglages();
  Serial.printf("Réglages repris : mode %d, vitesse %d, luminosité %d, pause %d\n",
                mode, vitesse, luminosite, dureePause);

  BLEDevice::init(NOM_BLE);

#if SECURITE
  // Appairage avec code fixe : le téléphone demande CODE_PIN à la première connexion,
  // puis s'en souvient (appairage mémorisé des deux côtés)
  BLESecurity *securite = new BLESecurity();
  securite->setPassKey(true, CODE_PIN);
  securite->setCapability(ESP_IO_CAP_OUT);           // nécessaire pour que le code soit demandé
  securite->setAuthenticationMode(true, true, true); // mémorisation, protection MITM, connexion sécurisée
#endif

  BLEServer *serveur = BLEDevice::createServer();
  serveur->setCallbacks(new ServeurCallbacks());

  BLEService *service = serveur->createService(SERVICE_UUID);

  txChar = service->createCharacteristic(TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
#if defined(CONFIG_BLUEDROID_ENABLED)
  txChar->addDescriptor(new BLE2902());  // ajouté automatiquement avec NimBLE (ESP32-S3)
#endif

  // Les commandes ne sont acceptées que d'un téléphone appairé avec le bon code
  uint32_t proprietesRx = BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR;
#if SECURITE
  proprietesRx |= BLECharacteristic::PROPERTY_WRITE_ENC | BLECharacteristic::PROPERTY_WRITE_AUTHEN;  // NimBLE
#endif
  BLECharacteristic *rxChar = service->createCharacteristic(RX_UUID, proprietesRx);
#if SECURITE
  rxChar->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);  // Bluedroid (ignoré par NimBLE)
#endif
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

  sauverSiBesoin();
  delay(2);
}

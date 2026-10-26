# Car Flash Light

**Fais flasher les phares de ta voiture depuis ton téléphone, en Bluetooth.**

Un petit boîtier à base d'ESP32-S3 se branche sur le 12 V de la voiture et pilote deux lumières (gauche et droite). Une application web, installable sur le téléphone comme une vraie appli, permet de choisir parmi 14 effets : stroboscope, alterné gauche/droite, doubles et triples flashs, fondu, rafales avec pause…

Le projet a été monté sur une Mazda MX-5, mais il fonctionne sur **n'importe quel véhicule en 12 V** : voiture, utilitaire, quad, moto, remorque, char de carnaval, scène… Il suffit de deux lumières LED 12 V et d'une alimentation 12 V.

<p align="center">
  <img src="docs/photo-montage.jpg" alt="Le boîtier monté : deux modules MOSFET, l'ESP32-S3 protégé par de la mousse et le régulateur de tension avec son afficheur" width="420">
</p>

<p align="center"><em>Le boîtier : les deux modules de commande (en haut), l'ESP32-S3 (au milieu) et le régulateur 12 V → 5 V avec son afficheur (en bas), sur un support imprimé en 3D.</em></p>

---

## Sommaire

- [Fonctionnalités](#fonctionnalités)
- [Matériel](#matériel)
- [Schéma de câblage](#schéma-de-câblage)
- [Installation](#installation)
- [Utilisation](#utilisation)
- [Personnalisation](#personnalisation)
- [Dépannage](#dépannage)
- [À savoir avant de rouler](#à-savoir-avant-de-rouler)

---

## Fonctionnalités

- **Contrôle sans fil** en Bluetooth Low Energy, sans appairage dans les réglages du téléphone.
- **Application installable** sur l'écran d'accueil (Android), qui s'ouvre en plein écran et fonctionne sans Internet une fois installée.
- **14 modes de flash** :

  | Continu | Alternés | Rafales avec pause |
  |---|---|---|
  | Fixe | Alterné gauche / droite | Strobo ×3 + pause |
  | Strobo | Double flash alterné | Double alterné + pause |
  | Gauche seule | Triple flash alterné | Triple alterné + pause |
  | Droite seule | Fondu alterné | 3 + 2 · 2 + 2 · 3 + 1 |

- **Réglages en direct** : vitesse, luminosité, durée des pauses.
- **Aperçu à l'écran** des deux feux, synchronisé avec l'effet choisi.
- **Bouton marche/arrêt** qui relance le dernier mode utilisé.
- **Témoin sur la carte** : la LED RGB de l'ESP32 s'allume en rouge pour la gauche et en bleu pour la droite, pratique pour tester sans rien brancher.

---

## Matériel

| Qté | Composant | Rôle | Lien |
|:---:|---|---|---|
| 1 | Carte **ESP32-S3 DevKitC-1** | Le cerveau : Bluetooth et génération des effets | [Amazon](https://www.amazon.fr/dp/B0FZGJYPHK) |
| 1 | **Régulateur de tension** 12 V → 5 V avec afficheur | Alimente l'ESP32 à partir du 12 V | [Amazon](https://www.amazon.fr/dp/B0H2VRCJLN) |
| 2 | **Module de commande MOSFET** (déclencheur) | Allume et éteint chaque lumière 12 V | [Amazon](https://www.amazon.fr/dp/B0DG8B58PM) |
| 2 | Lumières **LED 12 V** | Phare gauche et phare droit | selon ton véhicule |
| 1 | **Fusible** et porte-fusible | Protège le circuit, calibre selon la consommation des LED | — |
| — | Fils, câbles Dupont, gaine thermo | Câblage | — |
| — | Support imprimé en 3D *(optionnel)* | Fixer et protéger les modules | — |

---

## Schéma de câblage

<p align="center">
  <img src="docs/schema-cablage.svg" alt="Schéma de câblage : la batterie 12 V alimente le régulateur et les deux modules MOSFET, le régulateur fournit 5 V à l'ESP32, les GPIO 20 et 21 commandent les modules, chaque module alimente un phare" width="100%">
</p>

### Les connexions une par une

**1. Alimentation 12 V**

Le + et le − du 12 V arrivent sur le régulateur et sur les deux modules :

| Depuis | Vers |
|---|---|
| + 12 V (après le fusible) | `IN+` du régulateur |
| + 12 V | `VIN+` du module gauche, puis repiqué vers `VIN+` du module droit |
| − 12 V (masse) | `IN−` du régulateur |
| − 12 V | `VIN−` du module gauche, puis repiqué vers `VIN−` du module droit |

**2. Alimentation de l'ESP32 en 5 V**

| Depuis | Vers |
|---|---|
| `OUT+` du régulateur (5 V) | broche `5V` de l'ESP32 |
| `OUT−` du régulateur | broche `GND` de l'ESP32 |

> ⚠️ **Règle la sortie du régulateur sur 5,0 V avant de brancher l'ESP32.** Tourne la petite vis du potentiomètre bleu en regardant l'afficheur (le bouton permet de basculer entre la tension d'entrée et celle de sortie). Plus de 5,5 V peut détruire la carte.

**3. Commande des modules**

| Depuis l'ESP32 | Vers |
|---|---|
| `GPIO 20` | entrée `TRIG` / `PWM` du module **gauche** |
| `GPIO 21` | entrée `TRIG` / `PWM` du module **droit** |
| `GND` | `GND` (côté commande) des **deux** modules |

> La masse commune est indispensable : sans le fil `GND` entre l'ESP32 et les modules, les lumières ne s'allument pas.

**4. Lumières**

| Depuis | Vers |
|---|---|
| `OUT+` / `OUT−` du module gauche | phare gauche |
| `OUT+` / `OUT−` du module droit | phare droit |

---

## Installation

### 1. Programmer l'ESP32

1. Installe l'**[Arduino IDE](https://www.arduino.cc/en/software)**.
2. Dans le gestionnaire de cartes, installe **esp32 by Espressif Systems**.
3. Ouvre `firmware/flash_esp32/flash_esp32.ino`. Le fichier doit rester dans un dossier qui porte **exactement le même nom** que lui (`flash_esp32`).
4. Dans le menu **Outils**, applique ces réglages :

   | Réglage | Valeur |
   |---|---|
   | Type de carte | **ESP32S3 Dev Module** (pas la version *Octal*) |
   | Flash Mode | QIO 80MHz |
   | Flash Size | 16MB (128Mb) |
   | PSRAM | Disabled |
   | USB CDC On Boot | Disabled |
   | Port | le port COM de la carte |

5. Branche la carte avec le port USB marqué **UART** (ou **COM**) et clique sur **Téléverser**. Si l'IDE reste bloqué sur « Connecting… », maintiens le bouton **BOOT** de la carte enfoncé jusqu'au début de l'écriture.
6. Ouvre le moniteur série à **115200 bauds** : le message `BLE prêt` doit s'afficher.

### 2. Installer l'application sur le téléphone

L'application est hébergée gratuitement avec GitHub Pages :

**👉 https://TON-PSEUDO.github.io/car-flash-light/**

- **Android (Chrome)** : ouvre le lien, appuie sur **⋮**, puis **Installer l'application**. Une icône apparaît sur l'écran d'accueil.
- **iPhone** : Safari ne gère pas le Bluetooth web. Ouvre le lien dans l'application gratuite **Bluefy**.
- **PC (Chrome ou Edge)** : la page fonctionne aussi directement dans le navigateur.

Pour héberger ta propre copie, fais un *fork* de ce dépôt, puis active **Settings → Pages → Branch : main**.

---

## Utilisation

1. Mets le contact (ou alimente le boîtier en 12 V).
2. Ouvre l'application et appuie sur **Connecter**, puis choisis **Car-Flash-Light** dans la liste.
3. Choisis un mode, règle la vitesse, la luminosité et la pause.
4. Utilise le **bouton central** pour tout éteindre ou relancer le dernier mode.

L'icône de réglages, en haut à droite, affiche l'état de la connexion et le journal des messages échangés avec la carte.

---

## Personnalisation

Les réglages se trouvent en haut de `flash_esp32.ino` :

```cpp
#define NOM_BLE    "Car-Flash-Light"  // nom visible en Bluetooth
#define PIN_GAUCHE 20                 // sortie vers le module gauche
#define PIN_DROITE 21                 // sortie vers le module droit
#define PIN_RGB    48                 // LED témoin de la carte (38 sur certaines versions)
```

Tu peux renommer la carte comme tu veux (par exemple `MX5-Flash-Light`) : l'application la retrouve quel que soit son nom.

### Protocole Bluetooth

La carte utilise le service **Nordic UART** (`6E400001-B5A3-F393-E0A9-E50E24DCCA9E`). Elle peut donc aussi se piloter avec n'importe quel terminal BLE (nRF Connect, Serial Bluetooth Terminal…) en envoyant du texte :

| Commande | Effet |
|---|---|
| `M:n` | choisit le mode `n` (0 = arrêt, 1 à 14) |
| `V:n` | vitesse, de 1 à 100 |
| `L:n` | luminosité, de 0 à 100 |
| `P:n` | pause, de 0 à 100 (× 15 ms, soit 0 à 1,5 s) |
| `?` | renvoie l'état : `ETAT mode vitesse luminosité pause` |

---

## Dépannage

| Problème | Solution |
|---|---|
| Rien ne s'affiche dans le moniteur série après le téléversement | Vérifie que la carte choisie est bien **ESP32S3 Dev Module** et non la version *Octal*. |
| Erreur `sdkconfig.h: No such file or directory` | Même cause : mauvaise variante de carte sélectionnée. |
| L'application ne trouve pas la carte | Déconnecte toute autre appli (nRF Connect…) : la carte n'accepte qu'une connexion à la fois. Sur Android, active aussi la localisation. |
| La LED témoin ne s'allume pas | Remplace `48` par `38` dans `PIN_RGB`. Sur certaines cartes, il faut aussi relier la petite soudure « RGB » près de la LED. |
| Les phares ne s'allument pas | Vérifie le fil `GND` commun entre l'ESP32 et les modules. |
| Un phare clignote brièvement à la mise sous tension | Ça peut arriver avec le GPIO 20, qui sert aussi à l'USB natif de la carte au démarrage. Si ça te gêne, utilise d'autres broches (par exemple 4 et 5). |
| L'ancienne version de l'appli reste affichée | Ferme complètement l'appli et rouvre-la une ou deux fois. |

---

## À savoir avant de rouler

En France, comme dans la plupart des pays, **les feux à éclats et les stroboscopes sont réservés aux véhicules prioritaires et d'intervention** sur la voie publique. Ce montage est destiné à un usage **hors circulation** : terrain privé, salon auto, rassemblement, tournage, spectacle…

Le circuit tire son énergie de la batterie : ajoute un fusible adapté et coupe l'alimentation quand le véhicule reste longtemps à l'arrêt.

---

## Structure du dépôt

```
├── index.html               Application web (télécommande)
├── manifest.webmanifest     Infos d'installation de l'appli
├── sw.js                    Fonctionnement hors ligne
├── icon-*.png               Icônes de l'appli
├── firmware/
│   └── flash_esp32/
│       └── flash_esp32.ino  Programme de l'ESP32-S3
└── docs/
    ├── photo-montage.jpg
    └── schema-cablage.svg
```

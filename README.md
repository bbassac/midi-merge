# Contrôleur MIDI « merge » sur Arduino Uno

<p align="center">
  <img src="docs/images/prise-din.png" width="300" alt="Prise DIN 5 broches">
  <img src="docs/images/midi-shield.jpg" width="300" alt="midi Shield">
</p>
<p align="center">
  <img src="docs/images/connector_view.jpg" width="300" alt="Prise DIN 5 broches">
  <img src="docs/images/component_view.jpg" width="300" alt="midi Shield">
</p>

Documentation complète du projet (firmware : `midi-merge.ino` + `chords.h`).

> **État de validation** : le firmware de base (recopie du clavier, potentiomètres, joystick, encodeur, boutons en notes) a été validé sur le matériel. Les **modes d'accords** (clic de l'encodeur, §4.1) ont été testés en simulation sur PC mais **pas encore sur le matériel**. Les sections *Tests* et *Dépannage* servent à valider le montage pas à pas.

---

## Contenu du dépôt

```
midi-merge/
├── README.md                    ← ce document
├── midi-merge.ino               ← sketch Arduino
├── chords.h                     ← modes des 8 boutons (notes, accords) : à éditer pour en ajouter
├── moniteur_midi.py             ← moniteur MIDI pour PC (Python, interface Tkinter)
├── Midi_Monitor.png             ← capture du moniteur MIDI
└── docs/
    ├── guide.html               ← version illustrée (à ouvrir dans un navigateur)
    └── images/                  ← schémas et photos
```

Le dossier doit s'appeler `midi-merge` pour que l'IDE Arduino ouvre le sketch avec `chords.h` dans un onglet. La version illustrée `docs/guide.html` est un fichier unique, sans dépendance : elle peut aussi être publiée avec GitHub Pages.

---

## Sommaire

1. [Objectif du projet](#1-objectif-du-projet)
2. [Matériel](#2-matériel)
3. [Schéma matériel](#3-schéma-matériel)
4. [Messages MIDI générés](#4-messages-midi-générés) (dont les [modes de boutons et accords](#41-modes-des-boutons--notes-et-accords))
5. [Fonctionnement du logiciel](#5-fonctionnement-du-logiciel)
6. [Installation et mise en route](#6-installation-et-mise-en-route)
7. [Réglages modifiables](#7-réglages-modifiables)
8. [Tests et validation](#8-tests-et-validation)
9. [Dépannage](#9-dépannage)
10. [Boîtier et prises MIDI déportées](#10-boîtier-et-prises-midi-déportées)
11. [Limites connues et évolutions possibles](#11-limites-connues-et-évolutions-possibles)
12. [Annexe : fichiers source](#12-annexe--fichiers-source)

---

## 1. Objectif du projet

Construire un boîtier MIDI qui se place **entre un clavier MIDI et un synthétiseur** (ou une interface son) :

- le boîtier reçoit sur **MIDI IN** tout ce que joue le clavier (notes, vélocité, pédales, molettes…) ;
- il **recopie intégralement** ces messages vers **MIDI OUT** ;
- il y **ajoute**, sur le **même canal MIDI**, les contrôles de ses propres composants (potentiomètres, boutons, encodeur, joystick) ;
- le tout sort **mélangé sur une seule sortie MIDI** ;
- les **8 boutons jouent des notes ou des accords prêts à l'emploi** ; un clic sur l'encodeur change de mode (notes seules, accords enrichis, accords simples).

Le boîtier peut aussi fonctionner **en contrôleur autonome**, sans rien sur MIDI IN : seuls les messages des composants sortent, sur le canal 1.

![Principe du montage](docs/images/principe.svg)

---

## 2. Matériel

| Élément | Quantité | Rôle |
|---|---|---|
| Arduino Uno | 1 | Cerveau du montage |
| Shield MIDI (IN / OUT / THRU) | 1 | Interface MIDI (optocoupleur en entrée, sortie, switch RUN/PROG) |
| Potentiomètres | 4 | Contrôle continu (CC) |
| Boutons on/off | 8 | Notes ou accords, selon le mode (§4.1) |
| Encodeur numérique avec clic | 1 | Valeur 0-127 (CC 30) + changement de mode des boutons |
| Mini joystick 2 axes avec clic | 1 | Pitch bend, modulation, bouton |
| Prises DIN 5 broches femelles pour panneau | 2 | IN et OUT déportés sur le boîtier (voir §10) |
| Fils, boîtier, alimentation | — | USB ou 7-12 V sur la prise jack |

> **Prise THRU du shield** : c'est une copie matérielle de l'entrée, elle ne contient **pas** les contrôles ajoutés. Elle n'est pas utilisée dans ce projet. Le synthé se branche sur **OUT**.

---

## 3. Schéma matériel

### 3.1 Architecture

```mermaid
flowchart LR
    KB[Clavier MIDI] -->|câble MIDI| IN[Shield : MIDI IN\noptocoupleur]
    IN -->|D0 / RX| UNO[Arduino Uno]
    UNO -->|D1 / TX| OUT[Shield : MIDI OUT]
    OUT -->|câble MIDI| SYN[Synthé / interface]

    P[4 potentiomètres] -->|A0 à A3| UNO
    J[Joystick] -->|A4, A5, D13| UNO
    E[Encodeur] -->|D2, D3, D4| UNO
    B[8 boutons] -->|D5 à D12| UNO
```

### 3.2 Tableau des broches

Les 20 broches de l'Uno sont toutes utilisées.

![Brochage de l'Arduino Uno](docs/images/brochage.svg)

| Broche | Composant | Fonction |
|---|---|---|
| **D0** | Shield MIDI | RX : réception MIDI IN (port série matériel) |
| **D1** | Shield MIDI | TX : émission vers MIDI OUT |
| **D2** | Encodeur | Voie A (CLK) |
| **D3** | Encodeur | Voie B (DT) |
| **D4** | Encodeur | Clic (SW) |
| **D5 à D12** | Boutons 1 à 8 | Un côté sur la broche, l'autre sur GND |
| **D13** | Joystick | Clic (SW) |
| **A0 à A3** | Potentiomètres 1 à 4 | Curseur (broche centrale) |
| **A4** | Joystick | Axe X (VRx) |
| **A5** | Joystick | Axe Y (VRy) |

Brochage du midi shield : 
![Borchage du midi shield](docs/images/midi-shield%20Pinout.png)

### 3.3 Câblage composant par composant

**Potentiomètres (×4)**
```
 5V ──┐
      ├── patte 1
 A0 ──── patte centrale (curseur)     (A1, A2, A3 pour les suivants)
      ├── patte 3
 GND ─┘
```
Inverser les pattes 1 et 3 inverse le sens de rotation.

**Joystick**
| Joystick | Arduino |
|---|---|
| +5V | 5V |
| GND | GND |
| VRx | A4 |
| VRy | A5 |
| SW | D13 |

**Encodeur**
| Encodeur | Arduino |
|---|---|
| CLK (A) | D2 |
| DT (B) | D3 |
| SW | D4 |
| GND (ou « - ») | GND |
| + | non utilisé (ou 5V si le module en a besoin) |

Si le sens de rotation est inversé, échangez les fils CLK et DT.

**Boutons (×8)**
```
 D5..D12 ──── bouton ──── GND
```
Aucune résistance externe : les broches utilisent le pull-up interne (`INPUT_PULLUP`). Un bouton appuyé lit donc `LOW`.

**Alimentation commune** : tous les GND (potentiomètres, joystick, encodeur, boutons) doivent être reliés au GND de l'Arduino.

### 3.4 Shield MIDI

- Le shield s'empile sur l'Uno ; ses connecteurs repassent les broches vers le haut pour câbler les composants.
- Il utilise le **port série matériel D0/D1**. Ces broches ne doivent donc rien porter d'autre.
- **Switch du shield** :
  - position *PROG* : l'entrée MIDI est déconnectée de RX, nécessaire pour **téléverser** ;
  - position *RUN* : fonctionnement normal.
  Le libellé exact dépend de votre modèle.
- **Conflits possibles** : certains shields MIDI (modèle SparkFun, par exemple) ont des boutons ou LED sur D2, D3, D6, D7 ou sur A0 à A2. Ce n'est pas le cas visible sur la photo de votre shield, mais à vérifier au multimètre si un composant se comporte mal.

---

## 4. Messages MIDI générés

Tous les messages ajoutés sortent sur le **canal du clavier** : le code mémorise le canal de la dernière note reçue. Tant qu'aucune note n'est arrivée (ou en mode autonome), le **canal 1** est utilisé.

| Composant | Broche | Message | Valeurs |
|---|---|---|---|
| Potentiomètre 1 | A0 | CC 20 | 0 à 127 |
| Potentiomètre 2 | A1 | CC 21 | 0 à 127 |
| Potentiomètre 3 | A2 | CC 22 | 0 à 127 |
| Potentiomètre 4 | A3 | CC 23 | 0 à 127 |
| Joystick, axe X | A4 | **Pitch bend** | -8192 (gauche) à +8191 (droite), 0 au centre |
| Joystick, axe Y | A5 | **CC 1** (modulation) | 0 au centre à 127 vers le haut ; rien vers le bas |
| Joystick, clic | D13 | CC 110 | 127 à l'appui, 0 au relâchement |
| Encodeur, rotation | D2/D3 | CC 30 | 0 à 127, pas de 2 par cran |
| Encodeur, clic | D4 | aucun message | passe au mode suivant des boutons (§4.1) |
| Boutons 1 à 8 | D5 à D12 | **Note On / Note Off** | note ou accord du mode courant, vélocité 100 (§4.1) |

![Joystick : pitch bend et modulation](docs/images/joystick.svg)

**Pitch bend et demi-tons** : la pleine course du joystick envoie ±8192, soit **±2 demi-tons** si le synthé est réglé sur la plage standard de ±2 (c'est le réglage par défaut de la plupart). Si votre synthé a une autre plage de bend, réglez-la dans le synthé.

### 4.1 Modes des boutons : notes et accords

Les 8 boutons ne jouent pas toujours la même chose : **un clic sur l'encodeur passe au mode suivant, en boucle**. Au démarrage, le mode est le **mode 0**. La rotation de l'encodeur reste le CC 30, quel que soit le mode.

| Mode | Contenu | Clic suivant |
|---|---|---|
| **0** | Notes seules (gamme de do majeur) | → mode 1 |
| **1** | Accords enrichis (9e, 7e, 13e, 11e augmentée…), avec basse | → mode 2 |
| **2** | Accords simples (triades et septième de dominante), avec basse | → mode 0 |

- Tous les accords sont en **do majeur**, vélocité fixe de **100** (`BTN_VELOCITY`), sur le canal du clavier.
- Un bouton joue ses notes **à l'appui** (Note On) et les coupe **au relâchement** (Note Off).
- Le mode et le canal sont mémorisés à l'appui : changer de mode ou de canal pendant qu'un bouton est tenu ne laisse **aucune note bloquée**.
- Plusieurs accords partagent des notes (le do 60 est dans C, F et Am, par exemple). Un compteur par note évite de couper un accord quand on en relâche un autre : une note n'est coupée que lorsque le **dernier bouton qui la tient** est relâché.
- Il n'y a pas d'indicateur du mode courant (aucune broche libre pour une LED) : on l'entend. Numérotation des notes : 60 = do central (C4).

#### Mode 0 : notes seules

| Bouton | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| Note | C4 | D4 | E4 | F4 | G4 | A4 | B4 | C5 |
| MIDI | 60 | 62 | 64 | 65 | 67 | 69 | 71 | 72 |

#### Mode 1 : accords enrichis

| Bouton | Accord | Rôle | Basse | Notes aiguës | Numéros MIDI (basse en premier) |
|---|---|---|---|---|---|
| 1 | **Cmaj9** | I (repos) | C3 | E4 G4 B4 D5 | 48 · 64 · 67 · 71 · 74 |
| 2 | **Dm9** | ii | D3 | F4 A4 C5 E5 | 50 · 65 · 69 · 72 · 76 |
| 3 | **Em7** | iii | E3 | G4 B4 D5 E5 | 52 · 67 · 71 · 74 · 76 |
| 4 | **Fmaj7#11** | IV | F3 | E4 A4 B4 E5 | 53 · 64 · 69 · 71 · 76 |
| 5 | **G13** | V (dominante) | G3 | F4 B4 D5 E5 | 55 · 65 · 71 · 74 · 76 |
| 6 | **Am9** | vi | A3 | C4 E4 G4 B4 | 57 · 60 · 64 · 67 · 71 |
| 7 | **Fm6** | iv mineur emprunté | F3 | C4 Ab4 C5 D5 | 53 · 60 · 68 · 72 · 74 |
| 8 | **G7sus4** | V suspendu | G3 | D4 F4 G4 C5 | 55 · 62 · 65 · 67 · 72 |

Progressions conseillées (numéros de boutons) :

| Ambiance | Boutons | Accords |
|---|---|---|
| Le grand classique, version soyeuse | 1 → 6 → 2 → 5 | Cmaj9 → Am9 → Dm9 → G13 |
| Turnaround plus doux, retarde le retour au do | 3 → 6 → 2 → 5 | Em7 → Am9 → Dm9 → G13 |
| Cadence jazz, finit sur un accord ouvert | 2 → 5 → 1 → 4 | Dm9 → G13 → Cmaj9 → Fmaj7#11 |
| Moment doux-amer (le Fm6) | 1 → 4 → 7 → 1 | Cmaj9 → Fmaj7#11 → Fm6 → Cmaj9 |
| Tension en deux temps avant la résolution | 2 → 8 → 5 → 1 | Dm9 → G7sus4 → G13 → Cmaj9 |
| Boucle flottante, bonne pour improviser | 6 → 4 → 2 → 3 | Am9 → Fmaj7#11 → Dm9 → Em7 |

#### Mode 2 : accords simples

| Bouton | Accord | Rôle | Basse | Notes aiguës | Numéros MIDI (basse en premier) |
|---|---|---|---|---|---|
| 1 | **C** | I (repos) | C3 | C4 E4 G4 | 48 · 60 · 64 · 67 |
| 2 | **Dm** | ii | D3 | D4 F4 A4 | 50 · 62 · 65 · 69 |
| 3 | **Em** | iii | E3 | E4 G4 B4 | 52 · 64 · 67 · 71 |
| 4 | **F** | IV | F3 | C4 F4 A4 | 53 · 60 · 65 · 69 |
| 5 | **G** | V | G3 | B3 D4 G4 | 55 · 59 · 62 · 67 |
| 6 | **Am** | vi | A3 | C4 E4 A4 | 57 · 60 · 64 · 69 |
| 7 | **Bb** | ♭VII emprunté | Bb2 | Bb3 D4 F4 | 46 · 58 · 62 · 65 |
| 8 | **G7** | V7 (résolution forte) | G2 | G3 B3 D4 F4 | 43 · 55 · 59 · 62 · 65 |

Progressions conseillées (numéros de boutons) :

| Ambiance | Boutons | Accords |
|---|---|---|
| La plus utilisée en pop | 1 → 5 → 6 → 4 | C → G → Am → F |
| Même progression, plus sombre | 6 → 4 → 1 → 5 | Am → F → C → G |
| Style années 50 | 1 → 6 → 4 → 5 | C → Am → F → G |
| Rock, un peu « stade » | 1 → 7 → 4 → 1 | C → Bb → F → C |
| Cadence classique, très satisfaisante | 2 → 8 → 1 | Dm → G7 → C |
| En mineur | 6 → 4 → 5 → 6 | Am → F → G → Am |

#### Ajouter un mode ou modifier un accord

Tout se fait dans **`chords.h`**, qui contient ses propres instructions. Copiez un bloc de 8 lignes à la suite des autres (sans oublier la virgule entre deux blocs) et changez les numéros de notes. Le nombre de modes est calculé automatiquement : rien d'autre à modifier, et le clic de l'encodeur les parcourt tous.

```cpp
  // ----- Mode 3 : mes accords -----
  {
    {48, 60, 64, 67},   // 1 : C
    {50, 62, 65, 69},   // 2 : Dm
    // ... une ligne par bouton, 8 au total
  },
```

Règles : jusqu'à `MAX_NOTES` (6) notes par bouton, numéros de 1 à 127 (la note 0 est réservée : elle marque la fin d'une ligne). Une ligne trop longue donne une erreur de compilation : augmentez `MAX_NOTES`. Les modes sont stockés en mémoire programme et ne consomment pas de RAM.

---

## 5. Fonctionnement du logiciel

### 5.1 Recopie du clavier (Thru)

La bibliothèque MIDI est configurée en recopie complète :

```cpp
MIDI.begin(MIDI_CHANNEL_OMNI);      // écoute tous les canaux
MIDI.turnThruOn(midi::Thru::Full);  // recopie tout vers la sortie
```

À chaque appel de `MIDI.read()`, les messages complets reçus sont renvoyés sur la sortie. Les messages des composants sont envoyés avec `MIDI.sendControlChange()` et `MIDI.sendPitchBend()` dans la même boucle, donc **mélangés** à ce flux.

### 5.2 Réglages anti-perte de l'instance MIDI

```cpp
struct MySettings : public midi::DefaultSettings {
  static const bool Use1ByteParsing = false;                  // lit tout le tampon à chaque read()
  static const bool HandleNullVelocityNoteOnAsNoteOff = false; // ne modifie pas les Note On vélocité 0
  static const bool UseRunningStatus = true;                  // moins d'octets envoyés
  static const unsigned SysExMaxSize = 256;                   // sysex plus longs = tronqués
};
```

La bibliothèque n'offre pas de réglage qui désactive purement la vérification des données ; ce sont les options les plus proches.

### 5.3 Boucle principale

```
loop():
    MIDI.read()                          ← clavier → sortie
    1 lecture analogique (tournante) :   ← A0, A1, A2, A3, A4, A5, puis recommence
    MIDI.read()
    encodeur, clic joystick, 8 boutons
```

Une seule lecture analogique (~0,3 ms avec la correction anti-diaphonie) est faite par tour, pour ne jamais retarder la lecture du MIDI entrant (tampon de réception de l'Uno : 64 octets, environ 20 ms de MIDI). La variable `step` (0 à 5) est remise à zéro par un modulo : aucun risque de dépassement.

### 5.4 Détails par composant

- **Potentiomètres** : lecture anti-diaphonie (`readAnalog()` : première mesure jetée, puis moyenne de 2), filtre de bruit (variation d'au moins 15 sur 1023), conversion en 7 bits, butées exactes 0 et 127 (zone de 12), envoi seulement si la valeur change.
- **Joystick** : le centre est **calibré au démarrage** (ne pas toucher au joystick à l'allumage). Zone morte de 25 et butée atteinte 40 avant la fin de course. La réponse est progressive entre ces deux bornes. Le bend n'est envoyé que si la variation dépasse 48, ou lors d'un retour exact au centre ou aux butées.
- **Encodeur** : décodage par interruption sur D2/D3 (aucune transition perdue), 4 transitions = 1 cran (`ENC_TRANSITIONS`), pas de 2 par cran, valeur bornée à 0-127. Le clic change le mode des boutons.
- **Boutons** : anti-rebond de 20 ms, Note On à l'appui et Note Off au relâchement. Les notes viennent du mode courant (`chords.h`) ; un compteur par note gère les accords qui partagent des notes (§4.1).
- **Canal** : mis à jour à chaque Note On / Note Off reçu.

---

## 6. Installation et mise en route

1. **Bibliothèque** : dans l'IDE Arduino, gestionnaire de bibliothèques, installer **« MIDI Library »** de Francois Best (FortySevenEffects).
2. **Carte** : Arduino Uno.
3. **Switch du shield** : position *PROG*.
4. Ouvrir `midi-merge.ino` (le dossier doit s'appeler `midi-merge` et contenir aussi `chords.h`), puis **téléverser**.
5. Remettre le switch en position *RUN*.
6. Brancher le clavier sur **MIDI IN** et le synthé sur **MIDI OUT**.
7. Alimenter par l'USB, ou par une alimentation 7-12 V sur la prise jack.
8. Ne pas toucher au joystick pendant la mise sous tension (calibrage).

> Le moniteur série de l'IDE ne sert à rien ici : le port série est utilisé par le MIDI.

---

## 7. Réglages modifiables

En haut de `midi-merge.ino` (les notes et accords des boutons se règlent dans `chords.h`, voir §4.1) :

| Constante | Défaut | Effet |
|---|---|---|
| `POT_CC[4]` | 20 à 23 | Numéros de CC des potentiomètres |
| `BTN_VELOCITY` | 100 | Vélocité des notes jouées par les boutons (1-127) |
| `JOY_Y_CC` | 1 | CC de l'axe Y (1 = modulation) |
| `JOY_SW_CC` | 110 | CC du clic du joystick |
| `ENC_CC` | 30 | CC de l'encodeur |
| `ENC_STEP` | 2 | Incrément par cran |
| `ENC_TRANSITIONS` | 4 | Transitions CLK/DT par cran : essayer 2, puis 1, si l'encodeur demande plusieurs crans |
| `POT_NOISE` | 15 | Variation minimale (sur 1023) pour envoyer un CC |
| `POT_END_ZONE` | 12 | Butées : brut ≤ 12 donne 0, brut ≥ 1011 donne 127 |
| `JOY_DEADZONE` | 25 | Zone morte du joystick (sur 1023) |
| `JOY_MARGIN` | 40 | Marge avant la butée |
| `INVERT_Y` | `false` | `true` si « haut » ne donne pas de modulation |
| `SysExMaxSize` | 256 | Taille max d'un sysex recopié |

---

## 8. Tests et validation

Procéder dans cet ordre, pour isoler les problèmes.

### 8.1 Test de la recopie seule (matériel)

Téléverser ce mini-sketch (switch en *PROG*, puis *RUN*) :

```cpp
void setup() { Serial.begin(31250); }
void loop()  { if (Serial.available()) Serial.write(Serial.read()); }
```

Jouer sur le clavier : si les notes ressortent sur OUT, le matériel (câblage, switch, optocoupleur) est bon.

### 8.2 Test du contrôleur autonome

Sans rien sur MIDI IN, téléverser la v3 et brancher OUT sur un synthé ou une interface. Bouger les composants : les messages sortent sur le canal 1.

### 8.3 Voir les messages en clair, sans le shield

Avec l'Uno en USB, les mêmes octets sortent sur le port USB :

1. Installer **Hairless MIDI↔Serial Bridge** et un port MIDI virtuel (**loopMIDI** sous Windows).
2. Dans Hairless : choisir le port de l'Uno, vitesse **31250**.
3. Ouvrir un moniteur MIDI (**MIDI-OX**, **MIDIberry**…) sur le port virtuel : chaque CC et chaque bend s'affiche avec ses valeurs.

Le dépôt contient aussi **`moniteur_midi.py`**, un moniteur MIDI pour PC (interface Tkinter) qui écoute tous les ports d'entrée MIDI. Installation : `pip install mido python-rtmidi` ; lancement : `python moniteur_midi.py`.

![Moniteur MIDI](Midi_Monitor.png)

### 8.4 Simulation (optionnelle)

**Wokwi** (wokwi.com) simule un Uno avec potentiomètres, boutons, encodeur et joystick. Il permet de vérifier que les composants réagissent, mais il n'affiche pas le MIDI en clair et ne simule pas de clavier MIDI en entrée, donc il ne remplace pas §8.3.

### 8.5 Test complet

Clavier sur IN, synthé sur OUT : jouer des notes en tournant un potentiomètre ou en bougeant le joystick. Les notes doivent sonner sans trou, et les contrôles agir sur le même canal.

### 8.6 Test des modes d'accords

1. Au démarrage, le bouton 1 joue un do (60) : mode 0, notes seules.
2. **Clic encodeur** : le bouton 1 joue maintenant Cmaj9 (5 notes : 48, 64, 67, 71, 74).
3. **Clic encodeur** : le bouton 1 joue C (4 notes : 48, 60, 64, 67).
4. **Clic encodeur** : retour aux notes seules.
5. En mode 2, tenir C (bouton 1) et F (bouton 4), puis relâcher C : le do 60, partagé, **doit continuer de sonner** jusqu'au relâchement de F.
6. Tenir un bouton, cliquer sur l'encodeur, relâcher : les notes du mode d'origine doivent s'éteindre, sans note bloquée.

---

## 9. Dépannage

| Symptôme | Cause probable | Action |
|---|---|---|
| Aucun message ne sort | Switch du shield mal positionné | Mettre sur *RUN* (et sur *PROG* pour téléverser) |
| Téléversement impossible | Switch en *RUN* ou clavier branché | Passer en *PROG*, débrancher le MIDI IN |
| Les notes du clavier ne ressortent pas | Câblage IN / OUT inversé, ou optocoupleur non alimenté | Test §8.1, vérifier les branchements |
| Rien en entrée mais les contrôles sortent | Problème limité à l'entrée (broches 4/5 du DIN IN inversées, câble) | Inverser les broches 4 et 5 de l'entrée |
| Le synthé reçoit des notes mais pas les CC | CC non assignés dans le synthé | Utiliser « MIDI learn » ou un moniteur MIDI |
| CC aléatoires sur une entrée | Entrée analogique non câblée (flottante) | Câbler le potentiomètre ou la brancher à GND |
| Pitch bend décalé au repos | Joystick touché au démarrage | Redémarrer sans toucher au joystick |
| Sens du joystick Y inversé | Montage mécanique | `INVERT_Y = true` |
| Sens de l'encodeur inversé | CLK / DT inversés | Échanger les deux fils |
| L'encodeur demande plusieurs crans pour bouger | Il produit 2 (ou 1) transitions par cran | `ENC_TRANSITIONS = 2`, puis 1 |
| Les boutons ne jouent pas ce que j'attends | Autre mode actif (aucun indicateur lumineux) | Cliquer sur l'encodeur jusqu'au mode voulu ; le mode 0 revient à chaque démarrage |
| Erreur de compilation « too many initializers » | Une ligne de `chords.h` dépasse `MAX_NOTES` | Retirer des notes ou augmenter `MAX_NOTES` |
| Bend sans effet audible | Plage de bend du synthé à 0 | Régler la plage à ±2 dans le synthé |
| Un bouton envoie plusieurs messages | Rebonds mécaniques importants | Augmenter l'anti-rebond (20 ms) |
| Sysex tronqué ou perdu | Message plus long que `SysExMaxSize` | Augmenter la valeur (consomme de la RAM) |
| Notes perdues pendant des gestes rapides | Charge trop forte sur la boucle | Retirer des composants non utilisés, ou me demander une version à recopie par octets |

---

## 10. Boîtier et prises MIDI déportées

### 10.1 Deux méthodes

**A. Dessouder les prises DIN du shield** et câbler des prises panneau par des fils courts (10 à 30 cm, de préférence torsadés).
**B. Sans soudure** : brancher sur les prises du shield des rallonges DIN 5 broches avec prise femelle à fixer sur panneau (« MIDI panel mount extension cable »).

### 10.2 Broches utiles (DIN 5 broches, 180°)

<img src="docs/images/hero-din.svg" width="220" alt="Prise DIN 5 broches">

| Prise | Broches à câbler | Rôle |
|---|---|---|
| **OUT** | 5, 4, 2 | 5 = signal, 4 = +5 V (via 220 Ω), 2 = masse |
| **IN** | 4 et 5 | Boucle de courant vers l'optocoupleur |

- Les broches 1 et 3 ne servent pas.
- Ne **pas** relier la broche 2 de l'entrée IN (la norme demande de la laisser libre).
- Si rien n'arrive à l'Arduino, inverser les broches 4 et 5 de l'entrée.

### 10.3 Précautions

- Fils courts, éloignés des potentiomètres et du joystick.
- Boîtier métallique : isoler les prises (collerette plastique ou rondelle isolante) pour éviter une boucle de masse.
- Rallonges du commerce : vérifier **5 conducteurs**, câblage **1:1**, **180°** (pas 240° ni 3 broches).
- Il faut 2 prises (IN et OUT) ; le THRU n'est pas utilisé.

---

## 11. Limites connues et évolutions possibles

**Limites**
- Les modes d'accords ne sont pas encore validés sur matériel (testés en simulation sur PC) ; le reste du firmware a été validé avant leur ajout.
- Aucun indicateur du mode courant (pas de broche libre) : le mode revient à 0 (notes seules) à chaque démarrage.
- Toutes les broches de l'Uno sont utilisées : pas de place pour des LED ou un afficheur sans changer de carte.
- Pas de logs possibles sans sacrifier une broche (une version de test avec logs sur D4 avait été étudiée puis abandonnée comme trop complexe).
- Les messages temps réel (horloge, etc.) sont recopiés, mais le temps de traitement des composants s'ajoute au trajet du MIDI (latence faible).
- Un sysex plus long que `SysExMaxSize` peut être tronqué.

**Évolutions possibles**
- Version à recopie **octet par octet** avec tampon (aucune interprétation, insertion des CC entre deux messages) si la bibliothèque pose problème.
- Envoi d'un **RPN** au démarrage pour forcer la plage de pitch bend à ±2 demi-tons.
- Envoi d'un CC à chaque changement de mode, pour l'afficher dans le logiciel de musique.
- Canal MIDI fixe réglable, ou sélectionnable avec l'encodeur.
- Passage à une carte avec plus d'entrées (Mega) pour ajouter des contrôles ou des LED.

---

---

## 12. Annexe : fichiers source

- **`midi-merge.ino`** : sketch Arduino (recopie du clavier, potentiomètres, joystick, encodeur, boutons). Entièrement commenté.
- **`chords.h`** : définition des modes des 8 boutons (notes seules, accords enrichis, accords simples). C'est le seul fichier à modifier pour ajouter des accords (§4.1).
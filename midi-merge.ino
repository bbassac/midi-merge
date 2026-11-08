// =============================================================================
//  MIDI Merge : Arduino Uno + shield MIDI
//
//  Rôle : se placer entre un clavier MIDI et un synthé.
//    - tout ce que joue le clavier (MIDI IN) est recopié vers MIDI OUT ;
//    - les composants du boîtier ajoutent leurs propres messages au même flux.
//  Sans clavier branché, le boîtier fonctionne seul comme contrôleur (canal 1).
//
//  Composants et messages générés :
//    Potentiomètres A0-A3 ........ CC 20 à 23
//    Joystick X (A4) ............. pitch bend (±2 demi-tons si le synthé est à ±2)
//    Joystick Y (A5) ............. CC 1 (modulation), centre -> haut uniquement
//    Joystick clic (D13) ......... CC 110 (127 appuyé / 0 relâché)
//    Encodeur CLK/DT (D2/D3) ..... CC 30, valeur absolue 0-127
//    Encodeur clic (D4) .......... remet la valeur de l'encodeur à 64
//    8 boutons (D5-D12) .......... notes : Note On à l'appui, Note Off au relâchement
//
//  Contraintes matérielles à garder en tête :
//    - D0/D1 appartiennent au MIDI (port série matériel) : rien d'autre dessus,
//      et le moniteur série de l'IDE est inutilisable.
//    - Switch du shield : PROG pour téléverser, RUN pour utiliser.
// =============================================================================

#include <MIDI.h>

// ---------- Réglages de l'instance MIDI ----------
// La bibliothèque n'a pas d'option "ne rien vérifier" : ces réglages sont les plus
// proches pour recopier le flux du clavier sans le déformer ni perdre d'octets.
struct MySettings : public midi::DefaultSettings {
  static const long BaudRate = 31250;  // Débit imposé par la norme MIDI DIN
  static const bool Use1ByteParsing = false;                  // vide tout le tampon de réception à chaque read(), au lieu d'1 octet : indispensable pour ne pas déborder le tampon de 64 octets de l'Uno
  static const bool HandleNullVelocityNoteOnAsNoteOff = false; // laisse passer les "Note On vélocité 0" tels quels (certains claviers s'en servent comme Note Off)
  static const bool UseRunningStatus = true;                  // n'envoie pas l'octet de statut s'il est identique au précédent : moins d'octets, donc moins de latence
  static const unsigned SysExMaxSize = 256;                   // taille du tampon sysex en RAM ; un sysex plus long est tronqué
};
MIDI_CREATE_CUSTOM_INSTANCE(HardwareSerial, Serial, MIDI, MySettings);

// ---------- Brochage ----------
// Les 20 broches de l'Uno sont toutes utilisées (D0/D1 réservées au MIDI).
const uint8_t POT_PINS[4] = {A0, A1, A2, A3};
const uint8_t JOY_X = A4, JOY_Y = A5, JOY_SW = 13;
// ENC_A et ENC_B sont sur D2 et D3 : ce sont les seules broches d'interruption
// externe de l'Uno (INT0 et INT1), nécessaires au décodage de l'encodeur.
const uint8_t ENC_A = 2, ENC_B = 3, ENC_SW = 4;
const uint8_t BTN_PINS[8] = {5, 6, 7, 8, 9, 10, 11, 12};

// ---------- CC ----------
const uint8_t POT_CC[4] = {20, 21, 22, 23};
const uint8_t JOY_Y_CC = 1;        // modulation
const uint8_t JOY_SW_CC = 110;
const uint8_t ENC_CC = 30, ENC_STEP = 2;   // ENC_STEP : valeur ajoutée par cran
const int8_t  ENC_TRANSITIONS = 4;   // transitions CLK/DT par cran : 4 (le plus courant), sinon essayer 2 ou 1 si la valeur bouge trop peu ou trop vite

// ---------- Boutons = notes ----------
const uint8_t BTN_NOTE[8] = {60, 62, 64, 65, 67, 69, 71, 72};  // do ré mi fa sol la si do (60 = do central)
const uint8_t BTN_VELOCITY = 100;                              // vélocité fixe : les boutons n'ont pas de capteur de force (1-127)

// ---------- Potentiomètres ----------
const int POT_NOISE    = 15;       // variation minimale (sur 1023) pour envoyer un CC : filtre le bruit
const int POT_END_ZONE = 12;       // en butée, raw <= 12 vaut 0 et raw >= 1011 vaut 127 : le bruit ne bloque plus les extrémités

// ---------- Joystick ----------
const int  JOY_DEADZONE = 25;      // zone morte autour du centre (sur 0-1023) : évite que le bruit ou un léger décalage au repos génère du bend
const int  JOY_MARGIN   = 40;      // butée atteinte un peu avant la fin de course mécanique, pour atteindre les valeurs extrêmes
const bool INVERT_Y     = false;   // passer à true si "haut" ne donne pas de modulation

// Canal MIDI utilisé pour tous les messages des composants (1-16, comme attend la bibliothèque).
// Mis à jour à chaque note du clavier : les contrôles suivent ainsi le canal du clavier.
uint8_t channel = 1;

// ---------- États ----------
int16_t potRaw[4]; uint8_t potVal[4];   // potRaw : dernière mesure retenue (0-1023) ; potVal : dernier CC envoyé (0-127)
// joyXCenter/joyYCenter : position de repos mesurée au démarrage (pas de calibrage fixe).
// joyXLast : dernier pitch bend envoyé (-8192..8191) ; joyYLast : dernier CC de modulation envoyé (0..127).
int16_t joyXCenter, joyYCenter, joyXLast = 0, joyYLast = 0;
// btnState : dernier état lu (HIGH = relâché, car pull-up) ; btnChan : canal du Note On en cours, pour y renvoyer le Note Off.
uint8_t btnState[8], btnChan[8], joySwState = HIGH, encSwState = HIGH;
// Partagés avec l'interruption de l'encodeur : "volatile" oblige le compilateur à relire la mémoire à chaque accès.
volatile uint8_t encPrev;      // dernier état (CLK<<1 | DT) vu par l'interruption
volatile int8_t encAcc = 0;   // transitions cumulées par l'interruption, consommées par readEncoder()
// Horodatage du dernier changement pour l'anti-rebond : indices 0-7 = boutons, 8 = clic joystick, 9 = clic encodeur.
unsigned long lastBtn[10];
int16_t encValue = 64;        // valeur courante de l'encodeur (0-127), partagée entre rotation et clic
uint8_t step = 0;             // voie analogique lue au prochain tour de loop() (0-5)

// Appelés par la bibliothèque pour chaque note reçue du clavier (jamais pour nos propres envois).
// Ils ne servent qu'à suivre le canal ; la recopie vers la sortie est faite par le Thru.
void handleNoteOn(byte ch, byte, byte)  { channel = ch; }
void handleNoteOff(byte ch, byte, byte) { channel = ch; }

void setup() {
  // Pull-up internes : un bouton relié à GND lit LOW quand il est appuyé, sans résistance externe.
  for (uint8_t i = 0; i < 8; i++) { pinMode(BTN_PINS[i], INPUT_PULLUP); btnState[i] = HIGH; }
  pinMode(JOY_SW, INPUT_PULLUP);
  pinMode(ENC_A, INPUT_PULLUP); pinMode(ENC_B, INPUT_PULLUP); pinMode(ENC_SW, INPUT_PULLUP);
  encPrev = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);   // état initial, pour que la 1re transition soit correcte
  attachInterrupt(digitalPinToInterrupt(ENC_A), encoderISR, CHANGE);   // D2 = INT0
  attachInterrupt(digitalPinToInterrupt(ENC_B), encoderISR, CHANGE);   // D3 = INT1
  // Valeurs initiales relevées avant la 1re boucle : évite un envoi de tous les CC à l'allumage.
  for (uint8_t i = 0; i < 4; i++) { potRaw[i] = readAnalog(POT_PINS[i]); potVal[i] = potToCC(potRaw[i]); }
  joyXCenter = readAnalog(JOY_X);   // calibrage : joystick au repos au démarrage
  joyYCenter = readAnalog(JOY_Y);

  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.begin(MIDI_CHANNEL_OMNI);         // écoute tous les canaux
  MIDI.turnThruOn(midi::Thru::Full);     // recopie tout ce qui entre, sans filtre
}

void loop() {
  // Priorité absolue au MIDI entrant : le tampon matériel de l'Uno ne fait que 64 octets
  // (environ 20 ms de MIDI). Rien dans cette boucle ne doit durer assez longtemps pour le déborder.
  MIDI.read();
  // Une seule voie analogique par tour (une lecture ~0,3 ms avec readAnalog), en tournant sur les 6 voies.
  if (step < 4) readPot(step);
  else if (step == 4) readJoyX();
  else readJoyY();
  step = (step + 1) % 6;
  MIDI.read();   // 2e passage : vide le tampon accumulé pendant la lecture analogique
  readEncoder();
  readSwitch(JOY_SW, joySwState, 8, JOY_SW_CC);
  readButtons();
}

// Lecture analogique anti-diaphonie : après un changement de broche, la 1re mesure
// est jetée (le multiplexeur de l'Uno garde une charge de la voie précédente, ce qui
// fait "fuir" un potentiomètre sur son voisin), puis moyenne de 2 mesures.
// Comme loop() change de voie à chaque tour, chaque lecture est toujours "la première
// après un changement" : sans cette mesure jetée, la diaphonie est systématique.
int readAnalog(uint8_t pin) {
  analogRead(pin);                                   // mesure jetée
  return (analogRead(pin) + analogRead(pin) + 1) >> 1;
}

// Conversion 10 bits -> CC (0-127). Les extrémités ont une petite zone dédiée : sans elle,
// raw>>3 n'atteint 127 que pour raw >= 1016 et 0 que pour raw < 8, ce que le bruit ou la
// résistance du potentiomètre en butée empêchent parfois d'obtenir.
uint8_t potToCC(int raw) {
  if (raw <= POT_END_ZONE) return 0;
  if (raw >= 1023 - POT_END_ZONE) return 127;
  return raw >> 3;
}

// Potentiomètre : hystérésis pour ne pas inonder le MIDI de CC dus au bruit.
void readPot(uint8_t i) {
  int raw = readAnalog(POT_PINS[i]);
  uint8_t v = potToCC(raw);
  // Arriver en butée (0 ou 127) est toujours envoyé, même si le mouvement est inférieur au seuil de bruit.
  // Pas d'oscillation possible : une fois en butée, il faut bouger d'au moins POT_NOISE pour en sortir.
  bool enteringEnd = (v == 0 || v == 127) && v != potVal[i];
  if (!enteringEnd && abs(raw - potRaw[i]) < POT_NOISE) return;   // filtre le bruit
  potRaw[i] = raw;
  if (v != potVal[i]) { potVal[i] = v; MIDI.sendControlChange(POT_CC[i], v, channel); }
}

// X : pitch bend progressif, 0 au centre, -8192 à gauche, +8191 à droite
// (= -2 / +2 demi-tons si le synthé est réglé sur la plage standard de ±2)
void readJoyX() {
  int dx = readAnalog(JOY_X) - joyXCenter;          // écart au centre mesuré au démarrage
  int bend = 0;
  // On retranche la zone morte avant la conversion, pour que la réponse démarre à 0 (sans saut) à sa sortie.
  // Le centre n'est pas forcément à 512 : chaque côté est converti sur sa propre course.
  if (dx > JOY_DEADZONE)
    bend = map(dx - JOY_DEADZONE, 0, (1023 - joyXCenter) - JOY_DEADZONE - JOY_MARGIN, 0, 8191);
  else if (dx < -JOY_DEADZONE)
    bend = map(dx + JOY_DEADZONE, 0, -(joyXCenter - JOY_DEADZONE - JOY_MARGIN), 0, -8192);
  bend = constrain(bend, -8192, 8191);              // map() ne borne pas : le joystick peut dépasser la butée prévue
  // On envoie si le bend a bougé d'au moins 48 (évite le bruit), mais toujours au retour
  // exact au centre (0) et aux butées, pour ne jamais rester bloqué à une valeur proche.
  bool edge = (bend == 0 || bend == 8191 || bend == -8192) && bend != joyXLast;
  if (edge || abs(bend - joyXLast) >= 48) {
    joyXLast = bend;
    MIDI.sendPitchBend(bend, channel);
  }
}

// Y : modulation (CC1) entre le centre (0) et le haut (127), rien vers le bas
void readJoyY() {
  int raw = readAnalog(JOY_Y);
  // dy est positif vers "le haut" ; INVERT_Y compense le sens de montage du joystick.
  int dy = INVERT_Y ? raw - joyYCenter : joyYCenter - raw;
  int span = (INVERT_Y ? 1023 - joyYCenter : joyYCenter) - JOY_DEADZONE - JOY_MARGIN;  // course utile vers le haut
  int y = 0;
  if (dy > JOY_DEADZONE) y = map(dy - JOY_DEADZONE, 0, span, 0, 127);
  y = constrain(y, 0, 127);
  if (y != joyYLast) { joyYLast = y; MIDI.sendControlChange(JOY_Y_CC, y, channel); }
}

// Interrupteur simple (ici le clic du joystick) : CC à 127 à l'appui, 0 au relâchement.
// idx = case de lastBtn[] réservée à cet interrupteur.
void readSwitch(uint8_t pin, uint8_t &state, uint8_t idx, uint8_t cc) {
  uint8_t s = digitalRead(pin);
  // Anti-rebond : le premier changement passe aussitôt (pas de latence), puis on ignore les 20 ms suivantes.
  if (s != state && millis() - lastBtn[idx] > 20) {
    state = s; lastBtn[idx] = millis();
    MIDI.sendControlChange(cc, s == LOW ? 127 : 0, channel);
  }
}

// Décodage de l'encodeur par interruption : chaque changement de CLK ou DT est
// pris en compte immédiatement, même pendant une lecture analogique ou une
// émission MIDI. (En scrutation, une rotation rapide perdait des transitions.)
// Doit rester très courte : pendant qu'elle s'exécute, les octets MIDI entrants attendent.
void encoderISR() {
  // Table d'états de quadrature, indexée par (ancien état << 2) | nouvel état :
  // +1 / -1 pour un pas valide dans un sens ou l'autre, 0 pour "pas de changement" ou transition
  // impossible (rebond). Un rebond aller-retour s'annule donc tout seul.
  static const int8_t table[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
  uint8_t cur = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  encAcc += table[(encPrev << 2) | cur];
  encPrev = cur;
}

void readEncoder() {
  // encAcc est modifié par l'interruption : on le lit et on le corrige sans qu'elle puisse s'intercaler.
  noInterrupts();
  int8_t steps = encAcc / ENC_TRANSITIONS;       // crans complets accumulés (signe = sens)
  encAcc -= steps * ENC_TRANSITIONS;             // on garde le reste : une fraction de cran n'est pas perdue
  interrupts();
  if (steps != 0) {
    encValue = constrain(encValue + steps * ENC_STEP, 0, 127);   // valeur absolue, bornée (pas de débordement)
    MIDI.sendControlChange(ENC_CC, encValue, channel);
  }
  // Clic : remet la valeur au milieu et le signale au synthé.
  uint8_t s = digitalRead(ENC_SW);
  if (s != encSwState && millis() - lastBtn[9] > 20) {
    encSwState = s; lastBtn[9] = millis();
    if (s == LOW) { encValue = 64; MIDI.sendControlChange(ENC_CC, encValue, channel); }
  }
}

// Boutons : Note On à l'appui, Note Off au relâchement.
// Le canal du Note On est mémorisé pour envoyer le Note Off sur le même canal,
// même si le clavier change de canal entre-temps (évite les notes bloquées).
void readButtons() {
  for (uint8_t i = 0; i < 8; i++) {
    uint8_t s = digitalRead(BTN_PINS[i]);
    // Même anti-rebond que readSwitch(), appliqué à chaque bouton avec son propre horodatage.
    if (s == btnState[i] || millis() - lastBtn[i] <= 20) continue;
    btnState[i] = s; lastBtn[i] = millis();
    if (s == LOW) {                                  // appui (pull-up : LOW = appuyé)
      btnChan[i] = channel;
      MIDI.sendNoteOn(BTN_NOTE[i], BTN_VELOCITY, btnChan[i]);
    } else {                                         // relâchement
      MIDI.sendNoteOff(BTN_NOTE[i], 0, btnChan[i]);
    }
  }
}

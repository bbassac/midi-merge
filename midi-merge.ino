// Arduino Uno + shield MIDI : clavier -> + contrôleurs -> sortie
// V3 : MIDI Library avec instance personnalisée (réglages anti-perte)
// Switch du shield : PROG pour téléverser, RUN pour utiliser.

#include <MIDI.h>

// ---------- Réglages de l'instance MIDI ----------
struct MySettings : public midi::DefaultSettings {
  static const bool Use1ByteParsing = false;                  // lit TOUT le tampon à chaque read()
  static const bool HandleNullVelocityNoteOnAsNoteOff = false; // ne touche pas aux Note On vélocité 0
  static const bool UseRunningStatus = true;                  // moins d'octets envoyés, sortie plus fluide
  static const unsigned SysExMaxSize = 256;                   // sysex plus longs que ça = tronqués
};
MIDI_CREATE_CUSTOM_INSTANCE(HardwareSerial, Serial, MIDI, MySettings);

// ---------- Brochage ----------
const uint8_t POT_PINS[4] = {A0, A1, A2, A3};
const uint8_t JOY_X = A4, JOY_Y = A5, JOY_SW = 13;
const uint8_t ENC_A = 2, ENC_B = 3, ENC_SW = 4;
const uint8_t BTN_PINS[8] = {5, 6, 7, 8, 9, 10, 11, 12};

// ---------- CC ----------
const uint8_t POT_CC[4] = {20, 21, 22, 23};
const uint8_t BTN_CC[8] = {102, 103, 104, 105, 106, 107, 108, 109};
const uint8_t JOY_Y_CC = 1;        // modulation
const uint8_t JOY_SW_CC = 110;
const uint8_t ENC_CC = 30, ENC_STEP = 2;
const bool BUTTONS_TOGGLE = false;

// ---------- Joystick ----------
const int  JOY_DEADZONE = 25;      // zone morte autour du centre (sur 0-1023)
const int  JOY_MARGIN   = 40;      // butée atteinte un peu avant la fin de course
const bool INVERT_Y     = false;   // passer à true si "haut" ne donne pas de modulation

uint8_t channel = 1;               // suit le canal du clavier

// ---------- États ----------
int16_t potRaw[4]; uint8_t potVal[4];
int16_t joyXCenter, joyYCenter, joyXLast = 0, joyYLast = 0;
uint8_t btnState[8], btnOut[8], joySwState = HIGH, encSwState = HIGH, encPrev;
unsigned long lastBtn[10];
int16_t encValue = 64;
uint8_t step = 0;

void handleNoteOn(byte ch, byte, byte)  { channel = ch; }
void handleNoteOff(byte ch, byte, byte) { channel = ch; }

void setup() {
  for (uint8_t i = 0; i < 8; i++) { pinMode(BTN_PINS[i], INPUT_PULLUP); btnState[i] = HIGH; }
  pinMode(JOY_SW, INPUT_PULLUP);
  pinMode(ENC_A, INPUT_PULLUP); pinMode(ENC_B, INPUT_PULLUP); pinMode(ENC_SW, INPUT_PULLUP);
  encPrev = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  for (uint8_t i = 0; i < 4; i++) { potRaw[i] = analogRead(POT_PINS[i]); potVal[i] = potRaw[i] >> 3; }
  joyXCenter = analogRead(JOY_X);   // calibrage : joystick au repos au démarrage
  joyYCenter = analogRead(JOY_Y);

  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.turnThruOn(midi::Thru::Full);
}

void loop() {
  MIDI.read();
  // une seule lecture analogique par tour pour ne pas retarder la lecture MIDI
  if (step < 4) readPot(step);
  else if (step == 4) readJoyX();
  else readJoyY();
  step = (step + 1) % 6;
  MIDI.read();
  readEncoder();
  readSwitch(JOY_SW, joySwState, 8, JOY_SW_CC);
  readButtons();
}

void readPot(uint8_t i) {
  int raw = analogRead(POT_PINS[i]);
  if (abs(raw - potRaw[i]) < 4) return;            // filtre le bruit
  potRaw[i] = raw;
  uint8_t v = raw >> 3;
  if (v != potVal[i]) { potVal[i] = v; MIDI.sendControlChange(POT_CC[i], v, channel); }
}

// X : pitch bend progressif, 0 au centre, -8192 à gauche, +8191 à droite
// (= -2 / +2 demi-tons si le synthé est réglé sur la plage standard de ±2)
void readJoyX() {
  int dx = analogRead(JOY_X) - joyXCenter;
  int bend = 0;
  if (dx > JOY_DEADZONE)
    bend = map(dx - JOY_DEADZONE, 0, (1023 - joyXCenter) - JOY_DEADZONE - JOY_MARGIN, 0, 8191);
  else if (dx < -JOY_DEADZONE)
    bend = map(dx + JOY_DEADZONE, 0, -(joyXCenter - JOY_DEADZONE - JOY_MARGIN), 0, -8192);
  bend = constrain(bend, -8192, 8191);
  bool edge = (bend == 0 || bend == 8191 || bend == -8192) && bend != joyXLast;
  if (edge || abs(bend - joyXLast) >= 48) {
    joyXLast = bend;
    MIDI.sendPitchBend(bend, channel);
  }
}

// Y : modulation (CC1) entre le centre (0) et le haut (127), rien vers le bas
void readJoyY() {
  int raw = analogRead(JOY_Y);
  int dy = INVERT_Y ? raw - joyYCenter : joyYCenter - raw;
  int span = (INVERT_Y ? 1023 - joyYCenter : joyYCenter) - JOY_DEADZONE - JOY_MARGIN;
  int y = 0;
  if (dy > JOY_DEADZONE) y = map(dy - JOY_DEADZONE, 0, span, 0, 127);
  y = constrain(y, 0, 127);
  if (y != joyYLast) { joyYLast = y; MIDI.sendControlChange(JOY_Y_CC, y, channel); }
}

void readSwitch(uint8_t pin, uint8_t &state, uint8_t idx, uint8_t cc) {
  uint8_t s = digitalRead(pin);
  if (s != state && millis() - lastBtn[idx] > 20) {
    state = s; lastBtn[idx] = millis();
    MIDI.sendControlChange(cc, s == LOW ? 127 : 0, channel);
  }
}

void readEncoder() {
  static const int8_t table[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
  static int8_t acc = 0;
  uint8_t cur = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  acc += table[(encPrev << 2) | cur];
  encPrev = cur;
  if (acc >= 4 || acc <= -4) {
    encValue = constrain(encValue + (acc > 0 ? ENC_STEP : -ENC_STEP), 0, 127);
    acc = 0;
    MIDI.sendControlChange(ENC_CC, encValue, channel);
  }
  uint8_t s = digitalRead(ENC_SW);
  if (s != encSwState && millis() - lastBtn[9] > 20) {
    encSwState = s; lastBtn[9] = millis();
    if (s == LOW) { encValue = 64; MIDI.sendControlChange(ENC_CC, encValue, channel); }
  }
}

void readButtons() {
  for (uint8_t i = 0; i < 8; i++) {
    uint8_t s = digitalRead(BTN_PINS[i]);
    if (s == btnState[i] || millis() - lastBtn[i] <= 20) continue;
    btnState[i] = s; lastBtn[i] = millis();
    if (BUTTONS_TOGGLE) {
      if (s == LOW) { btnOut[i] = !btnOut[i]; MIDI.sendControlChange(BTN_CC[i], btnOut[i] ? 127 : 0, channel); }
    } else {
      MIDI.sendControlChange(BTN_CC[i], s == LOW ? 127 : 0, channel);
    }
  }
}

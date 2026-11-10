// =============================================================================
//  chords.h : modes des 8 boutons
//
//  Un clic sur l'encodeur passe au mode suivant (cycle : 0 -> 1 -> 2 -> ... -> 0).
//  Chaque mode donne, pour chacun des 8 boutons, la liste des notes MIDI jouées.
//  Un bouton peut jouer une note seule ou un accord (basse comprise).
//
//  POUR AJOUTER UN MODE : copier un bloc { ... } ci-dessous, le coller à la suite
//  (n'oubliez pas la virgule entre deux blocs) et changer les numéros de notes.
//  Le nombre de modes est calculé tout seul : rien d'autre à modifier.
//
//  Règles :
//    - 8 lignes par mode, une par bouton (bouton 1 en premier).
//    - chaque ligne : jusqu'à MAX_NOTES numéros de notes MIDI, de 1 à 127
//      (60 = do central, 48 = do une octave plus bas, 72 = do une octave plus haut).
//    - la note 0 ne peut pas être utilisée (elle marque la fin d'une liste).
//    - trop de notes sur une ligne = erreur de compilation : augmenter MAX_NOTES.
//    - des accords peuvent partager des notes : le Note Off n'est envoyé que
//      quand le dernier bouton qui tient la note est relâché.
// =============================================================================
#pragma once
#include <avr/pgmspace.h>

#define MAX_NOTES 6      // notes maximum par bouton (accord + basse)

// Les modes sont stockés en mémoire programme (PROGMEM) : ils ne consomment pas de RAM.
const uint8_t MODES[][8][MAX_NOTES] PROGMEM = {

  // ----- Mode 0 : notes seules (gamme de do majeur) -----
  {
    {60},   // 1 : do
    {62},   // 2 : ré
    {64},   // 3 : mi
    {65},   // 4 : fa
    {67},   // 5 : sol
    {69},   // 6 : la
    {71},   // 7 : si
    {72},   // 8 : do (octave)
  },

  // ----- Mode 1 : accords enrichis (basse en premier, puis notes aiguës) -----
  {
    {48, 64, 67, 71, 74},   // 1 : Cmaj9
    {50, 65, 69, 72, 76},   // 2 : Dm9
    {52, 67, 71, 74, 76},   // 3 : Em7
    {53, 64, 69, 71, 76},   // 4 : Fmaj7#11
    {55, 65, 71, 74, 76},   // 5 : G13
    {57, 60, 64, 67, 71},   // 6 : Am9
    {53, 60, 68, 72, 74},   // 7 : Fm6
    {55, 62, 65, 67, 72},   // 8 : G7sus4
  },

  // ----- Mode 2 : accords simples (basse en premier, puis notes aiguës) -----
  {
    {48, 60, 64, 67},       // 1 : C
    {50, 62, 65, 69},       // 2 : Dm
    {52, 64, 67, 71},       // 3 : Em
    {53, 60, 65, 69},       // 4 : F
    {55, 59, 62, 67},       // 5 : G
    {57, 60, 64, 69},       // 6 : Am
    {46, 58, 62, 65},       // 7 : Bb
    {43, 55, 59, 62, 65},   // 8 : G7
  },

  // ----- Ajouter de nouveaux modes ici, avant l'accolade de fermeture -----

};

const uint8_t MODE_COUNT = sizeof(MODES) / sizeof(MODES[0]);   // calculé automatiquement

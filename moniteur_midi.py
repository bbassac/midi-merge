#!/usr/bin/env python3
"""
Moniteur MIDI - écoute tous les ports d'entrée MIDI du PC.

Installation :
    pip install mido python-rtmidi

Lancement :
    python moniteur_midi.py

Note technique : la lecture se fait par "polling" (on interroge les ports
régulièrement) et non par callback. Cela évite le blocage (deadlock) que
python-rtmidi peut provoquer sous Windows quand on ferme un port pendant
qu'un callback est actif.
"""

import queue  # noqa: F401  (conservé pour compatibilité éventuelle)
import re
import tkinter as tk
from datetime import datetime
from tkinter import font as tkfont
from tkinter import messagebox, ttk

import mido
import rtmidi

# ----------------------------------------------------------------------
# Configuration
# ----------------------------------------------------------------------
MAX_LIGNES = 5000          # nombre max de lignes conservées à l'écran
INTERVALLE_MS = 10         # fréquence d'interrogation des ports
MAX_PAR_CYCLE = 200        # messages lus au maximum par port et par cycle

BG = "#1e1e2e"
BG_BARRE = "#181825"
FG = "#cdd6f4"
GRIS = "#6c7086"
VERT = "#a6e3a1"
ROUGE = "#f38ba8"

# Couleur par type de message
COULEURS = {
    "note_on": "#a6e3a1",          # vert
    "note_off": "#f38ba8",         # rouge
    "control_change": "#f9e2af",   # jaune
    "program_change": "#cba6f7",   # violet
    "pitchwheel": "#89dceb",       # cyan
    "aftertouch": "#fab387",       # orange
    "polytouch": "#fab387",
    "sysex": "#f5c2e7",            # rose
    "clock": "#585b70",            # gris foncé
    "active_sensing": "#585b70",
    "start": "#94e2d5",
    "stop": "#94e2d5",
    "continue": "#94e2d5",
    "songpos": "#74c7ec",
    "song_select": "#74c7ec",
    "reset": "#eba0ac",
}
COULEUR_DEFAUT = "#bac2de"

# Messages "bruyants" qu'on peut masquer
TYPES_BRUYANTS = {"clock", "active_sensing"}

# Noms des contrôleurs MIDI courants
NOMS_CC = {
    0: "Bank Select MSB", 1: "Modulation", 2: "Breath", 4: "Foot Ctrl",
    5: "Portamento Time", 6: "Data Entry MSB", 7: "Volume", 8: "Balance",
    10: "Pan", 11: "Expression", 32: "Bank Select LSB", 64: "Sustain",
    65: "Portamento", 66: "Sostenuto", 67: "Soft Pedal", 71: "Résonance",
    72: "Release", 73: "Attack", 74: "Cutoff", 91: "Reverb", 93: "Chorus",
    120: "All Sound Off", 121: "Reset Ctrl", 123: "All Notes Off",
}

NOMS_NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def nom_note(n: int) -> str:
    """60 -> C4 (convention mido / Middle C = C4)."""
    return f"{NOMS_NOTES[n % 12]}{n // 12 - 1}"


def analyser_liste_cc(texte: str):
    """
    Transforme '1, 7, 64, 20-31' en ensemble de numéros de CC.
    Retourne (ensemble, liste_des_erreurs).
    """
    ignores = set()
    erreurs = []
    for jeton in re.split(r"[,\s;]+", texte.strip()):
        if not jeton:
            continue
        m = re.fullmatch(r"(\d{1,3})(?:-(\d{1,3}))?", jeton)
        if not m:
            erreurs.append(jeton)
            continue
        debut = int(m.group(1))
        fin = int(m.group(2)) if m.group(2) is not None else debut
        if debut > fin:
            debut, fin = fin, debut
        if debut < 0 or fin > 127:
            erreurs.append(jeton)
            continue
        ignores.update(range(debut, fin + 1))
    return ignores, erreurs


# ----------------------------------------------------------------------
# Formatage des messages
# ----------------------------------------------------------------------
def decrire(msg):
    """Retourne (type_affiche, type_pour_couleur, valeurs_texte)."""
    t = msg.type

    if t == "note_on" and msg.velocity == 0:
        # Convention MIDI : note_on avec vélocité 0 = note_off
        return "NOTE OFF", "note_off", f"Note {msg.note:>3} ({nom_note(msg.note):<3})  Vel {msg.velocity:>3}"
    if t == "note_on":
        return "NOTE ON", t, f"Note {msg.note:>3} ({nom_note(msg.note):<3})  Vel {msg.velocity:>3}"
    if t == "note_off":
        return "NOTE OFF", t, f"Note {msg.note:>3} ({nom_note(msg.note):<3})  Vel {msg.velocity:>3}"
    if t == "control_change":
        nom = NOMS_CC.get(msg.control, "")
        return "CONTROL CHANGE", t, f"CC {msg.control:>3} {('(' + nom + ')') if nom else '':<18} Val {msg.value:>3}"
    if t == "program_change":
        return "PROGRAM CHANGE", t, f"Programme {msg.program:>3}"
    if t == "pitchwheel":
        return "PITCH BEND", t, f"Valeur {msg.pitch:>6}"
    if t == "aftertouch":
        return "AFTERTOUCH", t, f"Pression {msg.value:>3}"
    if t == "polytouch":
        return "POLY AFTERTOUCH", t, f"Note {msg.note:>3} ({nom_note(msg.note):<3})  Pression {msg.value:>3}"
    if t == "sysex":
        data = " ".join(f"{b:02X}" for b in msg.data)
        return "SYSEX", t, f"{len(msg.data)} octets : {data}"
    if t == "songpos":
        return "SONG POSITION", t, f"Pos {msg.pos}"
    if t == "song_select":
        return "SONG SELECT", t, f"Morceau {msg.song}"
    if t == "quarter_frame":
        return "QUARTER FRAME", t, f"Type {msg.frame_type}  Val {msg.frame_value}"

    return t.upper().replace("_", " "), t, ""


# ----------------------------------------------------------------------
# Application
# ----------------------------------------------------------------------
class MoniteurMidi(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Moniteur MIDI")
        self.geometry("980x620")
        self.configure(bg=BG)
        self.minsize(720, 380)

        self.entrees = []          # liste de (nom_du_port, rtmidi.MidiIn)
        self.en_ecoute = False
        self.compteur = 0
        self.ignores = 0
        self.nb_lignes = 0
        self.cc_ignores = set()
        self._id_after = None

        self.var_autoscroll = tk.BooleanVar(value=True)
        self.var_masquer = tk.BooleanVar(value=True)
        self.var_filtre_cc = tk.StringVar(value="")

        self._construire_ui()
        self.var_filtre_cc.trace_add("write", lambda *_: self._maj_filtre_cc())

        self._id_after = self.after(INTERVALLE_MS, self._sonder_ports)
        self.protocol("WM_DELETE_WINDOW", self._quitter)

    # ---------------- Interface ----------------
    def _construire_ui(self):
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("TButton", padding=(14, 6), font=("Segoe UI", 10, "bold"))
        style.configure("Start.TButton", background="#a6e3a1", foreground="#11111b")
        style.map("Start.TButton", background=[("active", "#94d49a")])
        style.configure("Stop.TButton", background="#f38ba8", foreground="#11111b")
        style.map("Stop.TButton", background=[("active", "#e0778f")])
        style.configure("Clear.TButton", background="#45475a", foreground=FG)
        style.map("Clear.TButton", background=[("active", "#585b70")])
        style.configure("TCheckbutton", background=BG_BARRE, foreground=FG)
        style.map("TCheckbutton", background=[("active", BG_BARRE)])

        # --- Barre du haut (boutons)
        barre = tk.Frame(self, bg=BG_BARRE, padx=10, pady=8)
        barre.pack(fill="x")

        self.btn_start = ttk.Button(barre, text="▶  Start", style="Start.TButton",
                                    command=self.basculer)
        self.btn_start.pack(side="left")

        ttk.Button(barre, text="🗑  Effacer", style="Clear.TButton",
                   command=self.effacer).pack(side="left", padx=8)

        ttk.Checkbutton(barre, text="Défilement auto",
                        variable=self.var_autoscroll).pack(side="left", padx=(16, 4))
        ttk.Checkbutton(barre, text="Masquer clock / active sensing",
                        variable=self.var_masquer).pack(side="left", padx=4)

        self.lbl_compteur = tk.Label(barre, text="0 message", bg=BG_BARRE, fg=GRIS,
                                     font=("Segoe UI", 10))
        self.lbl_compteur.pack(side="right")

        # --- Barre du filtre
        barre2 = tk.Frame(self, bg=BG_BARRE, padx=10)
        barre2.pack(fill="x", pady=(0, 6))

        tk.Label(barre2, text="Ignorer les Control Change n° :", bg=BG_BARRE, fg=FG,
                 font=("Segoe UI", 10)).pack(side="left")
        self.entry_cc = tk.Entry(barre2, textvariable=self.var_filtre_cc, width=28,
                                 bg="#313244", fg=FG, insertbackground=FG, relief="flat",
                                 font=("Consolas", 11), highlightthickness=1,
                                 highlightbackground="#45475a", highlightcolor="#89b4fa")
        self.entry_cc.pack(side="left", padx=8, ipady=3)
        tk.Label(barre2, text="ex : 1, 7, 64, 20-31", bg=BG_BARRE, fg=GRIS,
                 font=("Segoe UI", 9)).pack(side="left")
        self.lbl_filtre = tk.Label(barre2, text="", bg=BG_BARRE, fg=GRIS,
                                   font=("Segoe UI", 9))
        self.lbl_filtre.pack(side="right")

        # --- Zone de texte
        zone = tk.Frame(self, bg=BG)
        zone.pack(fill="both", expand=True)

        police = self._choisir_police_mono()
        self.texte = tk.Text(zone, bg=BG, fg=FG, font=police, wrap="none",
                             state="disabled", relief="flat", padx=10, pady=8,
                             insertbackground=FG, selectbackground="#45475a")
        scroll = ttk.Scrollbar(zone, orient="vertical", command=self.texte.yview)
        self.texte.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.texte.pack(side="left", fill="both", expand=True)

        # Tags de couleurs
        self.texte.tag_configure("heure", foreground=GRIS)
        self.texte.tag_configure("port", foreground="#7f849c")
        self.texte.tag_configure("canal", foreground="#b4befe")
        self.texte.tag_configure("info", foreground="#89b4fa")
        self.texte.tag_configure("erreur", foreground=ROUGE)
        for nom, couleur in COULEURS.items():
            self.texte.tag_configure(f"type_{nom}", foreground=couleur,
                                     font=(police[0], police[1], "bold"))
            self.texte.tag_configure(f"val_{nom}", foreground=couleur)
        self.texte.tag_configure("type_autre", foreground=COULEUR_DEFAUT,
                                 font=(police[0], police[1], "bold"))
        self.texte.tag_configure("val_autre", foreground=COULEUR_DEFAUT)

        # --- Barre d'état
        self.lbl_etat = tk.Label(self, text="⏹ Arrêté", anchor="w", bg=BG_BARRE,
                                 fg=GRIS, font=("Segoe UI", 9), padx=10, pady=4)
        self.lbl_etat.pack(fill="x")

        self._ecrire_info("Appuie sur Start pour écouter tous les ports d'entrée MIDI.")

    def _choisir_police_mono(self):
        disponibles = set(tkfont.families())
        for nom in ("Cascadia Mono", "Consolas", "JetBrains Mono", "Menlo",
                    "DejaVu Sans Mono", "Courier New"):
            if nom in disponibles:
                return (nom, 11)
        return ("TkFixedFont", 11)

    # ---------------- Filtre CC ----------------
    def _maj_filtre_cc(self):
        ignores, erreurs = analyser_liste_cc(self.var_filtre_cc.get())
        self.cc_ignores = ignores
        if erreurs:
            self.lbl_filtre.configure(text="⚠ invalide : " + ", ".join(erreurs[:4]), fg=ROUGE)
            self.entry_cc.configure(highlightbackground=ROUGE, highlightcolor=ROUGE)
        else:
            self.entry_cc.configure(highlightbackground="#45475a", highlightcolor="#89b4fa")
            if ignores:
                self.lbl_filtre.configure(text=f"✔ {len(ignores)} contrôleur(s) ignoré(s)", fg=VERT)
            else:
                self.lbl_filtre.configure(text="", fg=GRIS)

    # ---------------- Start / Stop ----------------
    def basculer(self):
        if self.en_ecoute:
            self.arreter()
        else:
            self.demarrer()

    def demarrer(self):
        try:
            sonde = rtmidi.MidiIn()
            noms = sonde.get_ports()
            del sonde
        except Exception as e:
            messagebox.showerror("Erreur MIDI", f"Impossible de lister les ports :\n{e}")
            return

        if not noms:
            messagebox.showwarning("Aucun port", "Aucun port d'entrée MIDI détecté.")
            return

        ouverts = []
        for index, nom in enumerate(noms):
            try:
                entree = rtmidi.MidiIn()
                entree.open_port(index)
                # On veut recevoir aussi sysex / timing / active sensing
                # (le masquage éventuel se fait dans l'affichage)
                entree.ignore_types(sysex=False, timing=False, active_sense=False)
                self.entrees.append((nom, entree))
                ouverts.append(nom)
            except Exception as e:
                self._ecrire_ligne_erreur(f"Impossible d'ouvrir « {nom} » : {e}")

        if not self.entrees:
            return

        self.en_ecoute = True
        self.btn_start.configure(text="■  Stop", style="Stop.TButton")
        self.lbl_etat.configure(text=f"🟢 Écoute sur {len(ouverts)} port(s) : " + " | ".join(ouverts),
                                fg=VERT)
        self._ecrire_info(f"--- Écoute démarrée ({len(ouverts)} port(s)) ---")

    def arreter(self):
        # Plus de callback : la fermeture d'un port est immédiate et sans risque.
        self.en_ecoute = False
        self._fermer_entrees()
        self.btn_start.configure(text="▶  Start", style="Start.TButton")
        self.lbl_etat.configure(text="⏹ Arrêté", fg=GRIS)
        self._ecrire_info("--- Écoute arrêtée ---")

    def _fermer_entrees(self):
        entrees, self.entrees = self.entrees, []
        for _nom, entree in entrees:
            try:
                entree.close_port()
            except Exception:
                pass
        del entrees

    def effacer(self):
        self.texte.configure(state="normal")
        self.texte.delete("1.0", "end")
        self.texte.configure(state="disabled")
        self.compteur = 0
        self.ignores = 0
        self.nb_lignes = 0
        self._maj_compteur()

    # ---------------- Réception (polling) ----------------
    def _sonder_ports(self):
        recus = False
        try:
            for nom, entree in list(self.entrees):
                for _ in range(MAX_PAR_CYCLE):
                    try:
                        resultat = entree.get_message()
                    except Exception:
                        break
                    if not resultat:
                        break
                    octets, _delta = resultat
                    try:
                        msg = mido.Message.from_bytes(octets)
                    except (ValueError, TypeError):
                        continue  # message malformé : on l'ignore
                    if self._traiter_message(datetime.now(), nom, msg):
                        recus = True

            if recus:
                self._limiter_lignes()
                if self.var_autoscroll.get():
                    self.texte.see("end")
                self._maj_compteur()
        except tk.TclError:
            return  # fenêtre détruite
        except Exception as e:  # ne jamais laisser mourir la boucle de lecture
            try:
                self._ecrire_ligne_erreur(f"Erreur de lecture : {e}")
            except Exception:
                pass

        self._id_after = self.after(INTERVALLE_MS, self._sonder_ports)

    def _traiter_message(self, horodatage, port, msg):
        """Applique les filtres puis affiche. Retourne True si le compteur a changé."""
        if self.var_masquer.get() and msg.type in TYPES_BRUYANTS:
            return False
        if msg.type == "control_change" and msg.control in self.cc_ignores:
            self.ignores += 1
            return True
        self._afficher(horodatage, port, msg)
        return True

    def _afficher(self, horodatage, port, msg):
        nom_type, type_couleur, valeurs = decrire(msg)
        cle = type_couleur if type_couleur in COULEURS else "autre"

        canal = f"Ch {msg.channel + 1:>2}" if hasattr(msg, "channel") else "     "
        port_court = (port[:22] + "…") if len(port) > 23 else port

        self.texte.configure(state="normal")
        self.texte.insert("end", horodatage.strftime("%H:%M:%S.") + f"{horodatage.microsecond // 1000:03d}  ", "heure")
        self.texte.insert("end", f"{port_court:<23}  ", "port")
        self.texte.insert("end", f"{canal}  ", "canal")
        self.texte.insert("end", f"{nom_type:<16}", f"type_{cle}")
        self.texte.insert("end", f"{valeurs}\n", f"val_{cle}")
        self.texte.configure(state="disabled")

        self.compteur += 1
        self.nb_lignes += 1

    def _limiter_lignes(self):
        if self.nb_lignes > MAX_LIGNES:
            exces = self.nb_lignes - MAX_LIGNES
            self.texte.configure(state="normal")
            self.texte.delete("1.0", f"{exces + 1}.0")
            self.texte.configure(state="disabled")
            self.nb_lignes = MAX_LIGNES

    # ---------------- Divers ----------------
    def _ecrire_info(self, texte):
        self.texte.configure(state="normal")
        self.texte.insert("end", texte + "\n", "info")
        self.texte.configure(state="disabled")
        self.nb_lignes += 1
        if self.var_autoscroll.get():
            self.texte.see("end")

    def _ecrire_ligne_erreur(self, texte):
        self.texte.configure(state="normal")
        self.texte.insert("end", "⚠ " + texte + "\n", "erreur")
        self.texte.configure(state="disabled")
        self.nb_lignes += 1
        if self.var_autoscroll.get():
            self.texte.see("end")

    def _maj_compteur(self):
        s = "s" if self.compteur > 1 else ""
        texte = f"{self.compteur} message{s}"
        if self.ignores:
            texte += f"  ·  {self.ignores} CC ignoré{'s' if self.ignores > 1 else ''}"
        self.lbl_compteur.configure(text=texte)

    def _quitter(self):
        self.en_ecoute = False
        if self._id_after is not None:
            try:
                self.after_cancel(self._id_after)
            except Exception:
                pass
        self._fermer_entrees()
        try:
            self.destroy()
        except Exception:
            pass


if __name__ == "__main__":
    MoniteurMidi().mainloop()
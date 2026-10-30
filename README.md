# OP1midiMap — a general MIDI mapping plugin for the Teenage Engineering OP-1 (OG)
# OP1midiMap — mappage MIDI généraliste pour l'OP-1 (OG)

(formerly **OP1midi**; plugin file and settings namespace keep the name `op1midi`,
 plugin version **1.7.5**; vibe coded with opencode + GLM-5.3-flash / Z.ai)

---

## EN — English

### What this is
`op1midi.knl` is a plugin for the **Hundekraft kennel runtime 3.0.3** (op1-workshop) on a
Teenage Engineering **OP-1 OG running base firmware R246**. It lets you trigger tape work
from any (USB) MIDI controller: sound selection, tape switching, track mutes (with fades),
marker jumps, and tape **LOOP** operations. CC, program-change and even the OP-1's own
keyboard notes can be mapped, per row (16 rows).

### Install (hardware)
1. Back up your OP-1 first: Shift + COM, then T3 (disk mode), copy everything to USB.
   See `install-recuperation.md` (backup and recovery, verified hashes for the base image
   `op1_246.op1` SHA-256 `c5315218f825f143b415ca554516541898abee843d3a236df0b54c04e1fb13a9`).
2. Put the kennel firmware on the device (Hundekraft workshop image, see the
   [op1-workshop](https://github.com/Hundekraft/op1-workshop) instructions).
3. Flash the shared image `op1r-src/op1_246_custom-repacked.op1`
   (SHA-256 `286c4124d6ffd37de182c8758f84f71ea45ffbc3e01e0b98e375e5f80417d0c4`, 12 411 779 bytes).
4. Workshop → Your plugins → add `op1midi.knl` → Update OP-1 → confirm with T1.

### Entering the setup
- Clock mode: **Shift + Help**, then **T3** — the settings page. The **MIDI MAP** row sits
  between the green MIDI SEQ switch and the red switch (a small "WHITE: EDIT" blinks there
  while the page is displayed).
- Turn the **white** encoder while MIDI MAP is highlighted: global mapping **on/off**.
- **Push the white** encoder in clock mode: the mapping **editor** opens (full-screen overlay).

### The editor (16 rows × 4 pages)
The cursor row is highlighted; columns use the encoder colors (blue/green/white/red).

| Encoder | Turn | Shift + turn | Push | Shift + push |
|---|---|---|---|---|
| **Blue** | move the cursor (the page follows) | — | cycle note display: `61` / `C#4` / `DO#4` | **clear the row** |
| **Green** | source number | row channel (1..16, ALL) | cycle source type NOTE/CC/PC | **learn MIDI** on/off |
| **White** | destination (flat list) | — | validate a COMBO | **learn COMBO** on/off |
| **Red** | fade: OFF / 1 s / 2 s / 8 s / 32 s | fade tracks ALL/P1/P2/P3/P4/P12/P34 | internal-midi reading of the row on/off (a small **i** appears before the source: red = the OP-1 keyboard triggers this row, gray = the keyboard plays normally) | nothing (removed relict) |

- **Arrows** = pages (1..16 rows, 4 per page). **Help** (with or without Shift) = validate and
  close; mode keys, T1–T4, SYNTH/DRUM/TAPE/MIXER, transport keys also close (that key then acts,
  except with Shift held). All exits save.
- While a **learn** is armed (green MIDI learn / white combo learn), every key is swallowed;
  Help always validates.
- MIDI learn: the first NOTE / CC / PC received is captured into the current row (a toast
  confirms). The row channel comes from the green Shift+turn.
- Combo learn (destination must be COMBO): press 1 to 3 keys (each with or without Shift; the
  third key auto-validates). Keys are shown live, e.g. `S06 21`.

### Sources
- **NOTE**: triggered by note-on with velocity > 0. External USB MIDI always works; the
  OP-1's own keyboard triggers the row only if the row's **internal reading** is on
  (red click; small `i` red vs gray). Mapped note-outs and note-offs are swallowed so
  excluded keys play the synth normally.
- **CC**: fires on a rising edge (value >= 64); stays silent while held.
- **PC**: fires on every program-change message.

### Destinations
| Destinations | Parameter | Action |
|---|---|---|
| `--` | — | row off |
| `L>` (right) | — | shifts the **loop region right** (same as Shift + arrow right everywhere in tape mode) |
| `L<` (left) | — | shifts the loop region left |
| `LOP` | — | loop **on/off** (toggles the existing loop region) |
| `COMBO` | 1..3 keys | replays the recorded key sequence as synthetic keys |
| `SND` 1..8 | sound number | opens SYNTH and presses the hard-coded sound key (verified on R246) |
| `TAPE` 1..4 | tape number | switches the tape (4 independent tapes in 3.0.3) |
| `MUTE` 1..4 | track | toggles the track mute; while playing the mute **fades** the muted track |
| `MRK` 1..32 | marker number | jumps to the shared marker set position (the tape's markers; if none is set at that slot, nothing happens) |
| `M+` / `M-` | — | previous / next marker in view, wrapping |

- The three loop destinations come **first** in the white destination list, right after OFF,
  before COMBO (v1.7.5, user feedback).
- The loop destinations use the tape services directly (no keys injected): they work the
  same while stopped and while playing, in tape, mixer and Shift+mixer views; they are
  refused while armed, recording, punch or in disk mode. Each step moves the region by
  1/16 of its width (minimum ~54 ms); the region width is kept and clamped to the tape.
  The display uses drawn **arrow / loop pictograms in green** (fixed color: readable on the
  white highlighted row and the dark background, and it recalls the OP-1 loop color).
- MUTE/MRK/M+/M- honor the row **fade**: while playing, the chosen tracks (red Shift+turn:
  all, track 1..4, or 1+2 / 3+4) fade to silence, the action applies at silence, then volume
  is restored. A MUTE only fades the muted track. Jumps (MRK/M+/M-) carry on playing.
- Captured CCs leave the firmware encoder/key path (no double action); captured notes are
  swallowed in the note pipeline.

### Persistence
Settings are stored in the plugin settings namespace (`op1midi.feat` mapping lines
`mmr0..15` as one int32 codec per row — format version 5 — plus combos `mmk0..15`, note
display `mmnd`, master switch `mmen`). A format change resets the 16 rows once: after
upgrading from an older release, re-map your lines.

### Build & tests
Tests run on a plain PC (no device, no toolchain):
```sh
cd mm2
cc -I. -Wall -Wextra -std=c99 mm2_test.c mm2_core.c mm2_edit.c -o build/mm2_test && ./build/mm2_test
```
The plugin build requires the bfin-elf Docker toolchain and a local copy of the Hundekraft
3.0.3 source bundle (never TE firmware):
```sh
cd hundekraft-plugin
bash build-knl.sh /path/to/hundekraft-3.0.3-source
# -> op1midi.knl
```

### Safety & legal
- GPL-3.0-or-later with the OP-1 firmware combination additional permission (see `hundekraft-plugin/LICENSE`).
- No Teenage Engineering firmware, image or key is included or redistributed by this repo.
- A corrupted image can brick the device — always keep a verified recovery copy; every flash
  happens on a validated image only. Advice, not warranty.

### Credits
Built on the Hundekraft kennel SDK (op1-workshop), itself based on pcppcp's op1r-src.
Marker-jump semantics port op1r's tape markers. **Vibe coded with opencode + GLM-5.3-flash (Z.ai)**.

---

## FR — Français

### Qu'est-ce que c'est
`op1midi.knl` est un plugin pour le **runtime kennel Hundekraft 3.0.3** (op1-workshop) sur
Teenage Engineering **OP-1 OG sous firmware de base R246**. Il permet de piloter le travail
tape depuis un contrôleur MIDI (USB): sélection de sons, changement de tape, mutes de pistes
(avec fondu), sauts de marqueurs et opérations de **boucle tape**. CC, program-change et
jusqu'aux notes du clavier de l'OP-1 peuvent être mappés, ligne par ligne (16 lignes).

### Installation (matériel)
1. **Sauvegardez d'abord** votre OP-1: Shift + COM, puis T3 (disk mode), copiez tout sur USB —
   voir `install-recuperation.md` (sauvegarde + récupération, hashes vérifiés de l'image de
   base `op1_246.op1` SHA-256 `c5315218f825f143b415ca554516541898abee843d3a236df0b54c04e1fb13a9`).
2. Mettez le firmware kennel sur l'appareil (image du workshop Hundekraft: voir
   [op1-workshop](https://github.com/Hundekraft/op1-workshop)).
3. Flashez l'image partagée `op1r-src/op1_246_custom-repacked.op1`
   (SHA-256 `286c4124d6ffd37de182c8758f84f71ea45ffbc3e01e0b98e375e5f80417d0c4`, 12 411 779 octets).
4. Workshop → Your plugins → ajoutez `op1midi.knl` → Update OP-1 → confirmez avec T1.

### Accès aux réglages
- Mode clock: **Shift + Help**, puis **T3** — la page settings. La rangée **MIDI MAP** est la
  rangée blanche, entre le switch vert MIDI SEQ et le switch rouge (un petit « WHITE: EDIT »
  y clignote quand la page est affichée).
- Tourner le **blanc** sur MIDI MAP: mapping global **on/off**.
- **Appuyer le blanc** en mode clock: ouvre l'**éditeur** (overlay plein écran).

### L'éditeur (16 lignes × 4 pages)
La ligne du curseur est en surbrillance; les colonnes reprennent les couleurs des encodeurs.

| Encodeur | Tourner | Shift + tourner | Appui | Shift + appui |
|---|---|---|---|---|
| **Bleu** | déplacer le curseur (la page suit) | — | affichage des notes: `61` / `C#4` / `DO#4` | **vider la ligne** |
| **Vert** | n° de source | canal de ligne (1..16, ALL) | type NOTE/CC/PC | **learn MIDI** on/off |
| **Blanc** | destination (liste plate) | — | valider un COMBO | **learn COMBO** on/off |
| **Rouge** | fade: OFF / 1 s / 2 s / 8 s / 32 s | pistes du fade ALL/P1/P2/P3/P4/P12/P34 | lecture du midi interne on/off (un petit **i** s'affiche devant la source: rouge = le clavier OP-1 déclenche la ligne, gris = le clavier joue normalement) | rien (reliquat supprimé) |

- **Flèches** = pages (lignes 1..16, 4 par page). **Help** (avec ou sans Shift) = valider et
  fermer; touches de mode, T1–T4, SYNTH/DRUM/TAPE/MIXER, transport ferment aussi (la touche
  agit ensuite, sauf Shift tenu). Toute sortie sauvegarde.
- Quand un **learn** est armé (learn MIDI vert / learn combo blanc), chaque touche est avalée;
  Help valide toujours.
- Learn MIDI: le premier NOTE / CC / PC reçu est capturé dans la ligne courante (un toast
  confirme). Le canal de ligne se règle par Shift+rotation verte.
- Learn combo (destination COMBO obligatoire): 1 à 3 touches (chacune avec ou sans Shift; la
  3e valide automatiquement). Les touches s'affichent au fil de l'eau, ex. `S06 21`.

### Sources
- **NOTE**: déclenchée par un note-on vélocité > 0. Le MIDI USB externe marche toujours; le
  clavier de l'OP-1 ne déclenche la ligne que si la **lecture du midi interne** de la ligne
  est active (clic rouge; petit i rouge vs gris). Ceux dont la ligne est inactive voient leur
  touche jouer le synthé normalement (note-on et note-off).
- **CC**: front montant (valeur >= 64); pas de répétition tant que tenu.
- **PC**: chaque program-change reçu déclenche.

### Destinations
| Destination | Paramètre | Action |
|---|---|---|
| `--` | — | ligne inactive |
| `L>` (droite) | — | **décale la région de boucle à droite** (comme Shift + flèche droite) |
| `L<` (gauche) | — | décale la boucle à gauche |
| `LOP` | — | boucle **on/off** (bascule la région de boucle existante) |
| `COMBO` | 1..3 touches | rejoue la séquence enregistrée comme touches synthétiques |
| `SND` 1..8 | n° du son | passe en SYNTH et presse la touche en dur du son (vérifié sur R246) |
| `TAPE` 1..4 | n° de tape | bascule de tape (4 tapes indépendantes en 3.0.3) |
| `MUTE` 1..4 | piste | toggle du mute; en lecture, le mute **fonde** la piste |
| `MRK` 1..32 | n° du marqueur | saut à l'emplacement de l'ensemble partagé de marqueurs (si le slot est vide, rien) |
| `M+` / `M-` | — | marqueur vu précédent / suivant, avec couvert |

- Les trois destinations loop viennent **en premier** dans la liste du blanc, juste après OFF,
  avant COMBO (v1.7.5, retour utilisateur).
- Les destinations loop touchent les services tape directement (aucune touche injectée):
  identique à l'arrêt et en lecture, vues tape/mixer/mixov; refusé armé, en enregistrement,
  punch ou disk mode. Chaque cran déplace la région de 1/16 de sa largeur (minimum ~54 ms);
  la largeur est conservée, la position clampée à la bande. L'affichage de l'éditeur dessine
  des **pictogrammes flèche / boucle en VERT** (couleur fixe: lisible sur le fond blanc de la
  ligne sélectionnée comme sur le fond sombre, et ça rappelle la boucle verte de l'OP-1).
- MUTE/MRK/M+/M- respectent le **fade** de la ligne: en lecture, les pistes choisies
  (Shift+rotation rouge: ALL, piste 1..4, ou 1+2 / 3+4) fondent vers le silence, l'action
  s'applique au silence, puis les volumes remontent. Un MUTE ne fonde que la piste mute.
  Les sauts (MRK/M+/M-) continuent la lecture.
- Les CC capturés quittent le chemin clés/encodeurs du firmware (pas d'action double); les
  notes capturées sont avalées dans le pipeline de notes.

### Persistance
Les réglages sont stockés dans l'espace de réglages du plugin (lignes `mmr0..15`, un int32
codec par ligne — version de format 5 — plus les combos `mmk0..15`, l'affichage des notes
`mmnd`, et le switch maître `mmen`). Un changement de format remet les 16 lignes à zéro une
seule fois: après une mise à jour, refaîtes vos mappings.

### Build & tests
Tests sur un simple PC (pas d'appareil, pas de toolchain):
```sh
cd mm2
cc -I. -Wall -Wextra -std=c99 mm2_test.c mm2_core.c mm2_edit.c -o build/mm2_test && ./build/mm2_test
```
Le build du plugin demande la toolchain Docker bfin-elf et une copie locale du bundle source
Hundekraft 3.0.3 (jamais le firmware TE):
```sh
cd hundekraft-plugin
bash build-knl.sh /chemin/vers/hundekraft-3.0.3-source
# -> op1midi.knl
```

### Sûreté & licence
- GPL-3.0-or-later avec la permission additionnelle de combinaison au firmware OP-1
  (voir `hundekraft-plugin/LICENSE`).
- Aucun firmware, image ou clé Teenage Engineering n'est inclus ou redistribué par ce dépôt.
- Une image corrompue peut bloquer l'appareil — gardez toujours une copie de récupération
  vérifiée; tout flash ne se fait que sur une image validée. Conseil, pas garantie.

### Crédits
Construit sur le SDK kennel d'Hundekraft (op1-workshop), lui-même basé sur op1r-src de pcppcp.
La sémantique des sauts de marqueurs porte celle d'op1r. **Vibe codé avec opencode + GLM-5.3-flash (Z.ai)**.

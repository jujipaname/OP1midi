# Rapport d'avancement — OP1midi (mapping MIDI OP-1 OG)
*Généré le 27 octobre 2026*

## 1. Architecture (décisions validées)
- Base : **fork de `pcppcp/op1r-src`** (Blackfin/R246) — cloné dans `op1r-src/`
- MVP : **markers next/prev + play-from-marker** ; config : **page Settings embarquée + outil compagnon**
- Analyse des points d'ancrage : `midi_cc_hook`, `key_event_hook`, pattern `MX_KEY`, `op1rsettings` (op1.db)

## 2. Code MVP — implémenté et testé ✅
| Fichier | Contenu |
|---|---|
| `synth_sdk/midi/mm.h` / `mm.c` **(nouveaux)** | mapping CC→actions (CC 80/81/82 par défaut), trigger front ≥64, canal configurable (`mmch`), CC réservés refusés, file thread KeyHandler (motif `mx.c`), gardes (mode tape, arrêt, punch, count-in) |
| `tape/markers.c` | ajout `tm_locate_current()` (marqueur courant, tolérance 10 ms) |
| `dispatcher.c` | 3 branchements : `mm_cc` dans `midi_cc_hook`, `MM_KEY 0x40` dans `key_event_hook`, `mm_load()` au boot |
| `screens/settings.c` | rangées verte « MIDI » (canal), blanche « NEXT », rouge « PREV » (CC éditables, wrap), play-from affiché lecture seule |
| `make_firmware.py` | `midi/mm.c` ajouté aux sources — zéro touch aux zones chiffrées (`l1_patches.json`, `hook.S`) |
| `harness/mm_test.c` **(nouveau)** + `markers_test.c`, `settings_test.c` étendus | ~30 assertions (canaux, edges, gardes, collisions, valeurs corrompues) |
| `tools/op1midi_map.py` **(nouveau)** | validation JSON stricte (version 1, actions, CC réservés, doublons), valeurs appareil `mmch/mm0..2`, envoi CC de test (mido) |

**Résultat : `harness/run_all.py` complet → all ok.** `midi-spec.md` mis à jour (section « État d'implémentation MVP »).
Rien n'a été commité dans git.

## 3. Build firmware sandbox — terminé ✅
- ✅ `op1_246.op1` récupéré via `git pull` (13 Mo, SHA-256 `c5315218…`, original protégé en lecture seule)
- ⚠️ **Le firmware TE propriétaire est poussé sur le repo GitHub** — à retirer du dépôt (règle 3 AGENTS.md)
- ✅ op1repacker cloné + venv ; unpack réussi → `op1r-src/op1_246/` (26 Mo)
- ✅ Toolchain Docker construite : 1er build tué par **OOM** (`make -j32` sur 7,8 Go RAM / 2 CPU) ;
  Dockerfile patché `-j2` (GCC uniquement, binutils en cache) → `bfin-elf-gcc 12.3.0` opérationnel
- ✅ `python3 make_firmware.py` (sans `--opz`) → **`op1_246_custom-repacked.op1`**
  (SHA-256 `286c4124d6ffd37de182c8758f84f71ea45ffbc3e01e0b98e375e5f80417d0c4`, 12 411 779 octets)

### Validation de l'image (règle 4 AGENTS.md) — **RÉSULTAT : SAIN / PASSÉE**
*Validée le 27 octobre 2026, ~15:54 UTC, sur copie sandbox — aucun appareil touché.*

**Environnement de build**
| Élément | Valeur |
|---|---|
| Cross-compilateur | `bfin-elf-gcc (GCC) 12.3.0` (image Docker `bfin-elf-toolchain`, 1,9 Go, Debian 12, binutils 2.40, build `-j2`) |
| Repacker | `op1repacker` (op1hacks), venv dédié |
| Commande | `cd synth_sdk && python3 make_firmware.py` (**sans** `--opz`) |

**Images en présence**
| Image | Taille | SHA-256 |
|---|---|---|
| Original TE R246 (`op1_246.op1`, lecture seule) | 13 039 128 o | `c5315218f825f143b415ca554516541898abee843d3a236df0b54c04e1fb13a9` |
| **Custom produite** (`op1_246_custom-repacked.op1`) | 12 411 779 o | `286c4124d6ffd37de182c8758f84f71ea45ffbc3e01e0b98e375e5f80417d0c4` |

La différence de taille (−627 Ko) vient de la recompression LZMA par op1repacker — attendue, non significative.

**Les 4 contrôles, tous passés :**

1. **Intégrité CRC + extraction reproductible** — re-unpack complet de l'image custom par
   `op1repacker unpack` : aucune erreur (`Done!`), le CRC32 d'en-tête et la couche LZMA sont valides.
   Structure résultante conforme : `OP1_vdk.ldr`, `content/`, `plugins/`, `te-boot.ldr`.
2. **Diff de contenu vs arborescence originale** (`diff` des listes de fichiers `op1_246/` vs
   `op1_246_custom-repacked/`) — **0 fichier supprimé, 0 fichier inattendu**, exactement les ajouts
   attendus du build op1r :
   - 5 écrans SVG : `fxgeneric.svg`, `iterlab.svg`, `quadlfo.svg`, `sysmon.svg`, `usbout.svg`
   - 24 plugins `.op1p` (aether, arc, brassophone, chord, clouds, dddd, dots, drumplus, euclid,
     fmbell, granular, grids, in_a_room, mixtur, nostromo, oracle, palme, peal, randomiser,
     reverb, samplerplus, steve, supersaw, tape)
3. **Code embarqué** — `OP1_vdk.ldr` : 2 171 624 o (original, 9 nov 2022) → 2 314 644 o
   (**+142 020 octets** : dispatcher + SDK op1r + notre `midi/mm.c` compilés par `bfin-elf-gcc`).
4. **Zones chiffrées intactes** — aucune modification de `hook.S` / `l1_patches.json` (les blocs
   XTEA chiffrés ne sont pas retouchés ; seuls les hooks précalculés existants d'op1r sont appliqués).

**Limites de la validation (à lire avant tout flash)**
- La validation porte sur la **structure** de l'image, pas sur l'exécution : le comportement
  matériel (écran boot `op1r <n>`, MIDI, tape) reste à vérifier sur l'appareil ou émulateur.
- Un test sur machine réelle ne doit se faire qu'après **sauvegarde complète du disque** et avec
  `op1_246.op1` original à portée de main pour reflash (voir `install-recuperation.md`).

## 4. Restant (prochaines étapes)
1. ✅ ~~Doc installation/récupération~~ → **`install-recuperation.md`** (à la racine)
2. ✅ ~~Noyau portable v2~~ → **`mm2/`** — tests verts
3. ✅ ~~Portage Hundekraft 3.0.3~~ → **`hundekraft-plugin/op1midi.knl`** packé (voir §6)
4. Retirer `op1_246.op1` et `hundekraft-3.0.3.op1` du dépôt git + `.gitignore` (règle 3 AGENTS.md) — en attente feu vert
5. Test matériel : installation du `.knl` via le Workshop (Your plugins → Update OP-1) — **aucune manipulation appareil sans feu vert explicite**
6. Optionnel : commande `push` (SysEx devupd) ; service core pour INSTRUMENT (voir §6)

## 6. Portage Hundekraft 3.0.3 — réalisé (27 octobre 2026)

**Analyses**
- Image `hundekraft-3.0.3.op1` (SHA-256 `39c3d475…`) dépackée : `OP1_vdk.ldr` (2 330 480 o),
  54 modules `.knl` dans `content/` (dont `markers`, `tapes`, `midicc`, `midiseq`, `countin`,
  `punch`, `settings`…), dossiers de build `op1r_stage/` (trace de lignée) et `kennel_recovery/`.
- Bundle source officiel téléchargé et vérifié (`hundekraft-3.0.3-source.tar.gz`,
  SHA-256 `2f745e38…`). **Lignée op1r prouvée** : `ext/kennel/docs/op1r-src-README.md`,
  `markers_feature.c` incluant littéralement `tape/markers.c`, même structure `synth_sdk/`.
- Architecture « kennel » : features (`kind: "feature"`, `op1_feature_t`) avec événements
  (`midi_raw` en contexte timer : **CC et Program Change** ; `input` sur le thread KeyHandler ;
  `overlay` sur le thread graphique), services exportés (`kn_exports.c`) : `op1_tape_switch`
  (4 tapes), `op1_tape_pos_get/set`, `op1_seq_press_play`, `op1_setting_get/set`,
  `op1_switch` (rangées ON/OFF de la page settings T3), `op1_midi_take_cc`, `op1_toast`,
  et données partagées `tm_pos[]/tm_n/pu_mode` (les marqueurs du plugin `markers` sont lisibles).
- **Un plugin se compile seul** (`tools/kennel/pack.py` → un `.knl`) et s'installe via le
  Workshop sur le core 3.0.3 existant — **aucun rebuild firmware requis**.

**Plugin `op1midi` livré** (`hundekraft-plugin/`)
- `op1midi.c` : glue kennel — `midi_raw` → `mm2_cc/mm2_pc` (CC B0 + PC C0), file d'actions
  timer→main loop, `input` (ouverture par **appui blanc en mode clock**, éditeur modal avec
  filtrage des releases de touches tenues avant ouverture), `overlay` (rendu : curseur rouge,
  autres blancs, valeur en édition en vert), `op1_switch("op1midi", "MIDI MAP")` sur T3,
  `op1_midi_take_cc` (diff appliqué au service), saut de marqueurs porté de `tm_turn`
  (wrap, tolérance 10 ms) via les données partagées.
- `plugin.json` : kind feature, claims `["key:clock:white"]` (aucun autre consommateur du push
  blanc en mode clock, vérifié dans sysmon/usbout/scalepg/settings).
- **`op1midi.knl` packé avec succès** : 12 028 octets, .text 5092 / .rodata 2060 / .data 8 /
  .bss 212, 369 relocations, 13 imports **tous présents dans kn_exports.c**, aucun warning packer.
  **Compilé avec bfin-elf-gcc 13.3.0** (toolchain dédiée `bfin-elf-toolchain-13`, Dockerfile
  `-j2` ; un premier pack en 12.3.0 donnait les mêmes 13 imports — version alignée sur le build
  officiel Hundekraft 3.0 après audit de compatibilité, §7).
- Build reproductible : `hundekraft-plugin/build-knl.sh` (injecte le plugin dans le bundle,
  packe sous Docker `bfin-elf-toolchain-13`, sort `op1midi.knl`).
- Licences : en-têtes GPL-3.0-or-later + permission OP-1 sur tous les fichiers du plugin et de
  `mm2/`, `hundekraft-plugin/LICENSE` (GPLv3 du bundle kennel) + `hundekraft-plugin/NOTICE`
  (lineage op1r/kennel, core requis 3.0.3 / API 1, control map, claims, build, install, changelog v1).
  Le LICENSE MIT racine couvre docs et scripts.

**Limites connues du plugin v1**
- INSTRUMENT SELECT : aucun service core de sélection d'instrument/preset à ce jour → toast
  unique « NO CORE SERVICE » à la première utilisation (nécessiterait une évolution du core).
- La rangée settings prendra la couleur **verte** (premier switch enregistré ; l'ordre
  vert/blanc/rouge suit l'inscription, nous sommes seuls à en enregistrer un).
- PLAY@MARKER bloqué pendant un count-in armé (état transport 6) — comportement voulu.
- Non testé sur matériel ni émulateur à ce stade (imports vérifiés statiquement uniquement).

## 5. Rappel distribution (choix : A — build complet)
- Première installation : image custom requise (parties chiffrées XTEA irrégénérables, hooks à adresses fixes)
- Mises à jour ultérieures possibles **à distance par USB MIDI** via `devupd` (SysEx `F0 00 20 76 7D`, flasher NAND gardé)

## 7. Audit de compatibilité vs PLUGIN_GUIDE.md (27 octobre 2026)

Audit point par point du plugin `op1midi.knl` contre `PLUGIN_GUIDE.md` du bundle 3.0.3 :

| Exigence | Statut |
|---|---|
| ID 1–15 minuscules/chiffres/`_`/`-` (`op1midi`) | ✅ |
| Manifest feature complet (kind/symbol/loadable/sources/claims/author/version) | ✅ |
| Descripteur `op1_feature_t` const, init désignée, membres inutilisés à zéro | ✅ |
| Claims dans plugin.json **et** descripteur, grammaire `key:<mode>:<combo>` | ✅ |
| Validité du claim côté Workshop (`plugin-plan.js claimText` : « WHITE IN CLOCK MODE », pas de whitelist, aucun autre claimant) | ✅ |
| Imports uniquement dans `kn_exports.c` (13/13) | ✅ |
| Core function as callback → wrappers locaux uniquement | ✅ |
| Descripteur en données initialisées (rodata) | ✅ |
| Taille ≤ 512 KiB | ✅ 11,8 Ko |
| `midi_raw` borné, travail délégué (file SPSC → `OP1_EV_SERVICE`) | ✅ |
| Réglages sous l'ID du plugin, jamais thread audio | ✅ |
| Overlay court (4 lignes + titre) | ✅ |
| Firmware TE / clés absents du bundle plugin | ✅ |
| Hardware non testé = déclaré non testé | ✅ |
| GCC 13.3.0 (build officiel 3.0) | ✅ (repacké en 13.3, voir §6) |
| Licences GPL-3.0-or-later + permission OP-1 par fichier, NOTICE dédié | ✅ (`hundekraft-plugin/LICENSE` = GPLv3 du bundle, `NOTICE` : lineage, core requis, control map, claims, build, install, changelog) |

Limites documentées : INSTRUMENT SELECT sans service core (toast) ; test matériel restant (aucun
émulateur dans le bundle ; imports vérifiés statiquement).

## 8. Test matériel v1 + fixes v2 (28 octobre 2026)

**Résultats du test sur l'OP-1 (Hundekraft 3.0.3)** :
- ✅ **Le plugin charge et tourne sans crash** — l'appareil reste stable.
- ✅ Le GUI de l'éditeur fonctionne comme convenu (navigation, édition).
- ❌ Bug 1 : dans la page settings (Shift+Help, T3), « MIDI MAP » prenait la place de « MIDI SEQ »
  (rangée verte ligne 2), MIDI SEQ repoussé en ligne 3.
- ❌ Bug 2 : l'éditeur s'affichait en surimpression sur la page restée visible → illisible.
- ❌ Bug 3 : impossible de quitter l'éditeur (Shift+Help ou touches de mode avalées par le mode
  modal).

**Analyse source + fixes (v2, packés)** :
- Bug 1 : `midiseq` enregistre son switch à `OP1_EV_BOOT_DONE` (midiseq.c:265) et les built-ins
  s'enregistrent avant les fichiers plugins (`kn_features.c` BUILTIN). Notre `op1_switch` est donc
  passé de `OP1_EV_REGISTER` à `OP1_EV_BOOT_DONE` → livraison après `midiseq` → **2ᵉ switch =
  rangée blanche ligne 3**, MIDI SEQ reste vert ligne 2.
- Bug 2 : l'overlay applique désormais le motif du plugin kennel `randomiser` (rn_draw.c) : voile
  plein écran (RGB565 >> 1) + **panneau noir opaque** (lignes 2 px) + bordure blanche avant le
  texte.
- Bug 3 : `mm2_edit_key` ferme désormais l'éditeur (avec sauvegarde) sur Help (Shift ou non) et sur
  les touches de mode / T1–T4 / transport — sans Shift la touche **agit ensuite normalement**
  (passthrough, motif `op1_text_entry`), avec Shift elle est consommée ; la glue propage le retour
  et n'avale plus aveuglément.

**`op1midi.knl` v2** : 10 304 octets, .text 4676 / .rodata 2000 / .bss 212, 265 relocations,
13 imports identiques à la v1, aucun warning packer (bfin-elf-gcc 13.3.0). `mm2_test` étendu
(sorties Help seul / mode / T / transport, sauvegarde systématique, passthrough) → **vert**.

Reste : re-test matériel de la v2 (rangée settings, lisibilité, sorties), README.md côté
utilisateur, autres retours d'interface (à venir).

## 9. v3 — nouveau modèle de mapping (28 octobre 2026)

Spec utilisateur (`midi-spec.md` réécrit + `gui-layout.md`), décisions validées en plan :

- **Sources** : NOTE (note-on vel > 0 ; note-off mappée avalée ; les notes mappées sont aussi
  **avalées dans le pipeline de notes** via `op1_note_source()` — le synthé ne les joue pas),
  CC (front ≥ 64), PC (chaque message). Canal global `mmch` (0 off / 1–16 / 17 ALL).
- **Destinations** : SOUND 1–8 (**aucun service core** de sélection de slot — vérifié dans
  `op1_feature_sound.h`/`kn_exports.c` → toast une fois par boot), TAPE 1–4 (`op1_tape_switch` :
  le core arrête la lecture lui-même ; erreurs rec/disk ignorées), MUTE 1–4 (toggle byte R246
  `0xFF8010A0+t` + màj DSP `0x01967ED0` — adresses stock, absentes du SDK kennel, documentées),
  MARKER global 1–124 (formule tape/locale ; ⚠️ **en 3.0.3 l'ensemble de marqueurs est partagé
  entre les 4 tapes** — un seul row DB `op1rmarkers`, pas de reload au switch : vérifié dans
  `markers.c` — la formule résout mécaniquement, sémantique à valider sur hardware), MRK+/MRK-
  (conservés, décision utilisateur).
- **Fade** (Shift+rotation blanche dans l'éditeur, `mmfade` OFF/1s/2s/8s/32s) : en lecture, saut
  de marker → descente des 4 volumes de pistes tape (mots live R246 `0xFF801928`/gammes
  `0xFF8018CC`) pilotée par `tick` (thread audio) → saut au silence sur la main loop (cross-tape :
  locate à `OP1_EV_TAPE_SWITCH` after) → remontée → restauration exacte ; STOP pendant fade =
  saut annulé + restauration.
- **MIDI learn** : Shift+clic bleu dans l'éditeur → `LRN` en en-tête → le prochain NOTE/CC/PC
  écrit la source de la ligne sous le curseur (destination par défaut TAPE si ligne vide) ; le CC
  appris est immédiatement « taken ».
- **Éditeur v3** : édition live (plus de mode validation), colonnes aux couleurs des encodeurs
  (n° de slot en rouge, source bleu, destination vert, n° blanc), curseur tout rouge, en-tête
  `MIDI MAP pX/4 CH:x F:x (LRN)`.
- **Persistance versionnée** : `mmv`=3 (ancien format → lignes remises à zéro), lignes re-packées
  4 champs (20 bits), `mmch` réexposé (Shift+rotation verte, décision utilisateur).

**`op1midi.knl` v3** : 15 664 octets, .text 7240, 481 relocations, **17 imports tous présents**
(`op1_note_source/on/off` et `op1_tape_current` ajoutés), 0 warning (GCC 13.3.0). `mm2_test` v3
réécrit (codec, migration mmv, dispatch 3 sources, gardes par état, formule marker, plages
destination, canal/fade, learn, édition live, sorties) → **vert** (sanitizers + plain).

Reste : test matériel v3 (fade audible, learn, mute, marker global), SOUND sans service core
(évolution à demander au kennel si l'usage le veut), README.md utilisateur.

## 10. v3.1 — markers plats + SOUND fonctionnel (28 octobre 2026)

Décisions utilisateur (après la découverte de l'ensemble de marqueurs partagé) :

- **MARKER 1–32 (plat)** : `MM2_MRK_MAX` 124 → 32 ; le n° = le n-ième marqueur **posé**, sur la
  tape courante, **sans switch de tape** (la destination TAPE existe pour changer de tape ;
  MRK+/MRK- naviguent). Garde renforcée au dispatch : `dnum > MM2_MRK_MAX || dnum > tm_n` →
  ignoré (défense en profondeur même si la config écrite excède la plage). Suppression de la
  signature `act_marker(tape, local)` → `act_marker(local)`, et du chemin cross-tape
  (`OP1_EV_TAPE_SWITCH` handler, `sw_*`, `fading_cross`) — le fade reste : descente, saut
  (locate+play), remontée, restauration.
- **SOUND 1–8 fonctionnel** (idée utilisateur : « un midi learn avec les boutons de l'OP-1 ») :
  - **Capture** : Shift+clic bleu arme le learn ; si la destination du slot courant est SND, le
    prochain appui de touche sound (T1–T4 = slots 1–4, Shift+T1–T4 = slots 5–8 ; codes 0x06–0x09
    vus par le hook input avant le stock) est **capturé et avalé** (rien ne bouge pendant
    l'apprentissage) → `mm2_edit_learn_feed_key(slot)` (nouvelle API testée). Pour toute autre
    destination, le learn capture NOTE/CC/PC comme en v3 (midi_raw ignoré si learn SND en cours).
  - **Replay par injection** : `snd_inject(slot)` pousse dans la **file de touches stock** (R246
    `0x01DD5044`, pointeurs `0x01DD5244/48`, byte busy `0x01DD524D`, réveil `0x019C2F04` — le
    mécanisme même du chemin MIDI→touches du firmware, pattern prouvé par op1r `midi/mx.c`) :
    `[SYNTH press/release]` (idempotent) + `[Shift press]` si slot 5–8 + `[T_n press/release]` +
    `[Shift release]`. Pas de flag anti-réentrance nécessaire : les touches injectées sont des
    touches ordinaires que notre hook laisse passer (éditeur fermé).
- **`op1midi.knl` v3.1** : 15 436 octets, .text 7344, 457 relocations, **17 imports tous
  présents** (`op1_toast` n'est plus importé), 0 warning (GCC 13.3.0). `mm2_test` mis à jour
  (markers plats, borne 32, plage prime sur le compte, learn touche SND) → **vert**.

Reste : test matériel v3.1 (learn touche + injection SOUND, fade, marker plat), README.md
utilisateur, autres retours d'interface.

## 11. v4 — nouvelle carte de contrôles (28 octobre 2026)

Retours d'interface de l'utilisateur → remaniement des rôles (plus d'affichages permanents) :

| Contrôle | Simple | Shift |
|---|---|---|
| Bleu | clic = type (NOTE/CC/PC) · rotation = n° source | rotation = **canal global** ; clic = **learn ON/OFF** |
| Vert | rotation = **destination + n° en liste plate** (OFF, SND 1-8, TAPE 1-4, MUTE 1-4, MRK 1-32, M+, M-) | — |
| Blanc | rotation = **fade** (OFF/1s/2s/8s/32s) | — |
| Rouge | rotation = **curseur** (16 slots, page suit) | clic = **supprimer la ligne** |

- **Affichages temporaires** : Shift tenu → la colonne type du curseur affiche `CH n/ALL/OFF`
  (le suivi du Shift se fait par les événements input, `in->shift` à chaque événement) ; learn
  armé → `LRN` à la place de la source tant que rien n'est capturé ; changement de fade →
  `op1_toast("FADE xS")` (plus d'en-tête permanent : l'en-tête se réduit à `MIDI MAP pX/4`).
- **Suppression** : `mm2_edit_delete()` (nouvelle API) — ligne → OFF, champs à zéro ; les takes
  CC sont réappliqués après toute touche en modal (couvre la suppression).
- **Incidents de build détectés et corrigés** : un import résiduel `_ch_name` (détail core non
  exporté — le loader l'aurait refusé) repéré dans la liste des imports du pack et éliminé ;
  en-tête de l'overlay réécrit proprement.
- **`op1midi.knl` v4** : 16 084 octets, .text 7656, 479 relocations, **17 imports tous présents
  dans kn_exports.c**, 0 warning (GCC 13.3.0). `mm2_test` v4 (liste plate, fade sans Shift,
  learn toggle, delete, sorties) → **vert** ; simulateur : **33 self-tests verts** + rendu mis
  en page (en-tête allégé, affichages contextuels).

Reste : test matériel v4, README utilisateur, itérations UI suivantes.

## 12. v5 — canal et fade par slot, surbrillance (28 octobre 2026)

Ajustements demandés après relecture du simulateur :

- **Canal MIDI PAR SLOT** : chaque ligne porte son canal (0 = non réglé → traité ALL ; 1..16 ;
  ALL). Édition : **Shift + rotation bleu** sur le slot courant (cycle 1..16 → ALL). Le canal
  global (`mmch`) disparaît du dispatch et du modèle. Format `mmv` = 4 (ancien → reset).
- **Destination + numéro fusionnés** : la colonne verte affiche `MRK 11`, `TAPE 2`, `SND 3`…
  (le numéro était auparavant dans une colonne séparée) ; la colonne blanche libérée affiche le
  **canal de chaque ligne** (`ALL` / `CH n`).
- **Fade PAR SLOT** : `row.fade` (OFF/1s/2s/8s/32s), édité par le blanc sur le slot courant ;
  le moteur de fade utilise la valeur de la ligne déclenchée et **ne s'applique pour l'instant
  qu'aux destinations MUTE / MRK / M+ / M-** (SND/TAPE ignorent le fade).
- **Surbrillance du curseur** : chaque cellule de la ligne sélectionnée prend le fond de sa
  couleur de colonne (violet/bleu/vert/blanc) avec la typo en **noir**.
- **Chiffres de slot en violet** (#9B5DFF).
- **`op1midi.knl` v5** : 15 600 octets, .text 7720, 435 relocations, **17 imports tous
  présents**, 0 warning. `mm2_test` v5 (codec ch/fade, canal par ligne, fade par ligne vers les
  hooks, migration `mmv`=4, learn préservant le canal, delete remettant tout à zéro) → **vert** ;
  simulateur : **41 self-tests verts**.

Reste : test matériel v5, README utilisateur.

## 14. v6 — continuité couleurs/fonctions (28 octobre 2026)

Décalage demandé par l'utilisateur (continuité encodeurs ↔ colonnes) :

| Contrôle | Simple | Shift |
|---|---|---|
| **Bleu** | rotation = curseur (16 slots, page suit) | — |
| **Vert** | clic = type (NOTE/CC/PC) · rotation = n° source | rotation = **canal de la ligne** · clic = **learn ON/OFF** |
| **Blanc** | rotation = **destination + n°** (liste plate) | clic = **supprimer la ligne** |
| **Rouge** | rotation = **fade de la ligne** | — |

Colonnes de l'écran : bleu (slot), vert (source), blanc (destination), rouge (fade) — curseur en
surbrillance (fond couleur de colonne + typo noire). La numérotation des slots passe donc en
bleu (la préférence rouge antérieure est supplantée par la continuité — revenir = 1 ligne).

**`op1midi.knl` v6** : 15 744 octets, 17 imports, 0 warning. `mm2_test` v6 vert (rôles décalés,
delete déplacé sur blanc) ; simulateur : **46 self-tests verts** + smoke rendu.

Reste : choix de police (propositions faites), test matériel, README utilisateur.

## 16. v6.2 — police native + key learn + diagnostics sim (28 octobre 2026)

- **Police native R246** dans l'overlay (choix utilisateur option 1): objets Text du firmware
  (`op1_text_ctor/set/draw`, OP1_TEXT_SIZE 0xA4, pattern `screens/sysmon.c`), construits une fois
  au premier rendu (thread graphique), dessinés avec `ident` — **minuscules supportées**
  ("mrk 11", "2s", "ch 5", "all" — la demande de lisibilité des secondes). 34 objets (titre,
  page, 4 lignes × 4 cellules × {colorée, noire}) ≈ 5,6 Ko de BSS. Simulateur: approximation
  par `fillText` (fonte navigateur) — le rendu exact reste à valider sur hardware.
- **Learn en deux états** (`MM2_L_NONE/MIDI/KEY`): Shift+clic **vert** arme/bascule le learn
  MIDI; **clic vert seul** le désarme (plus besoin de Shift pour sortir). Shift+clic **blanc**
  arme le **key learn** quand la destination du slot est SND; **clic blanc seul** le désarme.
  Pendant le key learn: **toutes les touches pressées sont tracées** (toast), les touches sound
  (T1–T4 / Shift+T1–T4) règlent le slot, le reste est avalé sans action.
- **Shift+clic rouge = supprimer la ligne** (retour; Shift+clic blanc est désormais l'armement
  du key learn).
- **Diagnostics simulateur** (la cause du « pas de fade » de l'utilisateur): les touches
  transport (PLAY/STOP/REC) pilotent maintenant l'état tape simulé; les rejets de garde
  nomment la raison exacte (mode, punch, état tape, nombre de marqueurs).
- **`op1midi.knl` v6.2** : 16 940 octets, .bss 328+Text, 17 imports, 0 warning. Tests: mm2_test
  vert (états de learn), simulateur **58 self-tests verts** + smoke rendu.

Reste : test matériel (rendu de la police native, key learn), README utilisateur.

## 21. v6.7 — learn visibles, key learn expliqué, pictos attaqués (28 octobre 2026)

v6.6 confirmée sur hardware: Shift fonctionne (canal + midi learn accessibles). Restaient:
key learn muet, pictos coin haut-gauche, toasts de diag à trier (demandes user).

1. **Key learn « rien »** : ce n'est pas un bug d'armement — `mm2_edit_learn_feed` met
   `dst = MM2_D_TAPE` sur une ligne vide, et Shift+blanc **refuse silencieusement** hors
   SND (`mm2_edit_key`, garde `dst == MM2_D_SND`). Le user avait fait un midi learn (dst
   TAPE) puis testé le key learn sur la même ligne. Fix: toast **« key: snd requis »**.
2. **Toasts de learn** (demande user: garder pour midi ET key): armement « midi learn » /
   « key learn » / « learn off », captures « cc N » / « note N » / « pc N » / « touche N ».
   Les toasts de diag (`k…`, `shift s…`) sont retirés.
3. **Pictos** : théorie = atlas partagé glyphes+icônes du firmware qui transparaît quand les
   objets Text R246 rasterisent de nouveaux glyphes (34 objets, chaînes changeant à chaque
   touche depuis les diag v5/v6). L'éditeur passe **entièrement** en police vectorielle
   kennel (`sf_text`, majuscules): 0 objet Text firmware (bss 6592 → 336 o). Si un picto
   survit → hypothèse fausse, on passera au voile/panneau.
- **`op1midi.knl` v6.7** : 17 460 o (text 8400, rodata 2396), 17 imports, 0 warning.
  mm2_test ok.

Reste: test hardware v6.7 (key learn sur ligne SND, pictos), README utilisateur.

## 22. v6.8 — learn blindé, clear slot, notes en toutes lettres (29 octobre 2026)

v6.7 confirmée: **pictos disparus** (théorie de l'atlas glyphes+icônes validée), midi learn
et key learn fonctionnels. Trois demandes user, trois changements:

1. **Learn blindé**: learn armé (midi ou touche) → **aucune touche ne quitte** l'éditeur
   (`mm2_edit_key`: exit_key avalées si `learn`). Échappatoires: Help valide+ferme toujours,
   clic blanc seul sort du learn touche, Shift+vert bascule le learn MIDI. Avant: une touche
   de mode/transport pendant la fenêtre de learn fermait tout.
2. **Clic bleu**: Shift+clic bleu = **vider la ligne** (comme Shift+rouge) — le binding
   n'était tout simplement pas implémenté (le clic bleu était avalé muet). Clic bleu seul =
   **mode d'affichage des notes**, global, persisté `'mmnd'`: numéro (60) → anglais (C#4) →
   solfège (DO#4). Toast avec la note du curseur en échantillon.
3. **Minuscules**: la police vectorielle kennel n'a que des majuscules → **petites
   capitales** (scale 1, ligne de base alignée) pour les unités: canal Shift = « ch n »,
   fade = « 1s/2s/8s/32s ». Toasts de fade en vraies minuscules (police firmware):
   « fade 1s ». En-tête v7 corrigé (rôles des encodeurs étaient périmés).
- **`op1midi.knl` v6.8** : 18 764 o (text 8936, rodata 2576, bss 344), 17 imports, 0 warning.
  mm2_test ok (nouveaux tests: noms de notes, mmnd persisté, sorties avalées pendant learn,
  Shift+bleu vide la ligne).

Reste: test hardware v6.8 (notes anglaises/solfège, clear bleu, learn blindé), README utilisateur.

## 23. v6.9 — régime toasts, LRN à sa colonne (29 octobre 2026)

v6.8 confirmée. Retours user: **les toasts laissent eux aussi des résidus d'atlas** (police
firmware), certains débordent, beaucoup sont inutiles.

1. **Toasts de capture uniquement**: cc/note/pc N (midi), « touche N » (key), « key: snd
   requis » (refus). Retirés: armement (« midi learn »/« key learn »/« learn off »),
   « notes: … », toasts de fade.
2. **LRN à sa colonne**: learn MIDI → LRN en vert (source, inchangé); learn **touche** →
   LRN en **blanc** (destination — c'est la destination qu'il édite; l'afficher en vert
   portait à confusion).
3. L'état armé est désormais **persistant à l'écran** (colonnes), pas volatil (toast).
- **`op1midi.knl` v6.9** : 18 400 o, 17 imports, 0 warning. mm2_test ok.

Reste: test hardware v6.9; v7.0 = destination COMBO (spé proposée, §23bis à valider).

## 24. v7.0 — destination COMBO (29 octobre 2026)

Design validé par le user: **un seul COMBO dans la liste, en premier** (juste après OFF —
première destination atteinte en tournant le blanc depuis une ligne vide); chaque ligne qui
le choisit porte sa propre séquence — pas de banque de combos, pas de défilement de 16
combos. 3 touches max ± Shift.

1. **Liste plate**: 0 OFF, 1 COMBO, 2-9 SND, 10-13 TAPE, 14-17 MUTE, 18-49 MRK, 50 M+, 51 M-.
   `dst` = 7 (nouvel enum, `MM2_D_MAX` 7; 3 bits du codec suffisent).
2. **Stockage**: `combos[16][3]`, 1 octet par entrée (bit7 = Shift, bits 0-6 = code), clés
   `'mmk0'..'mmk15'` (1 int32/ligne). Format `'mmv'` inchangé — les vieilles configs n'ont
   pas de 'mmk' → combos vides par défaut. `mm2_combo_get/set`, hook `mm2_act_combo`.
3. **Learn combo**: Shift+clic blanc sur une ligne dst=COMBO arme `MM2_L_COMBO` (le type de
   learn suit la destination). Chaque touche s'ajoute (Help, Shift lui-même et les 4 clics
   encodeurs rejetés); **clic blanc = valider**, **3e touche = validation auto**. Staging
   visible en direct dans la colonne blanche (hex petites capitales, « S06 21 » = Shift+T1,
   play); validé → « COMBO n » (dnum = nombre, pur affichage). Toute transition de learn
   vide le staging.
4. **Rejeu**: le trigger met la ligne en file; le drain construit la séquence ([Shift down]
   down, up [Shift up] par entrée) et l'injecte comme touches synthétiques (même mécanique
   que la sélection de sound key). Pas de garde d'état (comme SND).
5. **Vider la ligne** (Shift+bleu/rouge) efface aussi le combo. Learn armé: le MIDI passe
   sans capture ni dispatch (KEY et COMBO).
- **`op1midi.knl` v7.0** : 21 128 o (text 10316), bss 400, 17 imports, 0 warning.
  mm2_test ok — nouveaux tests: liste plate COMBO premier + wrap, plage dnum 0..3, rejets
  d'apprentissage, staging/commit/auto-validation, rejeu (hook reçoit la séquence), Shift+bleu
  vide le combo, persistance 'mmk0'. Codec: dst 7 désormais valide (test ajusté).

Reste: test hardware v7.0 (combo end-to-end), README utilisateur, « 8 sons en dur » quand
l'utilisateur aura fixé les touches.

## 25. v7.1 — notes internes, SND comme COMBO, colonne verte (29 octobre 2026)

Retours hardware v7.0: le combo note->touches n'a rien déclenché; le user demande l'affichage
des touches apprises; la colonne verte affichait le canal.

1. **Le midimap n'écoutait que le MIDI externe** — diagnostic du user confirmé: `midi_raw`
   (USB) déclenchait, mais les notes du **clavier interne** (`OP1_SRC_KEYS`) étaient avalées
   par le pipeline **sans action**. Fix: `pip_note_on` déclenche `mm2_note(...)` pour KEYS
   (et avale), MIDI reste « action par midi_raw + avale pipeline », PLAYER/FEATURE passent.
   `pip_note_off` avale l'off des notes avalées (KEYS inclus). `pq_push` passe sous cli/sti
   (deux producteurs: timer + pipeline).
2. **SND comme COMBO**: le learn stocke la touche **brute** (bit7 = Shift) dans la ligne
   (même stockage que les combos), le blanc affiche « SND 20 » / « SND S20 » (hex), le rejeu
   injecte [SYNTH, touche] (pattern op1r mx.c). Plus de dérivation de slot à partir de codes
   supposés: **le user apprend les 8 sound keys et lit les codes à l'écran** pour me les
   donner (ils seront ensuite en dur). 1re touche = commit immédiat. Fallback SYNTH + T_{dnum}
   sans touche apprise. `combo_commit` ne touche plus dnum hors COMBO.
   Indices codes: kb_keys kennel (kn_midi.c:222) = clavier piano {39,13,40,14,41,15,42,28,
   32,29,22,30,31,23,48,24,49,25,50,51,26,52,27,53}; 0x20 = 32 déc = touche piano — le « key
   20 » du user est une touche du clavier. midiseq kennel suppose T1..T4 = 0x06..0x09.
3. **Colonne verte = toujours la source**: étiquette petites capitales (note/cc/pc) + valeur
   pleine taille; « note » toujours écrit; canal plus affiché (édition Shift+vert intacte).
- **`op1midi.knl` v7.1** : 21 552 o, bss 400, 17 imports, 0 warning. mm2_test ok (learn par
  code brut + rejets, SND rejeu via hook combo + fallback slot).

Reste: le user relève les codes des 8 sound keys (à mettre en dur), test interne/externe.

## 26. v7.2 — touches sound en dur (29 octobre 2026)

Le user a relevé les 8 sound keys avec l'affichage v7.1: **2D 2E 2F 10 11 12 13 14** (hex,
sons 1..8). Mise en dur:

1. `sndkeys[8]` dans l'intégration; `snd_inject(slot)` = SYNTH down/up + `sndkeys[slot-1]`
   down/up (plus de Shift+T_n dérivé de codes supposés — l'ancienne table 0x06..0x09 était
   fausse: ce sont les touches T/piste, pas les sound keys).
2. **Learn touche SND retiré**: Shift+blanc n'arme plus que le learn **combo** (destination
   COMBO requise); le toast « key: snd requis » part avec lui. `mm2_edit_learn_feed_key_code`
   supprimée du noyau; les touches apprises résiduelles sont ignorées pour SND.
3. mm2_test: SND = act_snd(slot) uniquement (résidu de learn ignoré), learn combo pour le
   bloc « aucune touche ne quitte ».
- **`op1midi.knl` v7.2** : 20 368 o, bss 400, 17 imports, 0 warning. mm2_test ok.

Question ouverte (user): les marqueurs fonctionnent « en momentary et pas en toggle » —
clarifier le comportement attendu (A/B: 2e déclenchement = retour à la position précédente?).

## 27. v7.3 — le saut de marker continue la lecture (29 octobre 2026)

User: « les Mrk fonctionnent en momentary et pas en toggle », puis clarification: il veut
saut + **continuation de la lecture**. Cause trouvée dans le kennel: `op1_seq_press_play`
met `play_pending` et `kn_seq_after_key` dispatche la touche **PLAY (0x22)** — un **toggle**.
Donc un saut déclenché **en lecture** arrêtait la bande juste après le seek.

1. `do_jump(pos, playing)`: presser PLAY seulement si `raw_state != 10`; en lecture, la tête
   se repositionne et continue.
2. Chemin fade (saut au silence): `do_jump(pos, 1)` — le transport roule déjà, ne pas
   re-presser PLAY (même bug latent).
3. Arrêté: saut + démarrage (inchangé).
- **`op1midi.knl` v7.3** : 0 warning. mm2_test ok (noyau inchangé).








## 20. v6.6 — fix Shift: l'éditeur modal avalait l'appui Shift (28 octobre 2026)

Données du user (ligne diag v6.5): Shift seul → `26+26-26+26-26+26`; Shift+clic vert →
`26+26-26+81v3C+3C-` (le code Shift réel est bien 0x26, la livraison fonctionne, `81v` =
légère rotation verte accidentelle). En croisant avec le core: dans key_event_hook, notre
input() passe AVANT KEY_DISPATCH (0x019B6C74) — et notre éditeur modal avalait TOUT,
**y compris l'appui Shift (0x26)** → le stock ne latchait jamais son octet Shift
(0x01DD7F78) → tous les combos arrivaient avec shift=0. midiseq, non-modal, laisse passer
0x26 → son Shift marche. Explicatif pour v6.2/v6.3/v6.4 et pour « in->shift ne marche pas ».

- **Fix**: quand l'éditeur est ouvert, code 0x26 (press et release) → `return 0` (traverse
  vers le stock). 0x26 n'est pas dans exit_key(); Shift seul ne fait rien dans le stock.
- **Toasts temporaires** (demande user: popups plutôt qu'une ligne tronquée): `shift s1/s0`
  à l'appui/relâché de Shift, `k<hex><s>` (s = octet au moment de l'événement) à chaque
  appui pendant l'éditeur. Retrait prévu en v6.7.
- Ligne diag v6.5 retirée (tronquée en bas d'écran). knl 16 284 o, 0 warning. mm2_test ok.

Reste: test hardware v6.6 (combos Shift), pictos coin haut-gauche (§21), README utilisateur.


## 19. v6.5 — diagnostic Shift sur l'appareil (28 octobre 2026)

v6.4 confirmée sur hardware (rangée MIDI MAP à la bonne place) mais le Shift reste mort.
Analyse statique du core 3.0.3 : le hook `.hook_keyev` (0x019C2D3E, KeyHandler::process_queue)
est sur le dispatch commun, AVANT le routage stock on_key/on_key_shift (KEY_DISPATCH
0x019B6C74, (code, pressed)) → les touches Shiftées atteignent kn_features_input, et le
champ in->shift livré EST l'octet 0x01DD7F78 (kn_core.c:138) — la même valeur que notre
lecture directe. midiseq (feature) utilise in->shift (Shift: mute). Tout devrait marcher.

Donc: débogage empirique. L'éditeur affiche en bas du panneau une ligne de diagnostic:
- **S** = octet 0x01DD7F78 lu au rendu (1 = shift vu)
- **E** = champ shift du dernier événement livré à input()
- **A** = octet 0x019D7C6C (candidat alternatif kn_save.c, en hex)
- **k** = 6 derniers événements: code hex + état (+ appui/haut, - relâché/bas, v encodeur
  bas; bit 0x80 = encodeur) — montre si l'appui Shift (son vrai code) arrive et ce qui suit.
Le toast de boot v6.4 est retiré. Ligne diag à retirer en v6.6 une fois la source identifiée.
- **`op1midi.knl` v6.5** : 16 916 o. mm2_test ok (core inchangé).

Reste: rapport du user sur la ligne diag, puis fix du shift, README utilisateur.


## 18. v6.4 — le vrai fix de la rangée + toast de version (28 octobre 2026)

L'utilisateur confirme : aucune différence visible v6.2 → v6.3. Enquête : le binaire AVAIT
changé (hash knl différent, adresse Shift 0x01DD7F78 présente en demi-mots 01DD/7F78),
mais le fix de la rangée était **inopérant** : l'appel `op1_switch` restait dans la branche
BOOT_DONE (op1midi.c:570) et `sw_pending` n'était jamais mis à 1 → v6.3 = v6.2 de
l'extérieur. Impossibilité de savoir si l'utilisateur exécutait v6.3.

1. **Fix réel** : BOOT_DONE ne fait plus que `sw_pending = 1` ; registration au **1er
   OP1_EV_SERVICE** (kn_core.c:110, kn_event SERVICE inconditionnel après le kn_event
   BOOT_DONE du 1er passage) → strictement après le BOOT_DONE de midiseq → MIDI SEQ 1re
   rangée (verte, ligne 2 visuelle), MIDI MAP 2e (blanche, ligne 3 visuelle).
2. **Toast de version** : `op1_toast("midi map v6.4")` au même endroit, une fois au boot →
   preuve immédiate de la version qui tourne (à retirer en v6.5 après confirmation).
3. Doublons obsolètes `hundekraft-plugin/op1midi/mm2_*.c` supprimés (le build copie depuis
   `mm2/`, source canonique).
- **`op1midi.knl` v6.4** : 16 032 o, 17 imports, 0 erreur. mm2_test ok.

Reste : test hardware v6.4 (toast visible, rangée blanche ligne 3), README utilisateur.


## 17. v6.3 — fixes hardware (28 octobre 2026)

Retour du test OP-1 v6.2 : MIDI MAP sur la 2ᵉ ligne (vert), Shift inopérant, seul le mute
fonctionne, pas de fade.

1. **Rangée settings** : notre BOOT_DONE livrait le switch AVANT celui de midiseq (ordre de
   livraison des features ≠ hypothèse). Fix: enregistrement au **premier OP1_EV_SERVICE**
   après le BOOT_DONE → après midiseq → **MIDI MAP blanc ligne 3**, MIDI SEQ vert ligne 2.
2. **Garde d'état tape** : n'accepter que l'état 3 comme « arrêté » tuait tout (marqueurs,
   fade) si le R246 réel utilise 0 — **0 et 3 acceptés** désormais (5/6/7 toujours refusés).
   C'est l'explication la plus probable du « seul le mute fonctionne ».
3. **Shift** : lecture directe de l'**octet Shift stock** (`0x01DD7F78`, l'adresse qu'utilise
   le propre plugin markers du kennel) au lieu du champ `in->shift` livré — fiable sur
   hardware pour toutes les actions Shift (canal, learn on/off, sorties, delete, masque).
- **`op1midi.knl` v6.3** : 16 016 octets, 17 imports, 0 warning. mm2_test vert (état 0 ajouté).

Reste : re-test hardware v6.3, README utilisateur.



## 15. v6.1 — masque de pistes du fade + fix de build (28 octobre 2026)

- **Shift + rotation rouge = pistes du fade** (par ligne): ALL → 1 → 2 → 3 → 4 → 1+2 → 3+4 →
  ALL (wrap; `MM2_FM_*`, stocké dans la ligne — les anciennes lignes décoderont ALL). Le moteur
  de fade ne ramp que les pistes du masque (capture/rampe/restauration limitées au masque).
- **MUTE avec fade**: en lecture, un trigger MUTE armé avec fade > 0 fait maintenant
  FADE OUT → toggle au silence → FADE IN (au lieu du toggle instantané); hors lecture ou
  fade OFF: toggle immédiat (inchangé).
- **Fix de build important**: `build-knl.sh` copiait les sources mm2 depuis la COPIE du plugin
  (périmée) — le « knl v6 » publié était en réalité un build v5 (d'où les fonctions non
  décalées vues par l'utilisateur). Le script copie désormais la source canonique `mm2/`;
  le knl v6.1 embarque le vrai v6 + le masque. **À réinstaller via le Workshop.**
- **`op1midi.knl` v6.1** : 16 112 octets, 17 imports, 0 warning. `mm2_test` (masque: cycle
  Shift+rouge, hooks recevant le mask, roundtrip) → **vert**; simulateur: **57 self-tests**.



## 13. v5.1 — affichages (28 octobre 2026)

- **Diagnostic « chiffres de destination absents »** : la cause était le **simulateur** — sa
  fonction render était une chimère v3/v4 (référençant `X_DN`/`mmfade`/`mmch` supprimés) qui
  plantait à chaque rendu ; le **plugin C n'était pas touché** (la fusion `MRK 11` y est depuis
  v5 et compile). Fix: render v5 unique + **smoke-test du rendu réel sous stub DOM**
  (`simulator/render-smoke.js`) + vérification exacte du texte destination (`dstTextOf`:
  « MRK 11 » / « TAPE 3 » / « -- ») — le trou de couverture render est comblé.
- **Ajustements demandés** : colonne blanche = **fade de la ligne** (OFF/1S/2S/8S/32S) au lieu
  du canal ; le canal de la ligne n'apparaît que **tant que Shift est tenu** (colonne type,
  LRN prioritaire pendant le learn) ; **chiffres de slot en rouge** (violet abandonné).
- **`op1midi.knl` v5.1** : 15 720 octets, 17 imports, 0 warning. Tests: mm2_test vert,
  41 self-tests sim + smoke-test rendu.

Reste : test matériel, README utilisateur.

## 28. v7.4 — mode mixer + corrections fade (29 octobre 2026)

Retours hardware: MRK muets en mode mixer; fade absent/à durée aberrante sur M+/M-; fade-in
seulement en mode tape; demande de fade sur MUTE; case fade vide si non applicable; sélecteur
de pistes du fade au clic rouge.

1. **Mixer muet** : `env_query` n'acceptait que le mode tape (2) — le mode **mixer (3)**
   (touches 0x02..0x05 = synth/drum/tape/mixer, tape=2 → clé−2) et mixov (0x0D) bloquaient
   tout le chemin marker. Tape_mode = 2, 3 ou 0x0D.
2. **M+/M-** : inversion d'arguments dans la file (`pq_push(K_MNEXT, fade, fmask)` vs drain
   qui lit `p.b` = fade) → la **durée** du fade était la valeur du **masque**. Corrigé.
3. **Masque jamais appliqué** : `fade_mask` n'était jamais assigné (toujours ALL). Corrigé
   (`fade_mask = p.m` au drain).
4. **MUTE + fade** : en lecture, la piste mute fond (elle seule), byte basculé au silence,
   volumes restaurés juste après (inaudible sous mute); unmute direct (sûr si unmute manuel).
   Chemin `J_MUTE` (mort depuis v5) activé.
5. **Éditeur** : case fade **vide** pour OFF/SND/TAPE/COMBO; pour les sauts, secondes +
   destination en petites capitales (ALL/P1/P2/P3/P4/P12/P34), **clic rouge** = cycle
   (Shift+rotation inchangé); MUTE: secondes seules (l'unité s).
- **`op1midi.knl` v7.4** : 21 180 o, 0 warning. mm2_test ok (clic rouge testé sur MRK/MUTE/SND).

## 29. v7.5 — lecture du midi interne par ligne (29 octobre 2026)

Demande: Shift+clic rouge = reliquat à supprimer; clic rouge = activer/désactiver la lecture
du midi interne; petit i devant la source (rouge actif / gris sinon); destination du fade par
Shift+rotation rouge.

1. **kint par ligne** (bit 31 du codec, INVERSÉ: 0 = lit — les lignes pré-v7.5 gardent la
   lecture interne, pas de reset 'mmv'). Clic rouge: toggle de la ligne du curseur.
2. **Pipeline**: notes internes -> `mm2_note_int`/`mm2_note_match_int` (lignes kint only);
   une ligne kint=0 voit sa touche jouer le synthé normalement (on et off passent).
3. **Éditeur v13**: i (petites capitales) devant la source, rouge/gris (indépendant de la
   surbrillance); Shift+clic rouge = rien; Shift+rotation rouge = pistes du fade (inchangé).
4. Tests: codec bit31 (pack négatif, unpack pré-v7.5 = lit), dispatch interne/externe,
   toggle + reliquat + Shift+rotation, suppression (Shift+clic bleu) remet kint au défaut.
- **`op1midi.knl` v7.5** : 21 492 o, 0 warning. mm2_test ok.

## 30. OP1midiMap 1.7.5 — renommage, hint settings + destinations LOOP (30 octobre 2026)

Trois demandes utilisateur: (1) un message clignotant sur la page settings (rendons l'éditeur
repérable), (2) renommage du dépôt OP1midi -> OP1midiMap avec versionnage .knl en 1.7.x, (3)
destinations manquantes: décalage de la loop droite/gauche + boucle on/off (mode tape), avec
pictogrammes dans la colonne des destinations.

1. **Renommage** : projet/dépôt = OP1midiMap (gh repo rename); le FEAT / l'espace de réglages
   "op1midi" et le fichier `op1midi.knl` restent inchangés (persistance + continuité du
   workshop). Versionnage: 1.7.5 = première version sous ce nom.
2. **Hint clignotant** : « WHITE: EDIT » (SF_SMALL blanc) clignote à ~1 Hz sur la rangée MIDI
   MAP de la page settings (y = 88 px, x = 210) quand la page T3 est affichée. Détection par
   miroir: nos hooks d'input passent AVANT `kn_pages_key`, on suit donc T3 (toggle) et
   Shift+Help (`show(0)`) exactement; quitter l'éditeur hors Shift laisse la page se basculer
   — miroir dans le même flux que le dispatcher.
3. **LOOP** (port des Shift+flèches stock): destinations L> (décalage droite), L< (gauche),
   LOP (on/off). Testé dans le bundle: `op1_tape_loop_get/set` (kn_tapesvc.c, exportéd) =
   actions hot SANS injection de touches ni saut; pareil à l'arrêt et en lecture vues tape/
   mixer/mixov, refusé armé/enrec/punch/disk. Pas = largeur/16 (min 0x1400 échantillons
   ~54 ms), largeur conservée, clamp 0..0xF23FA0. `LOOP_ON/LOOP_EFF` suivent, dirty + redraw
   gérés par `op1_tape_loop_set`.
4. **Pictogrammes** : la colonne blanche des destinations dessine flèche droite / flèche
   gauche / anneau de boucle (traits raster Q10 width 2, 14 px) au lieu du texte — la police
   kennel n'a pas les glyphes; le codec garde des noms "L>"/"L<"/"LOP".
5. **Codec v5** ('mmv' 4 -> 5, reset une fois): dst sur 4 bits (8..10), dnum sur 7 bits
   (dnum>127 invalide au pack; plage par destination inchangée). Ligne: le clavier rejoue les
   anciennes lignes à zéro — re-mapping nécessaire aprés installation (documenté NOTICE/README).
6. Tests: dispatch kinds 0/1/2 (CC et PC), garde noyau = aucune (filtrage intégration),
   liste plate 52..54 + wrap à 54, pack/unpack v5. mm2_test ok; build 0 warning,
   `op1midi.knl` 23 100 o (imports `op1_tape_loop_get`/`op1_tape_loop_set`).

## 31. 1.7.5 bis — retours hardware sur les loop (30 octobre 2026)

Deux retours après flash: (1) les dests loop en premier dans la liste du blanc, avant COMBO;
(2) les pictogrammes étaient blancs -> invisibles sur la ligne sélectionnée (fond blanc).

1. **Ordre**: liste plate v1.7.5 = OFF, L>, L<, LOP, COMBO, SND 1..8, TAPE 1..4, MUTE 1..4,
   MRK 1..32, M+, M- (0..54). La première destination atteinte en tournant depuis OFF est
   L> — COMBO reste à sa valeur de liste (+4 crans pour l'atteindre). Tests recalculés.
2. **Picto vert fixe**: la couleur du tracé est bien un paramètre du dessin (donc dynamique
   POSSIBLE), mais le picto blanc sur le fond blanc de la surbrillance ne se voyait pas.
   Vert permanent (demandé vert pour rappeler la boucle OP-1): lisible sur les deux fonds.
   dst_texts 'L>'/'L<'/'LOP' restent des replis.
- **`op1midi.knl`** : 23 084 o, 0 warning. mm2_test ok.

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




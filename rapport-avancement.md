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
- **`op1midi.knl` packé avec succès** : 11 756 octets, .text 5100 / .rodata 1996 / .bss 212,
  351 relocations, 13 imports **tous présents dans kn_exports.c**, aucun warning packer
  (compilé avec bfin-elf-gcc 12.3.0 ; la build officielle utilise 13.3.0 — à aligner si besoin).
- Build reproductible : `hundekraft-plugin/build-knl.sh` (injecte le plugin dans le bundle,
  packe sous Docker `bfin-elf-toolchain-py` = image toolchain + python3, sort `op1midi.knl`).

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

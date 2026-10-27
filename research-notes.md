# Notes de recherche — firmware communautaire OP-1 OG

Recherche effectuée le 27 octobre 2026. Faits directs séparés des déductions; les fonctions évoluent selon version.

## Projets identifiés

### Hundekraft OP-1 Firmware Workshop
- Dépôt : [github.com/Hundekraft/op1-workshop](https://github.com/Hundekraft/op1-workshop) — release indiquée dans README comme **3.0.3**, ciblant OP-1 OG/R246, pas Field.
- Patcher/build configurable dans le navigateur depuis le firmware officiel R246 non modifié. Le firmware n'est pas téléversé selon README; choix de catalogue dans `localStorage`, fichiers plugins et opérations en attente dans `IndexedDB` au scope du navigateur/site. Ce mécanisme décrit l'application web, non la config embarquée de l'OP-1.
- Après une première installation via menu boot, mises à jour / gestion plugins peuvent se faire par USB MIDI. L'interface fait vérifier et réviser l'opération; le README dit firmware source GPL-3.0-or-later avec permission liée au firmware OP-1 et SDK + instructions inclus dans bundle source. Pas de firmware TE ni clé appareil fournis.
- Le README annonce 300 emplacements de presets synthé et 152 presets collection courante. Fonctions 3.0 comprennent « MIDI CC and sync », MIDI Tape 4 pistes (notes et CC), quatre tapes distinctes (tracks, loop, tempo, album propres), undo.
- Structure frontend visible : `app.js`, `runtime.py`, `worker.js`, `plugin-plan.js`, `plugin-state.js`, `op1push.js`, `catalog.json`, `release.json`, `manual.html`, etc.; dossier `source/` mais le code firmware est fourni comme bundle correspondant release. N'en déduire aucune source C accessible sans examiner archive précise.

### op1r et sources
- Patcher : [github.com/pcppcp/op1r](https://github.com/pcppcp/op1r). Modifie le firmware R246 fourni par l'utilisateur; Python 3.8+, bibliothèque standard; README propose `python3 patch_op1.py input.op1 -o output.op1`. N'inclut pas firmware TE.
- Source : [github.com/pcppcp/op1r-src](https://github.com/pcppcp/op1r-src). Le README indique dépôt sans firmware propriétaire et exige `op1_246.op1`; premier release bâti à partir de ce repo annoncé comme op1r 27 au moment du texte consulté. Voir `AGENTS.md` pour structure/build/tests.
- Cross compilation : Docker `toolchain/Dockerfile`, cross-compiler Blackfin `bfin-elf gcc 12`; pas ARM/OMAP selon ces sources. Build décrit par source : `docker build -t bfin-elf-toolchain toolchain`, installation op1repacker, unpack R246, `python3 make_firmware.py --opz`. Tests PC : `harness/audio.py`, `harness/render.py`, `harness/render_euclid.py`.
- Chemin plugin : `synth_sdk/plugins/` par plugin (`plugin.json` + C + écran); `op1_plugin.h`; dispatcher/hooks `dispatcher.c`, `hook.S`, `link.ld`; core dans dossiers screens/seq/lfo/tape. Le README avertit que quelques patches touchent code chiffré et se basent sur table de saut fixe (`l1_patches.json`).

## MIDI, interface et tape
- op1r README documente MIDI CC via USB sur canal MIDI OP-1 : messages pour simuler touches dérivés du key-code +5, valeur >=64 press / <64 release; encodeurs CC 102–105 en relatif (1–63 sens haut, 65–127 sens bas). Le README mentionne aussi contrôle CC de touches/encodeurs, MIDI sync et séquenceur MIDI 4 pistes associé à la bande. Cela confirme de la logique MIDI existante mais ne prouve pas de mapping utilisateur programmable des commandes tape.
- Fonctionnalités MIDI supplémentaires de Hundekraft sont indiquées par Workshop (MIDI CC/sync, MIDI Tape) et README; aucune liste de mapping personnalisable CC/PC n'a été confirmée.
- Op1r : count-in configuré depuis écran clock + T3. Tape markers : red/orange press place un marqueur; Shift+press supprime sous curseur; Shift+rotation saute au suivant/précédent même en lecture; jusqu'à 32 selon README. Enregistrement punch-in : maintenir Tape + REC, le prochain marqueur commence et le suivant arrête. Ces fonctions sont propres au firmware communautaire.
- Hundekraft manual consulté : pour MIDI SEQ réglages, Shift + virgule puis point ouvre settings; tourner encodeur vert active/désactive MIDI SEQ. Ce chemin diffère de l'assertion reçue « Shift+Help puis T3 »; celle-ci reste non vérifiée. Ne pas confondre clock + T3 (op1r count-in) avec un écran universel de paramètres.
- Manuel TE OG : mode Tape a quatre pistes, enregistrement standard 6 minutes à vitesse normale, 44,1 kHz/16 bits; T1–T4 sélectionnent pistes. Disk mode : Shift+COM puis T3, répertoire `tape`, fichiers `track_1.aif` … `track_4.aif`. Le guide mentionne une bande/fichier en usage de base.
- Hundekraft décrit quatre tapes, chacune ayant tracks, loop, tempo et album indépendants. Cela corrobore « quatre bandes » communautaires, pas une durée garantie de 6 minutes pour chacune ni une allocation mémoire exacte.
- « MAXVW » : aucune source concluante trouvée/consultée. « 4×4 pistes de 6 min » semble assembler la capacité stock (4 pistes × 6 min) avec quatre tapes du mod. On ne peut affirmer quatre fois 6 min par piste, ni une allocation fixe, sans lecture et validation du code gestion tape/flash. Mention à traiter comme hypothèse/question technique.

## Matériel et format firmware
- [op1hacks/docs](https://github.com/op1hacks/docs) décrit l'image `.op1` : CRC32 4 octets puis LZMA, extraction LZMA -> TAR; arborescence avec `content/audio`, `content/display`, bases `op1.db`, `op1_factory.db`, `tape.db`, chargeurs `OP1_vdk.ldr`, `te-boot.ldr`. La documentation rapporte chiffrement XTEA de blocs code dans `OP1_vdk.ldr` (premiers 8 octets chiffrés sur blocs de 24), clé en OTP Blackfin.
- Donc l'affirmation de matériel « ARM Cortex-A8 / OMAP » est en conflit avec indices Blackfin documentés ici. Pas de preuve Cortex-A8 dans cette recherche; source matérielle complète non vérifiée, donc note comme correction forte fondée sur documentation reverse engineering + toolchain, plutôt que spéculation OMAP.

## « MAXVW » / autres hacks
- Recherches web sur « MAXVW », « 4x4 6 minutes » et hack OP-1 n'ont pas fait apparaître d'identification vérifiable de projet nommé MAXVW. L'affirmation de capacité reste non corroborée.
- Autre projet distinct : [maikopruett.com/op-1](https://maikopruett.com/op-1), firmware community #251 consulté, annonce quatre tapes, undo/redo, tape edits, MIDI CC, synths/FX/séquenceurs; #250 ajoute 4 bandes avec opérations tape, #249 MIDI CC et outils op1r. Ne pas attribuer automatiquement ses comportements à Hundekraft.
- Outils reverse engineering : [op1hacks/op1repacker](https://github.com/op1hacks/op1repacker) pour unpack/repack; [op1hacks/op1-fw-archive](https://github.com/op1hacks/op1-fw-archive) archive de versions. Le firmware original doit rester une entrée locale de l'utilisateur.

## Sources primaires consultées
1. [Hundekraft Workshop README](https://github.com/Hundekraft/op1-workshop) — release, configuration navigateur, plugins, update, composants et capacités.
2. [Hundekraft manuel](https://hundekraft.github.io/op1-workshop/manual.html) — commandes de markers/settings (les séquences peuvent varier selon version).
3. [op1r patcher README](https://github.com/pcppcp/op1r) — usage, commandes, MIDI CC, markers, count-in, punch.
4. [op1r source README](https://github.com/pcppcp/op1r-src) — architecture, toolchain Blackfin, build et harness.
5. [OP-1 firmware docs](https://github.com/op1hacks/docs) — format `.op1`, contenu extrait, Blackfin.
6. [Guide officiel TE — Tape mode original](https://teenage.engineering/guides/op-1/original/tape-mode) — 4 pistes, 6 min/44,1 kHz/16 bits et sauvegarde disk mode.
7. [Maiko — releases OP-1](https://maikopruett.com/op-1) — firmware séparé #247–#251, quatre tapes et MIDI CC.

## Sources de contexte, moins autoritatives
- [Rackears Hundekraft](https://www.rackears.io/patches/op1-workshop), [FW Radar](https://fwradar.com/p/teenage-engineering-op-1/custom-firmware) résument des versions récentes mais sont des agrégateurs.
- La requête « MAXVW » n'a pas donné d'URL ou de dépôt concluant; aucune source ne doit être inventée.

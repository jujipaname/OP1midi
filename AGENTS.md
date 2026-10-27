# AGENTS.md — repères projet OP-1 OG / firmware communautaire

## Cible et limites
- Cible : Teenage Engineering **OP-1 original (OG)**, firmware de base R246 (`op1_246.op1`). Ne pas confondre avec OP-1 Field.
- Important : la piste « Cortex-A8 / OMAP » n'est **pas étayée** pour l'OP-1 OG. La documentation de reverse engineering décrit un cœur firmware `OP1_vdk.ldr` et le chiffrement Blackfin; le dépôt op1r utilise un cross-compilateur **Blackfin `bfin-elf-gcc`**. Traiter ARM/OMAP comme une supposition erronée, sauf confirmation nouvelle par source matérielle fiable.
- Le firmware distribué par TE est une image complète. Selon op1hacks/docs : CRC32 de 4 octets suivi de données LZMA décompressibles en archive TAR. L'image ne doit pas être modifiée à l'aveugle.
- Ces notes documentent principalement op1r et Hundekraft Workshop; elles ne prouvent pas que leurs bases de code soient identiques.

## Règles de sûreté strictes
1. Ne jamais écrire/flasher sur un OP-1 physique sans validation explicite, sauvegarde vérifiée, image connue et procédure de récupération documentée.
2. Tous les tests de parsing, patch, migration et update se font sur copies locales isolées. Préserver l'original R246 en lecture seule et contrôler hashes/CRC.
3. Ne jamais ajouter au dépôt le firmware propriétaire TE, clé OTP, dump privé ou données utilisateur. Le code op1r-source demande une copie officielle fournie par l'utilisateur.
4. Valider chaque image reconstruite hors appareil (structure, taille, CRC, extraction/ré-emballage reproductible) avant tout essai matériel. La communauté avertit qu'une image défectueuse peut bloquer l'appareil.
5. Pour les données de bande, effectuer les opérations destructives sur une copie sandbox et conserver les fichiers source.

## Structure connue de `pcppcp/op1r-src`
- `synth_sdk/plugins/` : plugins (moteurs synthé/effets), un dossier avec `plugin.json`, C et écran.
- `synth_sdk/template/`, `op1_plugin.h` : gabarits et API plugin.
- `synth_sdk/dispatcher.c`, `hook.S`, `link.ld` : dispatcher/hooks et linkage firmware.
- `synth_sdk/screens/`, `seq/`, `lfo/`, `tape/` : écrans, séquenceurs, LFO, outils tape.
- `synth_sdk/display/` : générateurs SVG; `synth_sdk/harness/` : tests PC et rendus PNG/WAV.
- `synth_sdk/make_firmware.py` : assemblage du firmware; `tools/` : scripts MIDI/USB.
- Les patches vers code chiffré sont pré-calculés (`synth_sdk/l1_patches.json`) et ciblent une table fixe : toute modification de ces patches nécessite mainteneur compétent.

## Build / tests (op1r-src)
Dépendances documentées : Docker, Python 3, image originale `op1_246.op1`, `op1repacker`.
```sh
docker build -t bfin-elf-toolchain toolchain
git clone https://github.com/op1hacks/op1repacker.git
python3 -m venv op1repacker/.venv
op1repacker/.venv/bin/pip install -e op1repacker
cp /chemin/vers/op1_246.op1 .
op1repacker/.venv/bin/op1repacker unpack op1_246.op1
cd synth_sdk
python3 make_firmware.py --opz
```
La commande produit selon le README `../op1_246_custom-repacked.op1`. La commande de build est reproduite du README; `--opz` est l'option publiée, à valider dans la version/branche courante.
Tests sur PC (pas de firmware/appareil requis; C, Python, pycairo et numpy selon tests) :
```sh
cd synth_sdk
python3 harness/audio.py supersaw
python3 harness/render.py tape out.png
python3 harness/render_euclid.py
```
Patcher utilisateur plus simple (autre dépôt `pcppcp/op1r`, Python >=3.8, bibliothèque standard) : `python3 patch_op1.py /chemin/op1_246.op1 -o op1-custom.op1`. Cela n'est pas le même workflow que compiler `op1r-src`.

## Touches, tape et MIDI : repères vérifiés
- OP-1 stock : T1–T4 servent de pistes en mode Tape; le guide TE OG annonce quatre pistes et six minutes à vitesse normale / 44,1 kHz, 16 bits. Shift+COM puis T3 ouvre disk mode; le dossier `tape` contient `track_1.aif` à `track_4.aif` pour la bande exportée.
- op1r : écran clock + T3 règle le count-in; sur sa vue Tape, press orange = marqueur, Shift+press orange = supprimer le marqueur sous curseur, Shift+tourner orange = marqueur précédent/suivant (jusqu'à 32 dans README op1r). Les marqueurs sont des fonctions op1r, pas garanties stock.
- Un changement de bande: Hundekraft documente quatre tapes, chacune avec pistes/loop/tempo/album distincts. Les détails clavier spécifiques et la supposée combinaison Shift+flèche droite ne sont pas établis ici; vérifier dans le manuel/version visée avant implémentation.
- MIDI déjà présent : op1r documente USB MIDI CC des touches et encodeurs, MIDI sync, et un enregistreur MIDI 4 pistes synchronisé tape. Hundekraft inclut MIDI CC/sync et MIDI Tape. Voir `midi-spec.md` pour la proposition d'extension.
- Configuration : Hundekraft Workshop garde choix catalogue dans `localStorage`, octets plugin et opérations dans `IndexedDB` du navigateur. C'est la configuration du **Workshop Web**, pas la preuve d'un fichier config JSON persistant sur le disque OP-1. Pas de JSON utilisateur sur le disque identifié dans les sources consultées.

## Incertitudes à résoudre avant modification
- Aucune preuve consultée ne confirme « MAXVW » ou un découpage physique fixe « 4 × 4 pistes de 6 minutes » sur OP-1 OG. Le manuel TE confirme 4 pistes × 6 min pour une bande; Hundekraft annonce 4 bandes indépendantes avec piste/loop/tempo/album propres. Ne pas déduire 24 min × 4 ni présumer de la partition mémoire: vérifier code `tape/`, gestion flash et tests.
- L'accès « Shift+Help puis T3 vers une page Settings » est rapporté dans le contexte utilisateur mais n'est pas corroboré sous cette séquence exacte. op1r README donne clock/Help + T3 pour count-in; manuel Hundekraft décrit un chemin Shift + virgule, puis point, pour ses réglages. Distinguer cette UI du settings page demandé.

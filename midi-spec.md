# Proposition de spécification — mapping MIDI OP-1 OG

**Statut :** proposition d'architecture, non fonctionnalité confirmée de Hundekraft/op1r. Les sources consultées confirment déjà des hooks MIDI CC/sync et un MIDI Tape 4 pistes; cette note vise une couche de mapping utilisateur des fonctions de transport et de sélection.

## But et périmètre
Permettre d'assigner des messages **Control Change (CC)** et **Program Change (PC)** entrants à des actions que l'on réalise normalement par commandes locales (touches, Shift, encodeur). La priorité est aux commandes discrètes; ne pas transformer le mapping en séquenceur d'automatisation non borné.

Actions souhaitées :
- prochain/précédent marqueur ou boucle (dont raccourci rapporté Shift+→, à confirmer dans la version exacte);
- lecture à partir du marqueur courant / début de boucle;
- sélectionner une bande parmi les bandes disponibles;
- sélectionner banque synthé / slot preset, sans confusion avec les banques du matériel MIDI externe;
- lecture, arrêt, armement/annulation punch; fonctions d'édition uniquement si réversibles et explicitement activées.

## Modèle d'événement proposé
- Écouter l'entrée USB MIDI sur canal configurable (défaut : canal de réception actuel de l'OP-1; `OMNI` facultatif). Ignorer les messages d'autres canaux.
- Mapping = `(type, canal, numéro)` vers `action`, avec paramètres. CC : valeurs 0–127; mode `trigger` (front montant, seuil configurable et hystérésis) ou `absolute` (pour action à valeur). Par défaut un CC lance l'action une seule fois sur transition vers ≥64; ne pas répéter l'action à chaque paquet.
- PC 0–127 sélectionne une entrée dans une liste définie par l'utilisateur (`0` affiche le premier programme; documenter convention zéro-based). PC ne déclenche pas plusieurs actions simultanées par défaut.
- Protection : valider plage/index existant, ignorer action indisponible ou bande absente, pas d'écriture disque/format/effacement via MIDI; anti-rebond et journal d'état facultatifs. « Changer de bande » doit attendre un point sûr pour réduire risques de glitch; définir l'état transport en cas de switch.
- Les combinaisons de touches ne doivent pas être simulées comme maintien Shift global si cela pourrait interagir avec l'UI; appeler fonctions d'action internes testées (next marker, play-from-marker, select tape, etc.).

## Schéma de configuration candidat
```json
{
  "version": 1,
  "channel": 1,
  "mappings": [
    {"type":"cc", "number":20, "mode":"trigger", "action":"tape.marker.next"},
    {"type":"cc", "number":21, "mode":"trigger", "action":"tape.play_from_marker"},
    {"type":"pc", "number":0, "action":"tape.select", "value":0},
    {"type":"pc", "number":16, "action":"synth.bank.select", "value":0}
  ]
}
```
Exemple illustratif uniquement. Les identifiants d'action, CC/PC, capacités de parser JSON sur la cible et emplacement persistant sont **à concevoir**, pas des propriétés déjà observées. La configuration réelle doit avoir limites de taille, validation stricte, valeurs par défaut embarquées, migration versionnée et refus sûr des clés inconnues/valeurs hors plage.

## Actions et sémantique proposée
| Action logique | Déclencheur | Garde / résultat |
|---|---|---|
| `tape.marker.next` / `previous` | CC trigger | Nécessite marqueurs; boucle au début/fin ou pas? décider et afficher. op1r existant saute au marqueur par rotation de l'encodeur. |
| `tape.play_from_marker` | CC ou PC réservé | Se positionne puis lance PLAY; n'écrit rien. Préciser si marqueur courant ou prochain. |
| `tape.select` | PC (préféré) ou CC valeur | `value` doit identifier une des bandes disponibles; transport/fondu et conservation des pistes à spécifier. Hundekraft annonce quatre tapes. |
| `synth.bank.select` | PC | Banque/slot valide, sans écraser preset. Nom « bank » doit refléter navigation moteur et preset réelle. |
| `transport.play`, `stop`, `record.arm` | CC trigger | Stop/REC ne doivent pas devenir destructifs; distinguer armement et confirmation d'enregistrement. |
| `loop.select` | PC ou CC valeur | À n'activer que si concept de loop est exposé par build; définir index/base explicitement. |

Les commandes aux conséquences destructives ou difficilement annulables (effacer, formater, couper/coller/bounce) ne sont pas dans le mapping initial.

## Où éditer la configuration ?

### UI embarquée — page Settings
**Avantages :** retour d'état et validation immédiats; pas de PC requis; sélection canal/actions lisible; moins d'erreurs de syntaxe ou de chemin; portable avec l'appareil. Peut servir d'outil MIDI learn.
**Inconvénients :** développement coûteux sur écran/softkeys très contraints; liste de mapping peut être pénible; tests UI et consommation ressources; difficulté d'import/export et d'édition en masse; séquence d'accès n'est pas encore validée. Le chemin « Shift+Help puis T3 » n'est pas vérifié; op1r donne clock + T3 pour count-in, et le manuel Hundekraft décrit Shift+, puis . pour settings.

### Fichier JSON sur disque USB
**Avantages :** facile à sauvegarder/éditer, diffable, peut être généré sur PC, import/export de profils rapide.
**Inconvénients :** le mode disque est un accès USB de stockage plutôt qu'un accès application garanti; source ne prouve ni lecture runtime du JSON ni chemin config sur l'OP-1; format invalide, chemins, corruption/éjection, permissions/FS et détection à chaud à traiter. Risque de confusion avec les JSON de catalogue du Workshop qui sont côté web et non config appareil.

### Recommandation
Faire de la **page Settings embarquée** la source de vérité minimale; proposer ultérieurement export/import via un fichier JSON bien documenté sur un chemin dédié, si le firmware peut lire/écrire ce fichier de façon sûre. Pour le MVP, commencer par un mapping embarqué réduit et stable, valider via simulateur MIDI et PC harness. Ne pas supposer « Shift+Help → T3 » sans test sur l'image ciblée. Si les limitations UI rendent l'édition impraticable, préférer un import explicite depuis un outil compagnon validant strictement le JSON, plutôt qu'un chargement automatique arbitraire sur disque.

## Tests d'acceptation avant appareil
1. Tests unitaires du parseur (JSON invalide, doublons, version inconnue, limite de taille, CC/PC/canal hors plage).
2. Test synthétique des événements MIDI: seuil/hystérésis, note-on/off ignorés, routage canal, aucun double trigger, CC concurrent.
3. Mock des handlers: marqueur inexistant, bande absente, transport actif, switch pendant enregistrement.
4. Régression sur copie sandbox du firmware et fixtures tape sauvegardées; aucun test d'écriture réelle sur OP-1.
5. Vérifier que mapping désactivé ne modifie pas le comportement MIDI actuel et que les commandes destructives sont impossibles par défaut.

## Design v2 — éditeur généraliste (cible : base Hundekraft 3.0.3)

Spécification validée avec l'utilisateur, noyau implémenté dans `mm2/` (module portable sans
dépendance op1r, prêt à être câblé sur le bundle Hundekraft) :

- **16 lignes** de mapping `(canal, type CC/PC, numéro) → (action, valeur)`, 4 visibles, 4 pages.
- **Éditeur** (machine à états pure, `mm2/mm2_edit.c`) : bleu = canal (1–16/ALL), vert = numéro
  (0–127, appui = bascule CC↔PC), **blanc = éditeur** (rotation = action ; appui = édition de la
  valeur, re-appui = validation), **rouge = curseur** (16 lignes, pagination auto), flèches 0x24/0x25
  = pages, **Shift+Help = valider + fermer**. Rendu côté intégration : ligne du curseur en **rouge**,
  autres en blanc.
- **Actions initiales** : MARKER NEXT, MARKER PREV, PLAY@MARKER, TAPE SELECT (valeur 1–4),
  INSTRUMENT SELECT (valeur 0–255, bornée par l'intégration). Liste extensible (ids constants).
- **Accès** : page settings (Shift+Help puis T3) + rangée « MIDI MAP » (rotation blanche = ON/OFF
  global `mmen`, appui blanc = ouvre l'éditeur).
- **Persistance** : clés `mmen` + `mmr0..mmr15` (int32 packé : canal/type/numéro/action/valeur),
  clés constantes (les implémentations hôte stockent le pointeur).
- **Dispatch** : CC front ≥ 64 sans répétition, PC à chaque message, première ligne correspondante
  gagnante, ligne `OFF` inactive, gardes marqueur via `mm2_env_query` (mode tape, arrêt, punch,
  count-in, marqueurs présents).
- **Hooks d'intégration** (pointeurs définis dans le module, assignés par l'hôte) :
  `mm2_setting_get/put`, `mm2_act_next/play/tape/instr`, `mm2_env_query`. Hooks nuls = action
  ignorée sans crash.
- **Tests** : `mm2/mm2_test.c` (codec, persistance aller-retour, dispatch CC/PC, gardes, hooks
  absents, éditeur complet) — **vert** avec `-fsanitize=address,undefined` et en build plain.

Inconnues restantes côté bundle Hundekraft (phase 1 du portage) : chemin de réception **PC**,
API de switch des 4 tapes, API de sélection instrument, présence de marqueurs en 3.0.3, structure
de leur page T3.

## Portage Hundekraft 3.0.3 — réalisé

Les inconnues ci-dessus sont **toutes levées** (bundle source analysé) : `midi_raw` reçoit CC **et**
PC ; `op1_tape_switch` gère les 4 tapes ; les marqueurs sont des **données partagées du core**
(`tm_pos[]/tm_n`, remplies par le plugin `markers`) ; `op1_switch` fournit la rangée settings T3 ;
`op1_seq_press_play` déclenche PLAY. Un plugin se compile **seul** (`.knl`) et s'installe via le
Workshop sans rebuild du firmware. Le plugin **`op1midi.knl`** est packé (imports tous résolus,
pack propre) — détail complet dans `rapport-avancement.md` §6. Seule limite : INSTRUMENT SELECT
sans service core (toast d'information).

## État d'implémentation MVP (27 octobre 2026, fork op1r-src dans ce dépôt)

Implémenté et testé sur PC (harness `harness/run_all.py`, tout vert; aucun flash appareil):

- `synth_sdk/midi/mm.c|mm.h`: mapping CC → actions `tape.marker.next` (CC défaut 80),
  `tape.marker.previous` (81), `tape.play_from_marker` (82). Mode trigger (front montant ≥ 64, un
  seul déclenchement), canal configurable (`mmch`: 0 = off, 1–16, 17 = all), CC consommés même si
  une garde saute l'action. CC réservés (touches/LFO/mx/encodeurs/mixer: 1–17, 21–26, 38–43, 48–60,
  72–79, 102–118) refusés à la charge; doublons entre actions → seul le premier agit.
- Fil d'exécution: `midi_cc_hook` (dispatcher.c, midi_poll) marque l'action et empile la clé privée
  `MM_KEY 0x40` (motif de `midi/mx.c`); `mm_apply` (thread KeyHandler) exécute: `tm_turn(±1024)`
  (tape/markers.c) et `tm_locate_current()` + PLAY stock (Shift neutralisé, motif euclid).
- Gardes: sauts de marqueur seulement en mode tape (CUR_MODE_ID 2, écran tape chargé, ≥ 1 marqueur);
  play-from-marker seulement à l'arrêt (tape state 0), punch inactif (`pu_mode` 0), count-in inactif.
- `tape/markers.c`: ajout `tm_locate_current()` (marqueur courant = dernier ≤ curseur + 10 ms,
  ne bouge pas si déjà dessus; testé dans `harness/markers_test.c`).
- Persistance: clés `mmch`/`mm0`/`mm1`/`mm2` dans `op1rsettings` (op1.db, `screens/op1r_settings.c`).
- Page Settings (clock + T3): rangée verte « MIDI » (canal OFF/1..16/ALL), blanche « NEXT » et rouge
  « PREV » (CC, 0 = --, wrap); CC play-from affiché en lecture seule (`PLAY n`).
- Outil compagnon `tools/op1midi_map.py`: validation stricte du JSON (version 1, actions connues,
  CC 0–127, réservés et doublons rejetés), affichage des valeurs appareil correspondantes, envoi des
  CC de test via mido. L'import automatique vers l'appareil (SysEx/DB) reste à faire — pour l'instant
  la page Settings est la source de vérité, l'outil documente les valeurs `mmch`/`mm0..2` à y saisir.
- Écart au spec initial: PC non mappé dans ce MVP (actions tape uniquement); « play from marker »
  replie sur PLAY simple sans relocation quand aucun marqueur ne précède le curseur.

Reste avant tout essai matériel: build sandbox (Docker bfin-elf + op1repacker + `op1_246.op1`
fourni par l'utilisateur), vérification structure/CRC de l'image, puis validation sur émulateur si
disponible — jamais de flash sans procédure de récupération documentée.

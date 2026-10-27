# Installation & récupération — firmware OP1midi (OP-1 OG)

**Image à installer :** `op1r-src/op1_246_custom-repacked.op1`
SHA-256 : `286c4124d6ffd37de182c8758f84f71ea45ffbc3e01e0b98e375e5f80417d0c4` (12 411 779 octets)
**Base :** firmware officiel TE R246 (`op1_246.op1`, SHA-256 `c5315218f825f143b415ca554516541898abee843d3a236df0b54c04e1fb13a9`)

## Avant toute manipulation

1. **Sauvegarde complète** : passe l'OP-1 en disk mode (**Shift + COM**, puis **T3**), copie **tout** le
   contenu du disque USB vers ton PC (dossiers `tape`, presets, etc.), puis éjecte proprement.
   Vérifie que les copies s'ouvrent (tailles non nulles).
2. Garde `op1_246.op1` (original) en lieu sûr : c'est ta porte de sortie.
3. Branche le chargeur : une coupure pendant l'installation est le seul vrai risque.

## Installation (comme toute mise à jour OP-1)

1. Disk mode (**Shift + COM**, puis **T3**).
2. Copie `op1_246_custom-repacked.op1` **à la racine** du disque USB.
3. Éjecte proprement (éjection logique depuis le PC).
4. L'appareil propose l'installation au démarrage (bootloader update mode) — confirme sur l'appareil.
5. Vérifie l'écran boot : il doit afficher `op1r <n>` + l'heure du build.

## Ce que cette image ajoute

- **Mapping MIDI → actions tape** (`synth_sdk/midi/mm.c`) : CC 80 = marqueur suivant,
  CC 81 = marqueur précédent, CC 82 = lecture depuis le marqueur courant (trigger, front ≥ 64).
- **Réglages** : mode clock (Shift + Help) puis **T3** — rangée verte « MIDI » (canal : OFF/1–16/ALL),
  blanche « NEXT » et rouge « PREV » (numéros de CC, 0 = --). Le CC « PLAY n » (play-from-marker)
  s'affiche en lecture seule (CC 82 par défaut).
- Tout le reste d'op1r (plugins synthé/FX, marqueurs, count-in, punch, etc.).

## Récupération (si problème)

1. Reflasher **l'original** exactement comme une mise à jour : disk mode, copier `op1_246.op1`
   à la racine, éjecter, confirmer au démarrage.
2. Si l'appareil ne démarre plus : réessayer l'installation de l'original une fois l'appareil
   éteint/redémarré (le bootloader de TE n'est jamais touché — les blocs NAND 0–3 sont protégés).
3. En dernier recours, contacter le support TE (l'original reflasable rétablit l'état stock).

## Règles de sécurité (AGENTS.md)

- Ne jamais flasher une image dont le SHA-256 ne correspond pas à celui noté ci-dessus.
- Ne jamais flasher pendant que l'appareil enregistre ou transfère.
- Toute nouvelle image (rebuild) doit repasser la validation : re-unpack op1repacker sans erreur,
  tailles cohérentes, diff contenu attendu.

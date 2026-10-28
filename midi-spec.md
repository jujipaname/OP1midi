# Spec MIDI — OP1 MIDI Mapper

## Principes
- Toutes les fonctions sont des **triggers** : valeur **≥ 64** déclenche (front montant).
- Le mute des pistes est en mode **toggle** : chaque trigger ≥64 bascule mute/unmute.
- Sources acceptées : **Note** (n° de note), **CC** (n° de control), **PC** (n° de program).
- Canal MIDI global configurable (réglage existant `mmch`).

## Destinations
| Destination | Paramètre (potard blanc) | Action |
|---|---|---|
| SOUND 1-8 | n° de la touche sound | Sélectionne le synthé monté sur la touche sound N (équivalent appui virtuel) |
| TAPE 1-4 | n° de tape | Change de tape active |
| MUTE piste | n° de piste (1-4) | Toggle mute de la piste |
| MARKER 1-124 | n° de marker global | Change de tape si nécessaire puis lance la lecture au marker |

## Mapping markers globaux
31 markers utilisés par tape (le marker 32 local n'est pas adressable globalement).

| Tape | Markers globaux |
|---|---|
| 1 | 1 – 31 |
| 2 | 32 – 62 |
| 3 | 63 – 93 |
| 4 | 94 – 124 |

Formule : `tape = (global - 1) / 31 + 1` (division entière) ; `marker_local = (global - 1) % 31 + 1`.

## Slots de mapping
- **16 slots** (4 pages × 4 slots), navigables à l'encodeur rouge.
- Chaque slot = { type source (NOTE/CC/PC), n° source, destination, n° destination }.
- Persistance en `op1.db` (clé(s) dédiées, à côté de `mmch`).
- Les CC réservés du système restent refusés comme sources (règle existante du MVP).

## Gardes (repris du MVP + ajustements)
- Actions MARKER : uniquement en mode tape, pas pendant punch/rec/count-in.
- Actions MARKER doivent fonctionner même si une piste **external CV / external audio** est armée
  (cas d'usage principal — à valider explicitement dans les tests).
- Actions TAPE : interdites pendant lecture/rec (comportement à définir : refuser ou stop d'abord).

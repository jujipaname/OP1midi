# GUI — Éditeur de mapping (encodeurs)

## Accès
Page Settings existante → naviguer jusqu'à la page MIDI MAP (extension du screens/settings.c).

## Matrice 4 encodeurs
| Encodeur | Appui (clic) | Rotation |
|---|---|---|
| **Bleu** | Cycler type source : NOTE → CC → PC | N° de la source |
| **Vert** | — | Destination : SOUND → TAPE → MUTE → MARKER |
| **Blanc** | — | N° de destination (plage selon destination) |
| **Rouge** | — | Navigation : slot (1-4) puis page (1-4) |

## Rendu écran
- 4 colonnes alignées aux couleurs des encodeurs (bleu / vert / blanc / rouge).
- Ligne du slot actif en **surbrillance**.
- Libellés courts (l'écran est petit) : `NOTE/CC/PC`, `SND/TAPE/MUT/MRK`.

```
┌ MIDI MAP  p1/4 ─────────┐
│ ►NOTE   SND   03    ▶   │
│  CC     MRK   17        │
│  PC     TAPE  02        │
│  CC     MUT   01        │
└─────────────────────────┘
```

## Conventions d'édition
- Les valeurs se modifient en direct (pas de mode "édition" séparé, pas de validation).
- Wrap sur les extrêmes (comme les CC du MVP).
- Persistance à la sortie de page (comportement déjà en place).

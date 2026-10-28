# Matrice initiale — troisième campagne adversariale

Référence figée : `c0dcb18d649a909846e8b2bcefe4eb39b1841dc9`, identique aux deux campagnes lues. Anciens dossiers préservés. Contrats : `spec/000-PRECEDENCE.md`, RFC-0001 et ADR acceptés. Une compilation réussie n'est pas un oracle sémantique ; une répétition d'une cause connue n'est pas un nouveau défaut.

| Axe | Invariant / oracle indépendant | Preuve antérieure | Attaque nouvelle | Résultat attendu avant essai |
|---|---|---|---|---|
| Ownership conditionnel | Chaque chemin exécute les destructeurs attendus une fois et dans l'ordre, ou le strict rejette une jonction non prouvée | F-01 `?:` et `&&` perdent une destruction ; faux rejet move | `?:`, `&&`, `||`, virgule, imbrication ; drop/move/adopt/borrow ; deux chemins, O0/O2/O3/LTO | Trace calculée par un modèle de chemin indépendant, comparée aux sorties du binaire et diagnostics |
| Alignement channel | `recv` typé reçoit stockage aligné pour T ; sentinelle après close, owning et select permis conservent valeurs/drops | C-01 align64 ordinaire | align32/64/128, tailles différentes, thread externe/tâche, fermé, own, select non-owning | Adresse mod align, UBSan, valeurs byte à byte, contrôles align16 et claim |
| R-01 scope | Aucun wake PAL sur adresse libérée ; conséquence fonctionnelle à distinguer | Wake tardif sur scope libéré/réutilisé, impact UNKNOWN | forcer réutilisation et waiter sur nouveau scope, observer retours futex et compteurs | Si aucun effet, impact fonctionnel reste UNKNOWN ; contrôle sans pause |
| Stats R-02 | Lecture/écriture synchronisées pour chaque champ de stats | queue_peak course C11 | inventaire tâche/scope/channel ; source des écritures ; témoin simultané ciblé | Courses prouvées au niveau source ; valeurs incohérentes seulement si observées |
| Init/arrêt | Handles publiés avant join et échecs partiels contractuels | S-01 déterministe, multistart 20/20 | S-01 contrôle 1×, état post-échec et autres transitions | Code/statuts et état observés, pas d'inférence de corruption |
| c2crug / FFI | `--safe` préserve effets ; preuve FFI valable et format conforme | S-02 macro, C2-B version99, F-02/F-03/F-04 | aliases/macros/appels indirects ; FFI contrôles grammar versus codegen | Original/migré O0/O2 + ASan ; identité objet/sortie, classification cause |
| Ressources | Fuite par thread et rétention par cache mesurées ; coût candidat comparé à témoin | C-02 16×256 KiB, A-01 ~16 MiB, c2 ~quadratique 100–800 | cycles tailles mixtes, scratch seuils, c2 800/1000/1200 bornés | Memcheck/LSan/RSS et temps CPU/mur, sans extrapolation au-delà |

Statuts finaux et zones non vérifiées seront consignés dans `RAPPORT.md`. Les scripts et résultats de cette troisième campagne restent uniquement dans ce dossier et `/tmp/crug-adversarial-3`.

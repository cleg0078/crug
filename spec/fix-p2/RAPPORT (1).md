# Troisième campagne adversariale CRUG — c0dcb18

Commit audité : `c0dcb18d649a909846e8b2bcefe4eb39b1841dc9`. Les campagnes [initiale](../2026-10-27-c0dcb18/) et [adversariale précédente](../2026-10-27-adversarial-c0dcb18/) ont été lues et laissées intactes. Cette campagne n'a modifié que ce dossier. Aucun fichier de production ni commit.

## Résultats classés par cause

| Classe | Cause et portée démontrée | Limite |
|---|---|---|
| **Défaut confirmé** | **F-01, même cause** : l'analyse d'ownership traverse séquentiellement les opérandes de `?:`, `&&`, `||` et virgule. Les branches où le `drop` ne s'exécute pas perdent le destructeur implicite ; des expressions à branches exclusives valides sont rejetées. | Aucun nouvel identifiant par forme d'expression ; effets observés uniquement sur les programmes minimaux ci-dessous. |
| **Défaut confirmé** | **C-01, même cause** : `recv` ordinaire charge un type aligné à 32/64/128 depuis un scratch qui ne garantit pas cet alignement. UBSan signale l'accès ; les valeurs vérifiées restent correctes dans ces essais. | Aucun crash matériel observé ; adresse parfois alignée par hasard. |
| **Impact fonctionnel confirmé, circonscrit** | **R-01, même cause** : un wake retardé de l'ancien scope réveille le futex d'un nouveau scope réutilisant l'adresse. Le témoin effectue un deuxième `FUTEX_WAIT_PRIVATE`. | Aucun retour anticipé de `scope_leave`, perte de réveil, erreur syscall ou corruption d'état observé ; impact applicatif au-delà du réveil parasite **UNKNOWN**. |
| **Défaut confirmé par les accès source** | **R-02 étendu** : `queue_peak` et `configured_workers` sont lus sans verrou par `crug_task_stats_get` et écrits sous verrou. Les autres champs lus par les trois API de statistiques utilisent des atomiques. | Aucune valeur invalide observée ; un effet visible de la course reste **UNKNOWN**. |
| **Impact fonctionnel confirmé** | **S-01, même cause** : deux tentatives d'arrêt pendant la création du premier worker retournent 3 ; le réveil du créateur provoque `crug-CONC-029` et `abort`. | Reproduction à entrelacement imposé ; fréquence naturelle non mesurée. |
| **Défaut confirmé** | **C-02, même cause** : le scratch TLS de `recv` par thread externe fuit à la fin des threads, pour plusieurs tailles. | La croissance RSS dépend aussi de l'allocateur ; Memcheck/LSan attribuent précisément les allocations. |
| **Suspicion / limite de ressources** | `c2crug --analyze` coûte de plus en plus cher avec 800 à 1 200 candidats ; les témoins de lignes comparables restent stables. | La forme est compatible avec les rescans source, sans preuve d'exposant asymptotique hors de l'intervalle mesuré. |
| **Écart documentaire, sans corruption démontrée** | La forme `blocking` seule est documentée comme clause de fonction, mais le parseur exige `argN` ou `ret`. `blocking ret` est accepté et ne change pas l'objet produit. | Contrat formel ambigu sur l'opérande ; défaut de cohérence documentation/implémentation, aucun effet mémoire démontré. |
| **Comportement contractuel** | `select` avec destination alignée et `recv_claim` restent propres à UBSan ; `select` d'un message `own` est rejeté par `crug-CONC-017` ; capture `go` sur-alignée est rejetée par `crug-CONC-028`. | Ces refus ne sont pas des régressions prouvées. |

Les noms F-01, C-01, R-01, R-02, S-01, C-02 et S-02 désignent les causes des campagnes antérieures. Une variante ne devient pas un nouveau défaut parce qu'elle donne un autre diagnostic.

## 1. Oracle de flot ownership

**Contrat.** Une expression conditionnelle n'exécute que ses opérandes requis. Un `drop` exécuté retire la valeur de l'ensemble à détruire ; à la sortie de portée, les autres propriétaires sont détruits dans l'ordre inverse des déclarations. Le témoin `if` vérifie la règle de contrôle ; le témoin d'emprunt vérifie l'absence de destruction anticipée. **Source.** [crug-ownership.cc](../../gcc/c/crug-ownership.cc) lignes 4248–4265 traite successivement les opérandes de `COND_EXPR`, `TRUTH_ANDIF_EXPR`, `TRUTH_ORIF_EXPR`, `COMPOUND_EXPR`.

L'[oracle Python](run_ownership.py) calcule la trace attendue par chemin sans lire le résultat du compilateur. Les [sources minimales](ternary_drop.crug.c), dont [expression imbriquée](nested_drop.crug.c), [virgule](comma_cond_drop.crug.c), [double branche](both_arms_drop.crug.c), [move](move_same.crug.c), [adopt](adopt_cond.crug.c), [emprunt](borrow_cond.crug.c) et [if témoin](if_control.crug.c), ont été compilées en strict O0/O2/O3/LTO avec le `cc1` épinglé. Les [sorties brutes](ownership_raw.json) contiennent commandes, SHA des sources et exécutables, diagnostics, codes de retour et traces de chaque chemin.

| Variante | Compilation | Chemins conformes à l'oracle | Observation |
|---|---:|---:|---|
| `?: drop` | 4/4 | 4/8 | branche sans drop : `Q`, attendu `QP` |
| `&& drop` | 4/4 | 4/8 | même divergence |
| `|| drop` | 4/4 | 4/8 | même divergence |
| virgule conditionnelle | 4/4 | 4/8 | même divergence |
| expression imbriquée | 4/4 | 4/12 | deux chemins sans drop : `Q`, attendu `QP` |
| drop dans les deux bras | 0/4 | — | rejet `crug-UAM-001` malgré un seul bras exécuté |
| move dans les deux bras | 0/4 | — | rejet `crug-UAM-002` / `crug-OWN-007` |
| adopt conditionnel | 0/4 | — | rejet `crug-OWN-007`, déjà rencontré dans la campagne précédente |
| `if` témoin | 4/4 | 8/8 | `PQ` |
| emprunt conditionnel | 4/4 | 8/8 | `P` |

Commande : `python3 audit/2026-10-27-adversarial-3-c0dcb18/run_ownership.py`. La trace de destructeurs prouve la perte de destruction dans ces cas ; elle ne chiffre pas les octets fuyants. Les rejets et acceptations incorrectes dérivent du même résumé sans chemins de l'expression, pas de causes distinctes.

## 2. Channels et alignement

**Contrat.** Un `struct Packet` valide avec `__attribute__((aligned(A)))` doit être lu à une adresse multiple de A. **Source.** [crug-chan.cc](../../gcc/c/crug-chan.cc) lignes 485–497 fabrique une lecture typée à partir du pointeur de scratch ; [chan.c](../../libcrug/core/chan.c) lignes 144–159 et [task.c](../../libcrug/core/task.c) lignes 755–766 agrandissent ce scratch par `realloc`, sans alignement spécialisé. Le buffer circulaire de `chan.c:221` est utilisé avec `memcpy`, sans lecture typée trouvée dans la surface inspectée. Le frame de capture est limité à `max_align_t` par [crug-tasks.cc](../../gcc/c/crug-tasks.cc) lignes 478–496.

La [matrice générée](run_alignment.py) couvre A=16, 32, 64, 128, les tailles de champ 3/73/77/129 octets, `recv` ordinaire, `close` puis `recv`, `select`, réception par pthread externe et par tâche, O0/O2 et `-fsanitize=alignment`. Les programmes, commandes, adresses modulo A, valeurs, diagnostics et SHA de binaires sont dans [alignment_raw.json](alignment_raw.json). Les premiers essais `select` dont le harnais avait un `break` invalide sont conservés dans [alignment_initial_raw.json](alignment_initial_raw.json) et exclus.

- Témoin A=16 : zéro diagnostic, valeurs correctes.
- Six couples taille/alignement A>16 en `recv` ordinaire : deux diagnostics UBSan par exécution O0/O2 (message puis sentinelle après fermeture), valeurs correctes.
- Les six couples en `select` : zéro diagnostic, valeurs correctes. Le chemin écrit dans une destination correctement alignée.
- Pthread externe : les six cas A>16 montrent une adresse non alignée et deux diagnostics. Tâche : A=64/128 montre deux diagnostics ; A=32 peut tomber par hasard sur une adresse alignée et ne donne alors aucun diagnostic.
- [Message owning](owned_aligned.crug.c) : pointeur sur une valeur alignée 128, `aligned_alloc`, `release` après réception et sentinelle fermée ; O0/O2 propres à UBSan, `value=77 closed=1 sizeof_payload=128 sizeof_element=8`. Le type transporté est le pointeur aligné à 8 octets. [select owning](select_owned_rejected.crug.c) est rejeté par `crug-CONC-017`, conformément au contrat. [Sorties](owned_alignment_raw.json). L'essai initial qui allouait le pointee par `malloc` était invalide et est conservé séparément.

Commandes : `python3 audit/2026-10-27-adversarial-3-c0dcb18/run_alignment.py` et `python3 audit/2026-10-27-adversarial-3-c0dcb18/run_owned_alignment.py`. UBSan constate des accès C mal alignés ; aucun crash matériel n'a été observé.

## 3. Runtime : réveil, statistiques, démarrage

**R-01.** Le [programme](scope_reuse_waiter.c) intercepte le wake, le suspend après libération de l'ancien scope, réalloue immédiatement un scope à la même adresse, bloque un waiter sur celui-ci et libère l'ancien wake. Un témoin réalise les mêmes opérations sans suspension. [Commandes, sorties et strace complets](scope_reuse_raw.json) : `python3 audit/2026-10-27-adversarial-3-c0dcb18/run_scope_reuse.py`. Quatre répétitions par mode à O0/O2, plus strace, sont toutes terminées à 0 : `reused=1 waiter_in_futex=1 early_returns=0 child_done=1 spawned=2 completed=2 scopes_live=0 lost=0`. Dans la trace bloquée O2, la nouvelle attente entre sur `0x5e14cbdf92a8`, l'ancien wake sur cette même adresse réveille un waiter (retour 1), celui-ci entre à nouveau dans `FUTEX_WAIT_PRIVATE`, puis le nouvel enfant émet le wake normal. [task.c](../../libcrug/core/task.c) lignes 804–810 peut réveiller après la décrémentation ; `scope_leave` lignes 1010–1023 libère à zéro. [pal_linux.c](../../libcrug/pal/linux/pal_linux.c) lignes 166–189 reboucle sur le prédicat, ce qui borne l'impact constaté au wake parasite et à un syscall d'attente supplémentaire. L'interception impose l'ordre mais ne crée pas la fenêtre de source. Erreur, blocage définitif, réveil prématuré à l'API ou état corrompu : **UNKNOWN / non observés**.

**R-02.** [task.c](../../libcrug/core/task.c) lignes 1158–1169 lit six compteurs atomiques `spawned/completed/yielded/lost/corrupt/live`, mais lit `configured_workers` et `queue_peak` en mémoire ordinaire. Leurs seuls écrivains trouvés sont respectivement ligne 711 dans `runtime_start` et lignes 594–595 dans `queue_push_locked`, sous verrou de file ; le lecteur n'acquiert pas ce verrou. Une lecture concurrente avec chacune de ces écritures constitue donc une course au niveau des accès source. [scope_stats_get](../../libcrug/core/task.c) lignes 1049–1056 et [chan_stats_get](../../libcrug/core/chan.c) lignes 524–533 lisent seulement des atomiques. Les instantanés multichamps ne promettent pas de cohérence globale. [Témoin de 12 cycles](stats_transition.c), O0/O2 : 319 940 et 656 556 échantillons, workers 1/4 vus, `invalid_workers=0`, `peak_changes=36/27`, compteurs finaux `spawned=completed=1212`, scopes 12/12/live0 et channels 12/12. [Sorties brutes](stats_start_raw.json). Aucun effet anormal de valeur n'est prouvé.

**S-01.** [Extension](s01_extension.c) : deux `try_shutdown` durant la première création bloquée retournent 3, `null_join=1`, puis le démarrage libéré déclenche `crug-CONC-029` et `abort` à O0/O2. Le témoin sans blocage donne `first=0 second=1 null_join=0 spawned=completed=1`. Même cause S-01 ; l'extension prouve la transition après un deuxième refus, sans estimer sa fréquence. [Commandes/sorties](stats_start_raw.json) : `python3 audit/2026-10-27-adversarial-3-c0dcb18/run_stats_start.py`. L'essai initial invalide est conservé séparément.

## 4. Migration, FFI et limites de ressources

**S-02 et contrôle de portée.** La campagne précédente a prouvé la migration dangereuse par macro, alias caché et ASan. Ici [quatre témoins](migration_controls/) utilisent un appel indirect à `free`, une expression GNU avec `return`, un nom masqué et une directive préprocesseur dans la fonction. `c2crug --safe` les garde tous `raw` ; l'original et le migré terminent à 0 en C ordinaire et sous ASan/UBSan. Ce sont des contrôles de refus conservateur, pas des preuves générales de correction. [Script et sorties, rapports, SHA des exécutables](run_migration_controls.py) / [migration_controls_raw.json](migration_controls_raw.json). L'essai préliminaire à déclarations sur une seule ligne, non reconnu par le parseur de déclarations, est archivé dans [migration_controls_initial_raw.json](migration_controls_initial_raw.json) et exclu.

**FFI.** Le [programme minimal](ffi_codegen/caller.c) et quatre sidecars testent une entrée documentaire valide `returns int`, une clause inconnue, une version future et `blocking ret`. [Script](run_ffi_codegen.py), [sorties et empreintes objet](ffi_codegen_raw.json). À O0 et O2, `baseline`, `valid_int` et `recorded_blocking` produisent des objets SHA-256 **identiques** (O0 : préfixe `77165ca84567`, O2 : `91a73553c19c`) ; `banana` donne `crug-FFI-003`, version 99 `crug-FFI-001`, sans objet. C'est cohérent avec [FFI.md](../../docs/crug/FFI.md) qui annonce aucune modification d'ABI/code pour les métadonnées enregistrées. En revanche `fn live_count returns int blocking;` est rejeté par `crug-FFI-003 expected 'argN' or 'ret'`, alors que la ligne 59 du guide décrit `blocking` comme clause `fn` autonome ; [source du parseur](../../gcc/c-family/crug-ffi.cc) lignes 385–404. Ce cas initial et ses sorties sont conservés dans [ffi_codegen_blocking_invalid_raw.json](ffi_codegen_blocking_invalid_raw.json) et [sidecar](ffi_codegen/recorded_blocking_invalid.crugffi). Il s'agit d'un écart de grammaire/documentation, sans affirmation de corruption ni d'appel généré incorrect. Le FFI invalide déjà rapporté par `c2crug` reste un défaut de validation de l'outil, pas une corruption du compilateur.

**C-02.** [Programme](tls_cycles.c) : trois cycles de threads externes, 24 threads au total, tailles 4 096, 65 536, 262 144 et 1 048 576, avec validation de chaque octet. `recv` par pointeur : RSS 2 804→7 252→9 964→12 676 KiB, Memcheck `definitely lost: 8,282,112 bytes in 24 blocks`, LSan même quantité ; `recv_claim` témoin : RSS stable autour de 5 564 KiB après le premier cycle, Memcheck zéro fuite, LSan aucune fuite. Tous les résultats fonctionnels `bad=0`. [Script et sorties intégrales](run_tls_cycles.py) / [tls_cycles_raw.json](tls_cycles_raw.json). L'option LSan `halt_on_error=0` laisse un code 0 même si un rapport de fuite est émis.

**Scratch des tâches.** [Programme](scratch_mixed.c) : six cycles à 8 tâches de 4 096, 65 536, 1 048 576, 4 096, 262 144, 4 096 octets, trois répétitions et cache tâches 0 ou 256, un worker. Avec cache 0, RSS après le grand cycle puis les petits se stabilise autour de 5,1–5,6 MiB ; avec cache 256, 11,4–11,9 MiB avant shutdown et environ 3,7 MiB après. `completed=48`, checksum correct à chaque répétition. Cela montre une rétention dépendante du cache, pas une fuite permanente prouvée. [Script/commandes/sorties/SHA](run_scratch_mixed.py) / [scratch_mixed_raw.json](scratch_mixed_raw.json).

**Coût de c2crug.** [Générateur](run_c2_scaling.py), [sources et rapports](scaling_inputs/), [sorties brutes](c2_scaling_raw.json) : deux répétitions par taille, `timeout=30s`, candidats et témoins sans candidats de nombre de lignes comparable.

| Candidats | Mur monotonic (s) | CPU user (s) | RSS max (KiB) | Témoin mur (s) |
|---:|---:|---:|---:|---:|
| 800 | 5,58 / 5,62 | 5,92 / 5,98 | 21 296 / 21 420 | 0,035 / 0,043 |
| 1 000 | 8,90 / 8,98 | 9,49 / 9,59 | 22 448 / 21 752 | 0,037 / 0,037 |
| 1 200 | 12,46 / 12,47 | 13,29 / 13,31 | 22 548 / 22 600 | 0,040 / 0,037 |

La première mesure `/usr/bin/time` à 800 annonce 8,18 s et à 1 200 14,66 s, différents du monotonic Python ; les deux sont conservées, sans leur attribuer une cause. Les 12 commandes sont terminées sans timeout. Les mesures étendent la série antérieure à 800 ; elles ne prouvent ni dépassement de ressources à une taille donnée ni loi au-delà de 1 200.

## Reproduction et bornes

Depuis la racine du dépôt, chaque `run_*.py` de ce dossier rejoue sa section et écrit son JSON brut. Les JSON enregistrent la commande exacte, les sorties stdout/stderr, les codes de retour et, pour les exécutables/objets, les SHA-256. Le compilateur isolé est `/tmp/crug-audit-c0dcb18/build/gcc/xgcc` (SHA-256 `8a0f38d8f2e95d7919409f44e7c1ecdc992a294685e25e63d967d8c641b83e7c`) avec `cc1` SHA-256 `4cbaa0ef491f6d34ec02a26a0fe6f7345b8a1b39430cb7f4a16f7d07785b70f4`. Les binaires sont sous `/tmp/crug-adversarial-3/` ou dans `migration_controls/`; leurs empreintes restent dans les preuves si `/tmp` est nettoyé. [Manifest](manifest.json) et statut Git final donnent la traçabilité du dossier.

La machine testée est Linux x86-64 sous WSL Ubuntu-CRUG ; GCC amont est hors périmètre. Les observations de valeurs correctes et les témoins qui passent ne prouvent pas la correction générale. Les seuls résultats nouveaux au niveau du *lieu de code* sont l'autre champ ordinaire de R-02 et l'écart documentaire `blocking` ; aucune nouvelle cause de corruption mémoire indépendante n'a été confirmée.


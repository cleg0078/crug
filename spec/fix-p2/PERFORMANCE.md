# Mémoire et performances CRUG — baseline et plan

**Commit :** `c0dcb18d649a909846e8b2bcefe4eb39b1841dc9`. **Machine :** WSL2 Ubuntu 24.04.5 sur AMD Ryzen 5 5600X, 12 CPU logiques, ~23 GiB RAM ; Linux x86-64. `perf`/compteurs matériels indisponibles. Les mesures du dépôt dans `docs/crug/PERFORMANCE.md` sont historiques et ne remplacent pas cette baseline.

## Protocole et portée

Les programmes `bench_runtime.c`, `bench_pipeline.c` et `scratch_retention.c` sont compilés directement avec `libcrug/core`, PAL Linux et switch SysV issus du commit. `bench/channels/chanbench.c` est reconstruit de même. GCC hôte 13.3 `-O2 -g -std=c11 -pthread` sert au runtime ; les coûts frontend utilisent le `cc1` CRUG isolé. Chaque mesure vérifie `completed`, `lost`, `stack_corrupt`, nombre de messages et checksum. 106/106 essais de microbenchmark, 44/44 essais pipeline et 12/12 essais de rétention se terminent correctement. Les suites durent respectivement ~28 s, ~1,3 s et ~2,5 s ; les commandes, paramètres et sorties de chaque essai sont dans les JSON bruts.

Durées ci-dessous = médianes de 3 ou 5 répétitions indépendantes. `spawn` couvre l’émission des 50 000 tâches ; `complete` va du début d’émission à leur achèvement ; `shutdown` est chronométré séparément après 100 ms de repos. `cpu` = temps CPU du processus entre début d’émission et fin du repos ; RSS/VM viennent de `/proc/self/status`. Les compteurs malloc/calloc/realloc/free interceptent les appels liés depuis le harnais et libcrug, pas toutes les allocations internes de libc. Le pipeline mesure le temps complet, y compris démarrage/arrêt, avec `/usr/bin/time` pour le pic RSS. Les percentiles ne sont pas robustes avec n=3/5 : les bornes min/max empiriques sont publiées pour les cas principaux, sans prétendre à un p95 fiable.

## Baseline des tâches

50 000 tâches de travail nul, frame 16 octets, pile demandée 16 KiB, caches de pile et de tâches activés, rafale ; temps en ms, mémoire en KiB.

| Workers | Répétitions | Spawn médian | Complet médian (min–max) | Shutdown médian | CPU médian | RSS repos / après arrêt | VmPeak |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 5 | 11,083 | 11,457 (11,139–11,752) | 0,133 | 24,247 | 3 268 / 3 388 | 12 244 |
| 2 | 5 | 37,823 | 37,829 (33,540–43,389) | 0,142 | 75,010 | 2 352 / 2 464 | 19 536 |
| 4 | 5 | 327,139 | 327,139 (320,363–362,925) | 0,155 | 480,909 | 2 284 / 2 380 | 35 968 |
| 8 | 5 | 323,150 | 323,150 (322,911–335,112) | 0,251 | 493,759 | 2 348 / 2 380 | 68 832 |

Cette charge minuscule est dominée par émission/ordonnancement : `spawn≈complete`. Le ralentissement à plusieurs workers et la hausse de CPU sont observés ; l’attribution précise entre verrou de queue, réveils, atomiques et ordonnanceur hôte reste une **hypothèse**, car aucun profil matériel n’est disponible. Le code montre un verrou `sched.lock` commun à l’admission et au pop (`task.c:566-615,963-976`), tandis que les tâches reprises ont une queue locale (`:618+`, `:845-855`). Les piles ne sont matérialisées qu’au dispatch (`:943-947`) : ce mécanisme existe déjà.

| Variante, 50 000 tâches | 1 worker : complet | 4 workers : complet | Observation |
|---|---:|---:|---|
| Caches désactivés (`CRUG_TASKS_CACHE=0`, `CRUG_TASKS_TASK_CACHE=0`) | 557,235 ms | 996,590 ms | ~100 002 malloc et ~150 002 free contre ~7 156/~14 309 à 1 worker avec caches ; compteurs directs seulement |
| Après échauffement interne de 5 000 tâches | 12,111 ms | 334,601 ms | Pas de gain stable sur cette charge ; ne pas extrapoler « cold vs warm » à d’autres charges |
| Flux borné à 1 024 tâches en vol | 12,190 ms | 324,130 ms | Borne d’admission n’efface pas le coût multi-worker |
| Travail grossier : 2 000 itérations arithmétiques/tâche | 88,040 ms | 23,914 ms | Parallélisme utile quand le travail amortit l’ordonnancement |
| Frame 128 octets | 11,692 ms | 367,253 ms | Variabilité et copies plus grandes ; n=3 |
| Frame 4 096 octets | 13,353 ms | 328,488 ms | À 1 worker, taille de frame visible ; à 4 workers, contention dominante |
| Pile demandée 64 KiB | 11,430 ms | 318,864 ms | Pics de VM à 4 workers : 36 160 KiB, proches du cas 16 KiB (35 968), sous caches |

Le benchmark `fine-warm` mesure 50 000 tâches **après** 5 000 tâches préalables dans le même processus ; les coûts de cet échauffement sont exclus. Le mode sans cache coupe simultanément les deux caches, donc il ne permet pas d’attribuer séparément le gain au cache de tâches ou de piles.

## Channels et charge représentative

`chanbench` reconstruit : ping-pong capacity 0, 20 000 tours, médiane d’environ 1,505 ms (1 worker) et 1,551 ms (4 workers). Débit bufferisé, 200 000 entiers, capacity 1 024 : ~2,474 ms (1 worker), ~2,711 ms (2), ~21,537 ms (4, série bruyante/bimodale), ~2,513 ms (8). Le programme interne sélectionne son **meilleur** temps parmi huit sous-essais ; la médiane externe est donc la médiane de trois « meilleurs », pas une latence de queue. La mesure mémoire à 2 048 channels, capacity 1 024, élément 4 octets, rapporte 4 096 octets de ring, 128 octets de structure et 4 382 octets RSS par channel ; cette mémoire n’est pas libérée en v1.

Charge représentative : 100 000 messages contrôlés, 4 producteurs/4 consommateurs (ou SPSC indiqué), checksums et compte exacts, tâches/fibres CRUG. Temps inclut la création des tâches, le transfert, les sentinelles et l’arrêt.

| Scénario | 1 worker, médiane (min–max) | 4 workers, médiane (min–max) | CPU 1 → 4 workers |
|---|---:|---:|---:|
| MPMC, 256 octets, capacité 64 (5 répétitions) | 2,933 ms (2,738–2,945) | 34,165 ms (30,068–36,460) | 3,062 → 164,813 ms |
| MPMC, 256 octets, capacité 1 024 (3) | 3,088 ms (2,836–3,407) | 35,885 ms (30,251–35,950) | 3,221 → 177,499 ms |
| MPMC, 256 octets, rendez-vous 0 (3) | 18,865 ms (18,247–23,112) | 71,911 ms (56,105–78,010) | 37,918 → 344,359 ms |
| MPMC, 4 096 octets, capacité 64 (3) | 23,586 ms (21,467–24,994) | 54,960 ms (51,885–61,053) | 48,385 → 278,390 ms |
| SPSC, 256 octets, capacité 64 (3) | 2,500 ms (2,452–2,642) | 22,380 ms (15,879–23,852) | 2,615 → 66,984 ms |

Les 5 répétitions MPMC 256/cap64 à 2/8 workers donnent respectivement 18,611 ms (12,619–20,108) et 44,733 ms (36,648–65,472) ; la dispersion, surtout à 8 workers, interdit une extrapolation fine. L’augmentation du CPU avec plusieurs workers et le code de channel (`chan.c:241-270,294-337`) motivent l’hypothèse de contention/spin-yield ; le benchmark ne sépare pas encore le coût du verrou de celui du scheduler WSL. Le ring copie à l’envoi et à la réception ; `chan_recv_common` remet en plus le scratch à zéro **avant chaque tentative** (`:317`) même si la réception réussit (`:322`). Cette observation est une piste mesurable, pas un gain déjà chiffré.

## Mémoire retenue après repos

Le harnais `scratch_retention.c` lance 16 receivers de messages de 1 MiB sur un channel synchrone, attend la fin, relève le RSS après repos puis après shutdown ; 1/4 workers × cache 0/256 × 3 répétitions = 12/12 contrôlées. Médianes arrondies : cache 256, RSS repos ~20 520–20 564 KiB ; cache 0, ~4 080–4 116 KiB ; après shutdown, ~4 072–4 116 KiB. L’écart proche de 16 MiB vient des `recv_scratch` de chaque tâche conservés par le cache (`task.c:513-561,753-768`), sans budget d’octets propre. Les octets du ring, le runtime C et l’allocateur libc contribuent au niveau de base ; la mesure n’est pas un profil de heap exact. `scratch_raw.json` contient les douze valeurs.

Les caches de tâches par classe (`task.c:200-259`) et les caches de piles locaux/globaux (`:263-465`) sont déjà présents et bénéfiques face au mode sans cache. La taille de pile est réservée au dispatch, non à chaque tâche en attente. Le pic VM augmente avec le nombre de workers et la réservation de stacks ; RSS et VM ne sont donc pas interchangeables. Le nombre d’objets en cache ne borne pas les octets de scratch qu’ils retiennent. La fragmentation et le faux partage n’ont pas été mesurés directement.

## Coût du compilateur et de l’abaissement

`compile_bench.c` généré avec 12 000 fonctions C identiques, `xgcc -fsyntax-only`, cinq répétitions mesurées après un échauffement : sans options CRUG, O0 0,36 s et O2 0,36 s ; avec `-fcrug-ownership -fcrug-borrow`, O0 0,47 s et O2 0,43 s. RSS maximal médian environ 71,8 MiB (O0) et 72,2 MiB (O2), avec écarts modestes et non stables entre modes. Il s’agit de l’effet des options sur **ce C ordinaire** ; ce n’est pas une décomposition de chaque passe ni le coût d’un programme CRUG riche. `compile_bench_raw.json` et `compile_bench_summary.json` donnent sorties, commandes et répétitions. Les 24 exécutions O0/O2/O3 et quatre LTO prouvent le comportement de huit abaissements sélectionnés, pas leur coût temporel isolé. Une campagne `-ftime-report`/profils sur code CRUG réel reste ouverte avant d’optimiser le frontend.

## Plan d’optimisation priorisé

Aucun gain futur n’est chiffré comme acquis. Les niveaux « rapide » désignent une taille de changement probable, sous réserve de tests ; les risques et preuves requis priment sur le débit.

| Priorité / localisation | Coût observé ou hypothèse | Mécanisme proposé et gain | Mémoire, sûreté, contrat/ABI, complexité | Validation exigée |
|---|---|---|---|---|
| **P1 mémoire, rapide** — `task.c:513-561`, scratch | ~16 MiB de RSS retenus par 16 receivers de 1 MiB ; budget en nombre de tâches uniquement | À l’insertion en cache, libérer ou plafonner séparément le scratch au-dessus d’un seuil en octets. Gain mémoire **estimé**, à mesurer ; peut coûter un `realloc` au prochain gros receive. | N’ajoute pas d’API/ABI si le pointeur de scratch n’est plus accessible après fin de tâche, à vérifier ; aucun partage entre tâches ; complexité faible à moyenne. Ne pas supprimer le scratch par tâche (K-007/K-008). | Rejouer `scratch_retention`, messages 16 B/256 B/1 MiB, cache on/off, ASan et réutilisation/yield. |
| **P1 débit, rapide** — `chan.c:317-335` | `memset` de la taille entière avant chaque tentative de recv, suivi de `memcpy` au succès ; hypothèse de coût pour messages 4 KiB et attente | Zéro seulement sur sortie « fermé et vide », tout en conservant l’invariant de retour fermé ; gain de cycles **non mesuré**. | Mémoire inchangée ; attention aux races close/claim, aucune sortie ne doit exposer un scratch non initialisé ; API/ABI inchangées ; complexité faible. | A/B 256 B/4 KiB, synch/buffer, close/drain, select/claim et sanitizers. |
| **P1 structurel** — queue globale `task.c:566-615,963-976` | 50k tâches nulles : complet 11,46 ms à 1 worker, 327,14 ms à 4 ; verrou partagé visible, attribution exacte encore hypothétique | Tester admission/pops en lots ou queues de nouveaux travaux réparties par worker, avec réveil ciblé. Gain **inconnu** avant profil/A-B. | Peut augmenter la mémoire par worker et changer ordre/équité ; préserver admission atomique contre shutdown, pinning des reprises (K-001), scopes et `live`. Pas d’API souhaitée, forte complexité. | Profil WSL+machine native, tâches fines/grossières, burst/bounded, 1/2/4/8, shutdown/startup et modèle d’interleaving S-01. |
| **P1 structurel** — attente channel `chan.c:241-270,294-337`, `chan_idle` | MPMC 256/cap64 : 2,93 ms/3,06 ms CPU à 1 worker contre 34,17 ms/164,81 ms CPU à 4 ; contention et yield suspectés | Étudier attente/notification ciblée par channel et backpressure, plutôt que polling complet ; gain **inconnu**. | Files de waiters, mémoire et races close/send/recv supplémentaires ; garder rendez-vous ticket, select claims et transfert owning ; ABI à éviter, complexité élevée. | MPMC/SPSC 0/64/1024, fermeture et select adversariaux, CPU/latence tail, contrôle des réveils perdus. |
| **P2 exploratoire** — cache de têtes de tâches `task.c:470-534` | Mode sans les deux caches : 557 ms contre 11,46 ms à 1 worker ; le cache actuel global fonctionne déjà. Son verrou peut contribuer à la contention. | Comparer petite réserve locale par worker pour les en-têtes, avec budget d’octets global et restitution au repos ; gain **inconnu**. | Risque de rétention et déséquilibre ; tâches initialisées sur thread externe, recyclées sur worker ; préserver tailles de classes et scratch ; API/ABI inchangées, complexité moyenne/haute. | A/B seul cache de tâches vs seul cache de piles, frames 16/128/4096, RSS/VM repos et pic, 1/2/4/8 workers. |
| **P2 exploratoire** — frontend CRUG | +0,07 à +0,11 s sur 12 000 fonctions C ordinaires sous `-fsyntax-only` ; cause exacte non isolée | Profiler passes et maps du frontend avant de choisir regroupement d’analyses ou mémoïsation ; aucun gain estimé. | Risque de changer diagnostics ou preuves d’alias ; pas d’API/ABI, complexité inconnue ; ne jamais affaiblir un rejet de sûreté. | `-ftime-report`, fichiers CRUG représentatifs, mêmes diagnostics, O0/O2/O3/LTO, RSS et temps CPU. |

**Pistes écartées à ce stade.** Augmenter aveuglément les limites des caches gagnerait parfois des allocations mais aggraverait A-01 et la mémoire après repos. Supprimer le pinning des tâches pour équilibrer la charge réintroduirait le défaut K-001 et demande un nouveau contrat et une preuve sur `_Thread_local`, piles et contexte. Ajouter `chan_free` sans contrat d’exclusivité/absence de waiters, de valeurs owning en ring et de sélections actives serait dangereux ; l’API et les preuves de lifetime doivent précéder tout benchmark. Affaiblir l’exclusivité borrow/`restrict` pour améliorer le code généré est exclu sans preuve sémantique. Les pools/slabs massifs ou un regroupement global des frames/piles n’ont pas de mesure d’avantage indépendante des caches existants ; ils restent des hypothèses, pas des recommandations de mise en production.

## Reproduction et limites

Dans `/home/matt/src/crug`, exécuter les scripts `audit/2026-10-27-c0dcb18/run_perf.py`, `run_pipeline.py`, `run_scratch.py`, `run_compile_bench.py` après la reconstruction et la compilation des harnais documentées par `MANIFESTE.md`. Tous écrivent dans ce dossier des JSON bruts avec commandes, environnement, valeurs et codes de sortie. Les mesures WSL subissent l’ordonnancement de l’hôte, les caches et la granularité de `/usr/bin/time` ; elles hiérarchisent les coûts de cette machine, pas les performances promises sur matériel natif. Ni p95/p99 ni compteurs de contention/cache misses n’ont été établis. Aucune optimisation de production n’a été appliquée.
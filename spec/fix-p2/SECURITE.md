# Audit de sécurité des mécanismes CRUG

**Référence unique :** `c0dcb18d649a909846e8b2bcefe4eb39b1841dc9` (27 octobre 2026). **Portée de la conclusion :** Linux x86-64, GCC/cc1 reconstruit hors dépôt et échantillons bornés. Cet audit **n’est pas exhaustif**. Les contrats sont lus selon `spec/000-PRECEDENCE.md` : RFC-0001, ADR acceptés, master spec ; la roadmap règle les processus, non la sémantique. `spec/fix-p1/`, `KNOWN-ISSUES.md` et les rapports anciens ont servi de pistes et non de preuve.

## Synthèse classée

| Niveau | Constat | Preuve | Conséquence |
|---|---|---|---|
| P1, confirmé | **S-01** : arrêt pendant le démarrage du runtime joint un handle de worker non publié | Interleaving imposé, contrôle négatif, ASan/UBSan et Valgrind ; `status=3 null_join=1` | Runtime bloqué en état `stopping`, arrêt non terminé ; déni de service ou échec fatal du destructeur. Aucune corruption mémoire démontrée. |
| P2, confirmé | **S-02** : `c2crug --safe` traite un retour anticipé caché dans une macro comme un chemin direct | Diff sémantique O0/O2, original `frees=1`, migré `frees=2` | Conversion « prouvée » invalide, libération supplémentaire et changement de durée de vie ; impact d’un alias externe dépendant du programme. |
| Risque d’architecture, mesuré | **A-01** : scratch de réception volumineux conservé par le cache de tâches | 16 tâches × 1 MiB : RSS après repos ≈20,5 MiB avec cache, ≈4,1 MiB sans cache | Pression mémoire persistante jusqu’à réutilisation ou arrêt ; pas de violation démontrée du contrat. |
| Limitation contractuelle, évaluée | **L-01** : les channels créés ne sont pas libérés en v1 ; absence de timeout/cancellation | `libcrug/core/chan.c:18,208-229`, `docs/crug/CHANNELS.md` §limitations | Charge à création répétée ou attente sans partenaire peut épuiser mémoire/temps ; comportement annoncé. |

Les autres cas ciblés ont passé ; ce résultat ne prouve pas l’absence de défaut dans les chemins non exercés. Ne pas assimiler une erreur/ICE du compilateur à une vulnérabilité du binaire généré.

## Matrice de couverture

`E` = code exécuté avec une assertion pertinente ; `R` = revue ciblée du code/contrat ; `O` = partie ouverte. Une ligne `E` ne désigne jamais une preuve exhaustive.

| Mécanisme, invariant et emplacement | Preuves faites | Ouverture explicite |
|---|---|---|
| Ownership, move, états vides, réaffectation, destructions sur retour/branche/agrégat — `gcc/c/crug-ownership.cc`, `gcc/c/c-decl.cc` | E : 24/24 exécutions O0/O2/O3 sur huit cas, dont exactement une libération, move, retour branché, enum owning ; 6/6 sous ASan/UBSan ; R des chemins d’abaissement | O : CFG complexes, goto/non-local, agrégats imbriqués, toutes variantes de destructeurs et sorties ; corpus DejaGNU entier non lancé |
| Borrow et alias, durée de vie, jonctions, échappement, appels indirects — `crug-ownership.cc`, diagnostics, intégration GCC | E : K-016 à K-020 et K-017, alias sous LTO ; R du modèle de jonction | O : boucles/goto et preuve exhaustive de `restrict` sur tous chemins |
| Strict/hybride, unsafe/raw, conversions, appels indirects — `crug-safe.cc`, `c.opt`, `c-opts.cc`, parseur | E : raw deref strict rejeté, hybride accepté, conversion entier/pointeur rejetée, strict drop exécuté ; R des drapeaux | O : combinaison complète d’unsafe, asm, setjmp/longjmp, frontières FFI dans tous modes |
| FFI `.crugffi`, désignation de destructeur, indices, découverte de chemins — `gcc/c-family/crug-ffi.cc`, options et include callback | E : K-021/K-022, 200 entrées fuzz FFI sans crash ; R du parseur, limite de déclarations et découverte | O : sidecars volumineux, collisions de chemins, symlinks, toutes signatures/callbacks et plateformes |
| Slices/bornes/tailles/alignements, option/result, enum à charge utile, match — `gcc/c/crug-safe.cc`, parseur, codegen | E : slice/option/result/enum O0/O2/O3 ; rejet arité slice et match non exhaustif ; frame alignment runtime | O : tailles adversariales à tous opérateurs, représentation sur autres ABI, exhaustivité des variantes |
| Captures Send/Sync et scopes — `crug-send-sync.cc`, `crug-tasks.cc`, `task.c` | E : capture scalaire/own acceptée, raw/borrow non Sync rejetés ; suite `tasks_main` 1/2/4/8 workers ; R scopes | O : graphes de types/captures complexes, affinité réelle multi-OS |
| Ordonnancement, queues, yield, atomiques, réveils, arrêt/redémarrage — `task.c`, PAL Linux | E : 44/44 régressions runtime, K-013/K-014, interleaving S-01 ; R du verrou global et admission | O : toutes courses concurrentes, preuve formelle des ordres mémoire, TSan indisponible sous cette WSL |
| Channels synch/buffer, close/drain, select/claims, ownership, scratch — `chan.c`, `crug-chan.cc` | E : ticket, claim, select, handoff, scratch, compilation select owning ; 44/44 runtime ; 44/44 pipeline | O : entrelacements complets MPMC, fermeture sous toutes formes de select, valgrind sur chaque chemin |
| Frames/piles/pages de garde/changement de contexte/caches/OOM — `task.c`, `libcrug/arch/x86_64/sysv`, PAL Linux | E : alignment, burst, cache/restart, scratch ; R checks d’overflow `task.c:927-937`, pile différée `943-947` | O : faute de page de garde réelle, injection systématique d’échecs d’allocation, Windows/AArch64/Darwin |
| Intégrations GCC héritées : parseur, déclarations, options, type, codegen | E : 20 régressions frontend sélectionnées, 800 entrées fuzz, 24 exécutions, 4 LTO ; R des points de raccord principaux | O : 23 fichiers du delta ne sont pas tous relus ligne par ligne ; pas d’audit général GCC amont |
| Lanceur `gcc/crugc`, `c2crug`, report JSON | R : résolution `-B`, priorité du driver et `CRUGC_REAL_DRIVER` ; E : 73 checks c2crug et S-02 | O : chemins installés sous préfixes arbitraires et toutes constructions préprocesseur |
| Scripts, gates, CI, packaging | E : auto-tests `gate.sh`, archives (7 cas) et CI ; R de `package.sh`/lanceur et validation des sorties | O : package complet, installation `.deb`, reproduction des gates release et signatures non testées |
| PAL Windows/Darwin/AArch64, ABI et contexte hors Linux x86-64 | R : répertoires et contrats repérés ; pas d’exécution locale | O : compilation/exécution sur plateformes ou émulateurs requis ; K-001 reste partiel |

## S-01 — publication incomplète au démarrage du runtime

**Niveau : P1 (disponibilité), preuve : confirmé** sur le commit ci-dessus. Contrat : `libcrug/core/task.h:66-76` annonce un arrêt contrôlé, idempotent et sûr depuis plusieurs threads. Un arrêt valide ne doit pas tenter de joindre un worker dont le handle n’existe pas et abandonner le runtime dans un état irréversible.

**Cause et conditions.** `runtime_start` publie `sched.worker_count` et `sched.started=1` sous `queue_lock`, puis relâche ce verrou (`libcrug/core/task.c:703-719`). Les créations de threads ne commencent qu’à `:721-725`. Un thread externe qui appelle `crug_task_runtime_try_shutdown` dans cette fenêtre lit `started`, devient propriétaire de l’arrêt et joint `sched.threads[i]` (`:1073-1109`). Le handle vaut encore `NULL`. L’échec de join fixe `stop_failed` et laisse `stopping`/`started` en place (`:1109-1117`). Le wrapper void peut alors produire un diagnostic fatal ; les futurs spawns sont refusés.

**Reproduction minimale.** `startup_shutdown_race.c` intercepte `crug_pal_thread_create` avant l’écriture du premier handle et suspend le thread de spawn. Le thread principal déclenche l’arrêt. Générer les exécutables avec `python3 audit/2026-10-27-c0dcb18/build_race.py`, puis lancer `python3 audit/2026-10-27-c0dcb18/run_race_tools.py`. Contrôle sans blocage : `block=0 status=0 null_join=0`. Entrelacement imposé : `block=1 status=3 null_join=1`. `status=3` est le code « join failed », pas une réussite. ASan/UBSan reproduit le résultat ; Memcheck ne détecte aucune erreur mémoire certaine (un thread volontairement bloqué conserve son TLS et produit 288 octets « possibly lost »). TSan échoue avant le test sous WSL avec `FATAL: ThreadSanitizer: unexpected memory mapping`; il ne réfute ni ne confirme la race. Résultats et commandes : `race_build_raw.json`, `race_tools_raw.json`.

**Direction de correction.** Introduire un état distinct « initialisation en cours/handles publiés », synchroniser shutdown avec la fin de création sans retenir le verrou pendant un `join`, puis tester création partielle, échec de création et plusieurs callers simultanés. Garder l’admission atomique de K-014 et la sémantique d’arrêt de K-013. La correction devra prouver qu’aucun handle incomplet n’est joint et qu’aucun worker créé n’est abandonné. Pas de correctif appliqué ici.

## S-02 — macro qui masque une sortie anticipée dans `c2crug --safe`

**Niveau : P2 (transformation sémantique), preuve : confirmé.** Contrat local : l’en-tête de `c2crug` et `scripts/check-c2crug-proofs.sh` conditionnent `--safe` à une libération unique sur chaque chemin atteignable. L’outil prétend ici à `confidence=1.00`, « straight-line path » (`c2crug-macro-report.json`). Il ne prétend pas certifier la sûreté générale du programme, mais cette preuve structurale particulière est fausse.

**Cause.** `_EXIT_RE` reconnaît les mots `return/goto/break/continue` et les appels de sortie dans les lignes visibles (`c2crug:354-356`). Le balayage avant `free(p)` (`:930-985`) ne développe pas `EARLY(c)` : la macro `#define EARLY(c) do { if (c) return 7; } while (0)` cache un retour qui saute la libération. `--safe` transforme pourtant `int *p` en `own int *p` et `free(p)` en `drop(p)`. Le destructeur scope d’`own` libère alors aussi sur le retour anticipé.

**Reproduction.** Source et compteur instrumenté : `c2crug-macro/macro_exit.c`, `wrapper.c`. `python3 audit/2026-10-27-c0dcb18/run_macro_repro.py` appelle réellement `c2crug --safe`, compile l’original avec GCC hôte et le migré avec cc1 isolé, puis exécute aux niveaux O0 et O2 avec `-fno-builtin-free -fno-builtin-malloc` pour préserver l’observation des appels. Attendu d’une migration sémantiquement équivalente : une libération dans les deux programmes. Observé : original `frees=1`, migré `frees=2` aux deux niveaux, sorties de programme identiques (code 0). `macro_repro_raw.json` contient toutes les commandes et sorties. Une optimisation O2 sans désactivation des builtins peut éliminer l’allocation dans les deux programmes : ce n’est pas une preuve de correction, d’où l’instrumentation.

**Lien avec les anciens constats.** K-004/K-005 et les 73 tests actuels couvrent les branches et gardes visibles lexicalement ; cette entrée est une variante de la même famille de preuve du flux de contrôle, **pas** un nouveau défaut indépendant par syntaxe. Elle exige un traitement au niveau de la cause : prétraitement/CFG fiable ou refus conservateur des macros/appels opaques entre allocation et libération, puis tests de non-régression macro et préprocesseur.

## Risques et limites sans violation contractuelle démontrée

- **A-01, cache/scratch :** `task_free` remet le `struct crug_task` entier en cache avant `free(task->recv_scratch)` (`task.c:513-534`) ; celui-ci n’est libéré qu’en éviction ou flush (`:538-561`). Le cache borne un **nombre de tâches par classe**, pas les octets de scratch. Un test de 16 receivers de 1 MiB, 1/4 workers, trois répétitions, conserve ~16 MiB de RSS au repos par rapport à `CRUG_TASKS_TASK_CACHE=0`, puis revient à ~4 MiB après shutdown. `scratch_raw.json`. C’est un risque de disponibilité si le programme traite des messages volumineux transitoires ; aucun use-after-free n’a été constaté. Voir le plan mémoire.
- **L-01, channels :** `chan.c:18` et `docs/crug/CHANNELS.md` §limitations annoncent la conservation des objets jusqu’à la fin du processus. `chan_make` alloue l’en-tête et le ring (`:208-229`) ; la charge de 2048 channels de capacité 1024 et d’éléments 4 octets a coûté ~4 224 octets demandés à l’allocateur et ~4 382 octets RSS par channel dans l’essai. Une application qui crée continuellement des channels ou attend sans partenaire doit prévoir cette limite ; il faut un changement de contrat et une preuve de l’absence de waiters, messages owning et sélections actives pour introduire une libération.
- Les slices ne garantissent que l’index contre la longueur fournie ; forger une longueur erronée hors garanties raw/unsafe demeure une responsabilité du programme. Aucun accès hors bornes par une slice valide n’a été démontré. Les diagnostics strict/hybride ciblés passent.

## Revalidation du registre K-001 à K-023

`Passe` signifie uniquement que le témoin indiqué passe sur ce commit et cet environnement ; `partiel` signale une preuve plateforme ou sémantique manquante.

| Ancien ID | Revalidation sur ce commit | Statut de cet audit |
|---|---|---|
| K-001 TLS après migration | `tasks_main` 1/2/4/8 workers passe ; pas de Windows/AArch64 | Partiel, autres PAL ouverts |
| K-002 parse error → ICE | `error-mark-body` sans ICE ; fuzz 800 entrées/0 crash | Passe sur échantillon |
| K-003 rendez-vous ticket | `chan_ticket` 1/2/4/8 | Passe |
| K-004 free conditionnel | 73 checks c2crug passent ; macro S-02 contourne la preuve lexicale | Témoin passe, famille de preuve rouverte |
| K-005 dominance du null guard | 73 checks c2crug dont deux gardes ; S-02 est une autre opacité | Témoin passe, même réserve |
| K-006 shutdown depuis tâche | `shutdown_status` 1/2/4/8 et fake spawn `status=2` | Passe |
| K-007 scratch après migration | `recv_scratch` 1/2/4/8 | Passe |
| K-008 scratch partagé sur un worker | même témoin et mesure de rétention dédiée | Passe pour aliasing ; rétention A-01 |
| K-009 select claim | `chan_select` et `select-claim.c` | Passe |
| K-010 select owning | `select-claim-owned.c` compilé, diagnostic conservateur attendu | Partiel : transfert dynamique non rejoué |
| K-011 CHECK jamais exécuté | `gate.sh --self-test` : 4 checks, 3 échecs attendus | Passe |
| K-012 archive incomplète | `check-gate-archives.sh --self-test` : 7 cas | Passe |
| K-013 double shutdown | `shutdown_concurrent` 1/2/4/8 ; S-01 concerne init simultanée | Passe pour ancien scénario ; S-01 nouveau |
| K-014 spawn pendant shutdown | fake PAL : abort `crug-CONC-029` attendu | Passe |
| K-015 alignment de frame | `frame_alignment` 1/2/4/8 | Passe |
| K-016 borrow vers global | diagnostic `crug-BORROW-018` | Passe |
| K-017 jonction borrow switch | diagnostic `crug-BORROW-002` | Passe |
| K-018 provenance conditionnelle | diagnostic `crug-BORROW-024` | Passe |
| K-019 chaîne de copies alias | diagnostic `crug-BORROW-024` | Passe |
| K-020 appel indirect | diagnostic `crug-BORROW-020` | Passe |
| K-021 destructeur FFI arité | diagnostic `crug-FFI-011` | Passe |
| K-022 argref FFI malformé | diagnostic `crug-FFI-003` | Passe |
| K-023 lookahead parseur | diagnostic `crug-TYPE-002`, sans crash | Passe |

Preuves détaillées : `frontend_regressions.json` (20/20 cas), `runtime_raw.json` (44/44 exécutions et 11 compilations), `tasks_main_raw.json` (4/4), `fake_spawn_raw.json`, `gate_selftests.log`, `c2crug_tests.log`, `fuzz.log`. Les suites n’ont pas été extrapolées au corpus complet.

## Campagnes et limites des outils

- Fuzzing borné : `fuzz/compiler/fuzz.py --check --budget 200 --seed 20261027` via le wrapper isolé ; 200 entrées chacune pour parser, ownership, borrow, FFI ; 800 entrées, 0 crash/timeout, environ 6,2 s. Il s’agit de cas générés, pas d’une preuve des états inatteignables.
- Exécution compilée : 24/24 O0/O2/O3 sur huit programmes ; 4/4 sous LTO O2 avec xgcc isolé et bibliothèques cible reconstruites ; 6/6 sous ASan/UBSan O0/O2 pour trois programmes. Les piles de fibres/context switches peuvent échapper aux outils ou créer des alertes instrumentales ; les tests sanitizer de codegen sélectionnés ne traversent pas tous les chemins de fibres.
- Interleaving S-01 : déterministe grâce au wrapper de création, non un stress probabiliste. TSan ne démarre pas dans cette WSL. Memcheck sur le harnais bloqué laisse un TLS « possibly lost » dû au thread intentionnellement non joint ; aucune « definitely lost » ni erreur d’accès mémoire. Il ne peut pas certifier le protocole concurrent.
- Les résultats positifs ne qualifient ni les PAL non locaux ni tous les modes de build/optimisation. L’ensemble des zones ouvertes dans la matrice doit rester ouvert avant une revendication de sûreté complète.
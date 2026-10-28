# Revalidation des constats précédents

Référence : `c0dcb18d649a909846e8b2bcefe4eb39b1841dc9`, identique à l'audit `2026-10-27-c0dcb18`. L'ancien dossier a été lu comme preuve, puis contrôlé par `validate_audit.py` (`previous_evidence_validation.log`). Tous les rejouages écrivent dans la nouvelle campagne ou `/tmp`. « Passe » qualifie uniquement le témoin indiqué.

## Défauts et limites connus

| ID | Nouvelle preuve | Verdict |
|---|---|---|
| S-01 | Binaire historique SHA vérifié ; `replay_previous_s01.py` : contrôle 3/3 `status=0 null_join=0`, entrelacement bloqué 3/3 `status=3 null_join=1`. Nouveau multistart 20/20. | Défaut d'arrêt toujours confirmé ; multistart explore un autre entrelacement et ne le réfute pas. |
| S-02 | 73/73 tests ordinaires ; macros imbriquées, `free` redéfini et alias caché dans `c2crug/raw.json` ; ASan prouve le `heap-use-after-free` du cas alias migré à O0/O2 (`hidden_alias_asan_raw.json`). | Même cause confirmée avec impact de durée de vie démontré ; variantes regroupées. |
| A-01 | Caches séparés (`channels_perf/performance_raw.json`) et cycles (`runtime/cycles_scope_raw.json`). | Revalidation partielle : le témoin exact de 16 scratch de 1 MiB n'a pas été relancé ; aucun nouveau verdict sur la borne d'octets. |
| L-01 | `chanbench` reconstruit : ~4 224 octets alloués par channel de capacité 1 024, éléments de 4 octets. | Limite contractuelle inchangée, non comptée comme nouveau défaut. |

## Registre K-001 à K-023

Les résultats nouveaux sont conservés dans `previous_frontend_replay_raw.json` (20/20), `previous_runtime_replay_raw.json` (11 compilations et 44/44 exécutions), `previous_tasks_main_replay_raw.json` (1/2/4/8 workers), `previous_fake_spawn_replay_raw.json`, `c2crug/c2crug_suite.log` (73/73) et `tooling_replay.log` (auto-tests gate, archives et CI).

| ID et sujet | Témoin rejoué | État borné |
|---|---|---|
| K-001 TLS après migration | `tasks_main` 1/2/4/8 | Passe Linux x86-64 ; autres PAL UNKNOWN |
| K-002 parse error puis ICE | `error-mark-body.c` | Passe |
| K-003 rendez-vous ticket | `chan_ticket` 1/2/4/8 | Passe |
| K-004 `free` conditionnel | suite `c2crug` | Témoin passe ; famille macro S-02 reste ouverte |
| K-005 dominance du garde null | suite `c2crug` | Témoin passe ; préprocesseur opaque ouvert |
| K-006 shutdown depuis tâche | `shutdown_status` 1/2/4/8, fake spawn | Passe |
| K-007 scratch après migration | `recv_scratch` 1/2/4/8 | Passe ; C-01 distinct |
| K-008 scratch partagé sur un worker | `recv_scratch` 1/2/4/8 | Passe pour cet aliasing ; C-02 distinct |
| K-009 claim de `select` | `chan_select` 1/2/4/8 et `select-claim.c` | Passe sur ces témoins |
| K-010 transfert owning dans `select` | diagnostic `select-claim-owned.c` | Partiel : pas de transfert dynamique rejoué |
| K-011 CHECK non exécuté | `gate.sh --self-test`, 4 checks | Passe |
| K-012 archive incomplète | `check-gate-archives.sh --self-test`, 7 cas | Passe |
| K-013 double shutdown | `shutdown_concurrent` 1/2/4/8 | Passe pour cet ancien scénario ; S-01 reste |
| K-014 spawn pendant shutdown | fake PAL : abort attendu `crug-CONC-029` | Passe |
| K-015 alignement de frame | `frame_alignment` 1/2/4/8 | Passe ; C-01 concerne recv |
| K-016 emprunt vers global | `borrow-into-global.c` : `crug-BORROW-018` | Passe |
| K-017 jonction borrow switch | `join-switch.c` : `crug-BORROW-002` | Passe |
| K-018 provenance conditionnelle | `alias/conditional.c` : `crug-BORROW-024` | Passe |
| K-019 chaîne de copies alias | `alias/copy-chain.c` : `crug-BORROW-024` | Passe |
| K-020 appel indirect | `alias/indirect-call.c` : `crug-BORROW-020` | Passe |
| K-021 destructeur FFI arité | `ffi/phase9/bad/dtor-arity.c` : `crug-FFI-011` | Passe |
| K-022 argref FFI malformé | `ffi/phase9/bad/argref-malformed.c` : `crug-FFI-003` | Passe |
| K-023 lookahead parseur | `qualifier-lookahead-5.c` : `crug-TYPE-002` sans ICE | Passe |

Les anciens témoins revalidés n'établissent pas la sûreté des nouvelles combinaisons. Le dossier précédent et ses builds restent intacts. Les PAL Windows/Darwin/AArch64, l'installation et l'ensemble DejaGNU restent UNKNOWN.

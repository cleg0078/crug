# Manifeste et commandes de rejeu — campagne adversariale

## Référence, état et environnement

- Racine : `/home/matt/src/crug` dans Ubuntu-CRUG ; commit `c0dcb18d649a909846e8b2bcefe4eb39b1841dc9`, **identique** au précédent audit. Référence GCC amont `releases/gcc-16.2` → `073f91637dec82017536643f3ba69ecdd9dce6b1`. Aucun delta de commit à expliquer.
- `git diff --name-only` reste vide pour les fichiers suivis ; `git status --porcelain=v1` ne montre que `?? audit/`. L'ancien dossier `audit/2026-10-27-c0dcb18/`, ses sources et journaux n'ont pas été écrasés. Aucune branche, commit, publication, optimisation ni correction de production.
- Contrat : `spec/000-PRECEDENCE.md` ; RFC-0001, ADR acceptés, master spec. Aucun `AGENTS.md` applicable trouvé dans les ancêtres du dépôt. Les anciens rapports ont été lus comme indices, et leurs preuves contrôlées par le validateur historique.
- Machine exécutée : Ubuntu-CRUG sous WSL2, Linux x86-64, GCC hôte 13.3.0, Clang 18.1.3 et Valgrind 3.22.0. `manifest.json` enregistre `uname`, `lscpu`, versions et empreintes exactes. Les temps dépendent de l'hôte WSL et des autres processus.

La table `COUVERTURE.md` recense les familles de fichiers CRUG et les raccords GCC hérités, avec les essais exécutés et les zones UNKNOWN.

## Identité des binaires et des sources

Le compilateur a été reconstruit **hors dépôt** lors de la campagne précédente depuis ce même commit, avec configuration et journaux conservés dans l'ancien dossier. Son `cc1` à `/tmp/crug-audit-c0dcb18/build/gcc/cc1` a SHA-256 `4cbaa0ef491f6d34ec02a26a0fe6f7345b8a1b39430cb7f4a16f7d07785b70f4` ; `xgcc` au même endroit a `8a0f38d8f2e95d7919409f44e7c1ecdc992a294685e25e63d967d8c641b83e7c`. Le contrôle final compare ces valeurs au `manifest.json` précédent, au commit courant et aux sources suivies. Aucun binaire historique `build/obj` n'a servi aux verdicts.

Les harnais runtime/channels lient directement `libcrug/core`, `libcrug/pal/linux` et `libcrug/arch/x86_64/sysv` du commit ; ils construisent leurs exécutables sous `/tmp/crug-adversarial-runtime` et `/tmp/crug-adversarial-channels`. Leurs JSON conservent les commandes GCC, paramètres, code retour, stdout/stderr et SHA des sources/binaires. Les manifestes détaillés sont `runtime/manifest.json`, `frontend/manifest_frontend.json`, `channels_perf/binary_manifest.json`. Le nouveau `chanbench` réellement mesuré est `138d34752321fb725d8583bc82ef952e25e02bb3a8302def16a5e3aed2e5dd01` ; son ancien binaire temporel avait une réserve de provenance, levée pour les mesures présentes. `manifest.json` à la racine de cette campagne donne SHA-256 de tous les fichiers de preuve, sauf lui-même.

## Commandes

Depuis `/home/matt/src/crug`, avec Python 3, GCC hôte, Valgrind et le compilateur isolé ci-dessus :

```sh
python3 audit/2026-10-27-adversarial-c0dcb18/replay_previous_frontend.py
python3 audit/2026-10-27-adversarial-c0dcb18/replay_previous_runtime.py
python3 audit/2026-10-27-adversarial-c0dcb18/replay_previous_tasks_main.py
python3 audit/2026-10-27-adversarial-c0dcb18/replay_previous_fake_spawn.py
python3 audit/2026-10-27-adversarial-c0dcb18/replay_previous_s01.py
python3 scripts/test-c2crug.py
bash scripts/gate.sh --self-test
bash scripts/check-gate-archives.sh --self-test
bash scripts/check-ci-gate.sh --self-test
```

Le rejeu S-01 emploie le binaire historique `/tmp/crug-audit-c0dcb18/startup_shutdown_race` après vérification de son SHA ; la procédure de reconstruction est dans l'ancien `MANIFESTE.md`. Ce script ne reconstruit ni ne remplace l'ancien binaire. Les données nouvelles sont `previous_*_replay_raw.json` ; la suite `c2crug` et les gates sont consignées dans `c2crug/c2crug_suite.log` et `tooling_replay.log`.

### Frontend et FFI

```sh
python3 audit/2026-10-27-adversarial-c0dcb18/frontend/run_cases.py
python3 audit/2026-10-27-adversarial-c0dcb18/frontend/run_codegen_cases.py
python3 audit/2026-10-27-adversarial-c0dcb18/frontend/run_modes.py
python3 audit/2026-10-27-adversarial-c0dcb18/frontend/run_strict_unsuppressed.py
python3 audit/2026-10-27-adversarial-c0dcb18/frontend/run_heap_valgrind.py
python3 audit/2026-10-27-adversarial-c0dcb18/frontend/run_sanitizers.py
python3 audit/2026-10-27-adversarial-c0dcb18/frontend/run_initializer_matrix.py
python3 audit/2026-10-27-adversarial-c0dcb18/frontend/run_ffi_focused.py
python3 audit/2026-10-27-adversarial-c0dcb18/frontend/fuzz_crug.py
python3 audit/2026-10-27-adversarial-c0dcb18/frontend/fuzz_ffi.py
```

Les générateurs ont graine 20261027 et limites CPU/mémoire/délai intégrées. Le rapport frontend précise les variantes O0/O2/O3/LTO et l'absence d'oracle sémantique pour les 190 sources générées.

### Runtime et channels

```sh
python3 audit/2026-10-27-adversarial-c0dcb18/runtime/run_scope_late_wake.py
python3 audit/2026-10-27-adversarial-c0dcb18/runtime/run_cycles_scope.py
python3 audit/2026-10-27-adversarial-c0dcb18/runtime/run_fail_paths.py
python3 audit/2026-10-27-adversarial-c0dcb18/runtime/run_multistart.py
python3 audit/2026-10-27-adversarial-c0dcb18/runtime/run_stats_concurrent.py
python3 audit/2026-10-27-adversarial-c0dcb18/runtime/run_memtools.py
python3 audit/2026-10-27-adversarial-c0dcb18/runtime/write_manifest.py
python3 audit/2026-10-27-adversarial-c0dcb18/channels_perf/run_unique.py
python3 audit/2026-10-27-adversarial-c0dcb18/channels_perf/run_claims_close.py
python3 audit/2026-10-27-adversarial-c0dcb18/channels_perf/run_select.py
python3 audit/2026-10-27-adversarial-c0dcb18/channels_perf/run_alignment.py
python3 audit/2026-10-27-adversarial-c0dcb18/channels_perf/run_alignment_extra.py
python3 audit/2026-10-27-adversarial-c0dcb18/channels_perf/run_tls_receiver.py
python3 audit/2026-10-27-adversarial-c0dcb18/channels_perf/run_tls_asan.py
python3 audit/2026-10-27-adversarial-c0dcb18/channels_perf/run_tls_receiver_restart.py
python3 audit/2026-10-27-adversarial-c0dcb18/channels_perf/run_adversarial_perf.py
```

Les scripts bornent les injections et répétitions. Memcheck/Helgrind traversant les fibres peuvent signaler des accès artificiels liés aux changements de pile ; les rapports isolent les diagnostics différentiels pertinents. TSan a compilé mais échoué **avant `main`** avec `unexpected memory mapping` sous WSL : aucun comportement concurrent n'a été validé par TSan.

### c2crug

```sh
python3 audit/2026-10-27-adversarial-c0dcb18/c2crug/run_cases.py
python3 audit/2026-10-27-adversarial-c0dcb18/c2crug/run_hidden_alias_asan.py
python3 audit/2026-10-27-adversarial-c0dcb18/c2crug/run_sidecar_controls.py
python3 audit/2026-10-27-adversarial-c0dcb18/c2crug/run_resource.py
python3 audit/2026-10-27-adversarial-c0dcb18/c2crug/run_resource_control.py
```

Les sources avant/après transformation, rapports de confiance, sidecars valides/invalides et entrées de ressources sont conservés dans `c2crug/` ; `raw.json`, `hidden_alias_asan_raw.json`, `sidecar_controls_raw.json`, `resource_raw_v2.json` et `resource_control_raw.json` contiennent les commandes exactes et sorties.

## Contrôle final

```sh
python3 audit/2026-10-27-c0dcb18/validate_audit.py
python3 audit/2026-10-27-adversarial-c0dcb18/validate_campaign.py
python3 audit/2026-10-27-adversarial-c0dcb18/write_manifest.py
git diff --name-only
git status --porcelain=v1
```

`validate_campaign.py` parse les JSON, compare commit et SHA des binaires au manifeste historique et vérifie les rejouages historiques ainsi que les oracles adversariaux clés ; sa sortie est `validation.log`. Le manifeste racine est à régénérer **après** toute édition des preuves, car il fige leurs SHA. Les tests passés n'impliquent pas sûreté générale : Linux x86-64 seulement, pas de profil matériel natif ni d'ensemble DejaGNU, et des UNKNOWN explicites dans la matrice.

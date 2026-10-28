# Manifeste de provenance et mode de reproduction

## Référence et état

- Dépôt audité : `/home/matt/src/crug` dans Ubuntu-CRUG ; commit `c0dcb18d649a909846e8b2bcefe4eb39b1841dc9`. Référence amont épinglée : `releases/gcc-16.2` → `073f91637dec82017536643f3ba69ecdd9dce6b1`.
- `git diff --name-only` est vide pour les fichiers suivis ; `git status --porcelain=v1` ne signale que `?? audit/`. Les builds historiques dans `build/obj` et les rapports précédents sont conservés. Aucun commit, push, publication ni fichier de production modifié.
- `spec/000-PRECEDENCE.md` lu ; aucun `AGENTS.md` applicable trouvé sous `/home/matt/src` pendant l’inventaire.
- Machine : Ubuntu 24.04.5 sous WSL2, noyau `6.18.33.2-microsoft-standard-WSL2`, x86_64 AMD Ryzen 5 5600X, 12 CPU logiques, ~23 GiB RAM. GCC hôte 13.3.0, Clang 18.1.3, Valgrind 3.22.0. Les informations complètes `uname`, `lscpu`, `/proc/meminfo` et les sorties de versions figurent dans `manifest.json`.

## Binaire CRUG vérifié

Le `build/obj/gcc/xgcc` présent dans le dépôt portait une date antérieure aux sources du commit ; il n’a servi à aucun verdict. Reconstruction GCC **hors arborescence** dans `/tmp/crug-audit-c0dcb18/build`, source absolue `/home/matt/src/crug`, avec :

```sh
mkdir -p /tmp/crug-audit-c0dcb18/build
cd /tmp/crug-audit-c0dcb18/build
/home/matt/src/crug/configure --prefix=/opt/crug-audit --enable-languages=c,c++ --disable-bootstrap --disable-multilib --enable-checking=yes --with-arch=x86-64 --with-tune=generic > configure.log 2>&1
make -j8 all-gcc > build.log 2>&1
make -j8 all-target-libgcc > libgcc-build.log 2>&1
make -j8 all-target-libatomic > libatomic-build.log 2>&1
```

Les quatre journaux sont copiés ici ; `manifest.json` donne leurs SHA-256, les empreintes des sources CRUG principales et des exécutables/objets. `xgcc -B/tmp/crug-audit-c0dcb18/build/gcc/ -dumpfullversion` renvoie 16.2.0 ; cette sortie n’est **pas** la preuve de correspondance. La preuve repose sur le checkout figé et propre, le chemin source du configure, les journaux de reconstruction réussie et les SHA-256 des artefacts. `cc1` reconstruit : `4cbaa0ef491f6d34ec02a26a0fe6f7345b8a1b39430cb7f4a16f7d07785b70f4` ; `xgcc` : `8a0f38d8f2e95d7919409f44e7c1ecdc992a294685e25e63d967d8c641b83e7c`.

Les trois binaires mesurés `bench_runtime`, `bench_pipeline`, `scratch_retention` ont été reconstruits séparément avec `build_benchmarks.py` depuis les mêmes sources et options : **SHA-256 identiques** aux binaires de mesure (`benchmark_build_raw.json`). Le `chanbench` reconstruit fonctionne (24/24 vérifications indépendantes, mêmes compteurs et mesure de mémoire), mais son SHA-256 diffère du binaire utilisé pour les premières valeurs temporelles ; `channel_rebuild_check_raw.json` permet la comparaison. Les temps channel originaux doivent donc être interprétés comme baseline locale avec cette limite de provenance, tandis que les résultats de comptage/mémoire se reproduisent sur le binaire reconstruit. Le script n’a pas écrasé les binaires mesurés.

## Commandes et fichiers de preuves

Depuis la racine du dépôt, avec Python 3 et les binaires reconstruits :

```sh
python3 audit/2026-10-27-c0dcb18/build_race.py
python3 audit/2026-10-27-c0dcb18/run_race_tools.py
python3 audit/2026-10-27-c0dcb18/run_runtime.py
python3 audit/2026-10-27-c0dcb18/run_tasks_main.py
python3 audit/2026-10-27-c0dcb18/run_fake_spawn.py
python3 audit/2026-10-27-c0dcb18/run_compiler_regressions.py
python3 audit/2026-10-27-c0dcb18/run_codegen.py
python3 audit/2026-10-27-c0dcb18/run_sanitizers.py
python3 audit/2026-10-27-c0dcb18/run_lto.py
python3 audit/2026-10-27-c0dcb18/run_macro_repro.py
python3 audit/2026-10-27-c0dcb18/run_perf.py
python3 audit/2026-10-27-c0dcb18/run_pipeline.py
python3 audit/2026-10-27-c0dcb18/run_scratch.py
python3 audit/2026-10-27-c0dcb18/run_compile_bench.py
python3 audit/2026-10-27-c0dcb18/build_benchmarks.py
python3 audit/2026-10-27-c0dcb18/run_channel_rebuild_check.py
python3 audit/2026-10-27-c0dcb18/write_manifest.py
```

`run_perf.py`, `run_pipeline.py` et `run_scratch.py` attendent des harnais déjà compilés ; `build_benchmarks.py` donne les commandes exactes de leur reconstruction dans un sous-répertoire de contrôle. Leurs fichiers `*_raw.json` conservent paramètres, commandes, code de retour, stdout/stderr et mesures ; les `*_summary.json` contiennent les médianes. `race_build_raw.json` et `benchmark_build_raw.json` consignent les commandes de compilation. `run_macro_repro.py` recrée un répertoire de migration distinct dans `/tmp` à chaque exécution. Sous Ubuntu, le lien d’objets produits par xgcc avec GCC hôte utilise `-no-pie` à cause du PIE par défaut de la distribution ; les quatre tests LTO sont compilés et liés avec xgcc isolé.

Auto-tests complémentaires exécutés et consignés : `python3 scripts/test-c2crug.py` (73/73), `bash scripts/gate.sh --self-test`, `bash scripts/check-gate-archives.sh --self-test`, `bash scripts/check-ci-gate.sh --self-test`, et fuzzing borné :

```sh
python3 fuzz/compiler/fuzz.py --check --budget 200 --seed 20261027 --compiler audit/2026-10-27-c0dcb18/cc-isolated.sh --findings /tmp/crug-audit-c0dcb18/fuzz-findings
```

Sorties : `c2crug_tests.log`, `gate_selftests.log`, `fuzz.log`. Toutes les sources des harnais, journaux, rapports, valeurs brutes et leurs empreintes se trouvent dans ce dossier ; `manifest.json` contient l’inventaire machine et les SHA-256. Les commandes peuvent changer les résultats temporels à cause du bruit WSL, mais ne doivent pas changer les assertions fonctionnelles.

## Limites de preuve

- L’exécution locale couvre Linux x86-64 ; les PAL Windows/Darwin et Linux AArch64 n’ont pas été lancés. Aucun audit général de GCC amont ni toute la suite DejaGNU n’ont été faits.
- `perf` est absent ; pas de compteurs de cache misses/contention ni p95/p99 statistiquement défendables. Les mesures de faible durée, surtout channel, fluctuent avec l’ordonnancement hôte.
- TSan refuse la cartographie mémoire de cette WSL avant l’exécution ; les commutations de piles de fibres compliquent également l’interprétation des sanitizers. Les interleavings du constat S-01 reposent sur un wrapper déterministe, pas sur TSan.
- Le build est hors arborescence mais conserve son historique dans `/tmp` ; les rapports, sources et journaux essentiels sont copiés dans `audit/`. Les chemins absolus `/tmp/crug-audit-c0dcb18` sont volontairement inscrits dans les scripts pour empêcher l’emploi accidentel du vieux `build/obj`.
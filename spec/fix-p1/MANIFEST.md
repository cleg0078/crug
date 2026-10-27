# Manifeste des preuves — CRUG phase 4

## Identité, intégrité et règles

- Date : 23 octobre 2026 (Europe/Paris), Ubuntu-CRUG sous WSL2, Ubuntu 24.04 x86-64.
- Dépôt de référence, consulté en lecture seule : `/home/matt/src/crug`. HEAD initial et contrôles intermédiaires : `f86b7df26df4451e3d5952d7bb0fba9cff698b96`, `git status --porcelain=v1` vide.
- Espace de preuves et de compilation : `/home/matt/audit-crug-phase4`, dossier frère indépendant. Aucun programme de reproduction antérieur, test officiel, source CRUG ou build original n'a été modifié. Aucun commit, push, PR, issue ou écriture distante.
- Hiérarchie normative : `spec/000-PRECEDENCE.md` §2 ; RFC 0001 puis ADR acceptées puis MASTER_SPEC. Les documents de phase 2 et 3 nommés dans la demande ont été lus en entier pour déduplication.
- CRUG original : `crugc 0.1.0`, GCC 16.2.0, langage 0.1.0, ABI CRUG/runtime 1, FFI 1.0, PAL 1.1. GCC hôte 13.3.0 a lié les objets compilés par CRUG ou compilé des copies runtime, sans servir d'oracle sémantique.
- Ressources constatées : 12 vCPU, 23 GiB RAM (22 GiB disponibles lors de la mesure), 6 GiB swap, 937 GiB libres sur le disque ; taille du dossier de phase 4 lors de la mesure : 271 MiB. AFL++ a utilisé un worker par cible, 2 048 MiB de mémoire cible et 2 s de timeout par entrée.

Les empreintes ci-dessous ont été recalculées pendant cette phase ; les cinq entrées `cc1`, `xgcc`, `crugc`, `chan.c` et `task.c` correspondent exactement à `audit-crug-phase3/measurements/allfiles.final.sha256`. Cette comparaison et le statut Git propre apportent une vérification ciblée de l'absence de modification du build original.

| Objet de référence | SHA-256 |
|---|---|
| `gcc/crugc` | `fd5cea6e08ad3c10adf0c65a32b199448792d84ce0e30aa15fe635dce0b19cf4` |
| `build/obj/gcc/cc1` | `6c45ed968b2121f5f02d50aa3f72f94c7b5288c744a5897ff1f014d21fdaaa2f` |
| `build/obj/gcc/xgcc` | `8f8d645bef35ab4218721167bcbf304e2e25f3d3e169b44b55e5fa3e62588d7a` |
| `gcc/c/crug-ownership.cc` | `036525f3cf336684df5573b822e51d0e8da6c0297c3b2c41c303e8f6811e9649` |
| `gcc/c/c-parser.cc` | `f5f1a4610a7be8267665b17d1b273ebeda9bbff051e62658cdc6bb1ecf40da98` |
| `gcc/c-family/crug-ffi.cc` | `1f75265e01cf626f3e9e999fa45ad781616d55620afad26bdce9c2dcfc6c2311` |
| `libcrug/core/chan.c` | `f071c36163a8b7f02e9178e99cbb795f7330596becb14ac6e825e1f652bdd573` |
| `libcrug/core/task.c` | `9fba2875126e84355acf659a5964035d73b38131d7ae902042695db7da6b753c` |
| `c2crug` | `f06379594465c60e66a4e0e1e8997c7dcf77ad026b98fe158e309e146d9705f8` |

## Delta propre à CRUG sur GCC 16.2

`git diff --name-only releases/gcc-16.2 HEAD -- gcc/c gcc/c-family libcrug` identifie les modules nouveaux `gcc/c/crug-{ownership,safe,tasks,chan,send-sync}.cc`, `gcc/c-family/crug-{ffi,common,diagnostics}.cc`, leurs headers et `libcrug/`. Les intégrations dans les fichiers GCC hérités comprennent `gcc/c/c-parser.cc`, `gcc/c/c-decl.cc`, `gcc/c/c-tree.h`, `gcc/c-family/{c-cppbuiltin,c-opts,c.opt}` et `gcc/c/Make-lang.in`. Contre le tag : `c-parser.cc` +2 437/-31 lignes, `c-decl.cc` +131/-2, `c-tree.h` +14/-0, `c.opt` +49/-0, `c-cppbuiltin.cc` +35/-0, `c-opts.cc` +21/-0, `Make-lang.in` +2/-1. Ce relevé circonscrit les intégrations ; il n'est pas un audit général de GCC.

## Inventaire principal

| Chemin | Preuve |
|---|---|
| `RAPPORT.md` | Synthèse, métriques définies, verdict et limites |
| `FINDINGS.md` | R4-RT-001 et deux défauts c2crug confirmés, R4-RT-002 prudent et déduplication |
| `ROOT_CAUSES.md` | Invariants et mécanismes par famille |
| `SEMANTIC_ORACLE.md`, `ORACLE_VALIDATION.md` | Règles du sous-ensemble, tests manuels, limites et vérification |
| `GENERATED_CORPUS.md`, `oracle/` | Modèle/générateur Python, seed 41620, 10 000 formes, corpus, comparaisons et logs |
| `BORROW_LIFETIMES.md` | Jonctions, prêteurs et 10 210 exécutions de chemins |
| `OWNERSHIP_RAII.md`, `raii/` | 13 formes valides, quatre rejets, 13 LTO, sanitizers/Valgrind, compteurs |
| `ALIAS_RESTRICT.md`, `alias/` | 120 compilations ordinaires + 16 LTO, sept variantes d'alias, fact dump, témoin disjoint |
| `FRONTEND_FUZZING.md`, `COVERAGE.md`, `fuzz/` | Seeds, harnais, campagnes AFL, crash témoin, couverture disponible et reprise |
| `RUNTIME_CONCURRENCY.md`, `runtime/` | Programmes réduits, interleavings, sanitizers, stress et logs |
| `C2CRUG_MIGRATION.md`, `c2crug*/` | Deux preuves de migration erronées indépendantes, sorties, compteurs et Valgrind |
| `RECOMMENDATIONS.md` | Directions de correction et régressions proposées, sans application |

## Commandes de rejeu et budgets

Depuis Ubuntu-CRUG, les scripts ci-dessous écrivent uniquement dans le dossier de phase 4. Les journaux existants de certaines matrices peuvent être remplacés en cas de rejeu ; faire une copie du dossier de preuves si leur version initiale doit être conservée.

```sh
python3 /home/matt/audit-crug-phase4/oracle/generate.py --help
python3 /home/matt/audit-crug-phase4/oracle/resume_campaign.py
python3 /home/matt/audit-crug-phase4/oracle/aggregate_campaign.py
python3 /home/matt/audit-crug-phase4/alias/run_matrix.py
python3 /home/matt/audit-crug-phase4/alias/run_lto.py
python3 /home/matt/audit-crug-phase4/raii/run_matrix.py
python3 /home/matt/audit-crug-phase4/raii/run_lto.py
python3 /home/matt/audit-crug-phase4/raii/run_valgrind.py
python3 /home/matt/audit-crug-phase4/runtime/stress_scheduler.py --seconds 60 --timeout 5
python3 /home/matt/audit-crug-phase4/fuzz/scripts/run_campaign.py parser --raw-signals --seconds 3600
python3 /home/matt/audit-crug-phase4/fuzz/scripts/manage_campaign.py snapshot parser-raw
python3 /home/matt/audit-crug-phase4/fuzz/scripts/triage_crashes.py parser
```

Le corpus final de 10 000 a été généré puis entièrement compilé et comparé par dix tranches vérifiées : 3 122 acceptations communes, 5 807 rejets communs, 191 variantes P3-001 acceptées à tort et 880 UNKNOWN. Les 10 210 chemins exécutés concordent ; 1 329,563 s de temps mural de comparaison. Les budgets AFL initiaux étaient parseur 8 h, borrow 12 h, ownership 8 h et FFI 8 h. Les campagnes de découverte terminées totalisent 15 745,790 s de temps mural cumulé et 1 337 682 lancements de cible ; 15 251,197 s et 1 298 118 lancements utilisent le shim de signaux qualifié. Les temps de cibles parallèles sont additionnés, pas présentés comme temps mural de la session. Les trois cibles C ont chacune une reprise bornée de 3 640 s ; le FFI a été réparti entre corpus initial et sidecars structurés. Les cinq dossiers de découverte ont zéro crash et zéro hang sauvegardé ; le contrôle séparé P3-008 a six mutations du même crash déjà connu. Les durées et compteurs exacts par segment, seeds, commandes et journaux bruts figurent dans FRONTEND_FUZZING.md et fuzz/logs/*summary.json. Tous les fuzzers et moniteurs sont arrêtés ; les objectifs horaires restent à poursuivre avec les scripts de reprise.

Le stress runtime prolongé a duré 3 600,055232 s et réussi 29 633/29 633 cycles, soit 88 899 000 tâches terminées. Son résumé est runtime/logs/scheduler_hour_summary.json ; l'empreinte SHA-256 recalculée du journal runtime/logs/scheduler_stress.jsonl est 4cf044758b3386a3e78bddf0d30fef0794264960f1f99c93329cf97acc65bb70. Le processus est arrêté. La charge fixe n'a donné qu'une signature de sortie ; une répétition identique ne démontrerait pas davantage de couverture.

La copie GCOV indépendante a produit un cc1 instrumenté (SHA-256 d8261e9767600e0183f64a0b777583b6763fd89b7264ac1e3453114225181c58). Les deux segments de reprise du build ont duré 324,862 s et 109,496 s, soit 434,358 s mesurées sous plafond de 2 400 s ; une tentative initiale interrompue pour préserver les autres campagnes avait duré environ 194,5 s selon les horodatages du journal. Sur 90 entrées représentatives (40 seeds et 10 entrées de queue par famille), 20 compilations sont acceptées, 70 rejetées, zéro timeout ou ICE. GCOV a couvert 2 267/4 196 lignes exécutables et 192/269 fonctions des huit nouveaux modules CRUG ; dans quatre fichiers GCC hérités modifiés, 750/1 327 lignes ajoutées et 52/63 fonctions touchant ces lignes. Les JSON bruts sont fuzz/gcov-report/representative-corpus.json et crug-source-coverage.json. Cette mesure de source provient de l'échantillon et ne représente pas tous les corpus AFL.

## Empreintes du dossier de preuves

`SHA256SUMS.txt` contient 97 111 entrées et a été vérifié intégralement avec `sha256sum -c --quiet` (code 0). Son empreinte SHA-256 est `4edecbbd7d3b78cfa5a895a272366a3980c6d49fb7ace400f21213d77c51946d`. Le manifeste de sommes exclut son propre fichier et `MANIFEST.md` pour éviter l'auto-référence. Il exclut aussi `fuzz/gcov-source/` et `fuzz/gcov-build/`, copies expérimentales reproductibles de 1,4 GiB et du build ; le commit source est épinglé, le `cc1` GCOV est identifié ci-dessus par SHA-256, et les 12 paires finales de `.gcno/.gcda` sont copiées sous `fuzz/gcov-report/final-profiles/` avec leur propre manifeste et incluses dans `SHA256SUMS.txt`. Les corpus, scripts, rapports, journaux et JSON de résultats sont inclus.
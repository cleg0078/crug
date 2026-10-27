# Fuzzing instrumente du frontend CRUG - phase 4

## Perimetre et provenance

Audit du commit `f86b7df26df4451e3d5952d7bb0fba9cff698b96`. La cible AFL++ 4.09c est le `cc1` de phase 3, utilise en lecture seule : `/home/matt/audit-crug-phase3/agents/alias_gcc/afl-build/gcc/cc1`, SHA-256 `f37b17fb12b0a6177db8200475050da18bf0c9c55aba257ce046ff6869f89fc3`. Les empreintes de `c-parser.cc`, `crug-ownership.cc` et `crug-ffi.cc` sont identiques dans cette copie et dans le depot de reference (`fuzz/logs/provenance.sha256`). Le depot de reference est reste propre ; aucun build ni repro anterieur n'a ete modifie.

Tous les scripts, entrees, resultats, checkpoints et journaux de cet axe sont dans `fuzz/` sous la copie d'audit phase 4. Les 14 seeds du parseur et les sous-corpus borrow/ownership proviennent de la phase 3 ; les sidecars FFI initiaux viennent des cas de phase 3. Le temoin P3-008 est exclu des corpus de decouverte et garde dans `fuzz/controls/`. Les empreintes des seeds sont dans `fuzz/logs/seeds.sha256`.

Chaque lancement emploie un seul worker AFL, `-t 2000` ms, `-m 2048` MiB et un garde externe `timeout`. Les quatre workers prolonges ont fonctionne simultanement sur 12 vCPU ; leurs secondes par cible ne sont donc pas une duree murale globale. La memoire disponible est restee proche de 20 a 23 GiB. Le build GCOV isole a ete suspendu pendant la phase chargee, puis repris apres l'arret de tous les workers ; voir `COVERAGE.md`.

## Qualification du signal et des chemins CRUG

L'instrumentation du `cc1` donne des cartes `afl-showmap` non vides (par exemple 14 773 cellules sur `qualifier_sizeof_single.c` et 17 560 sur `badarg.crugffi`). GDB confirme l'entree dans `crug_qualifier_type_name_p` (`c-parser.cc`), `crug_check_function` et `collect_drops` (`crug-ownership.cc`), ainsi que `parse_ffi_file` (`crug-ffi.cc`) : `fuzz/logs/{parser,borrow,ownership,ffi}-gdb.txt`. La seed FFI structuree valide atteint aussi `parse_ffi_file` (`fuzz/logs/ffi-structured-gdb.txt`). Un controle `afl-showmap -t 20` ms a effectivement expire ; les campagnes utilisent 2 000 ms.

GCC intercepte normalement SIGSEGV, imprime un ICE et quitte avec code 1. AFL ne classe alors pas le P3-008 connu comme crash : un controle `afl-fuzz -C` sans shim refuse la seed (`fuzz/logs/control-afl.log`). Les segments initiaux a gestionnaire GCC sont donc utiles pour le feedback, mais leurs zero crash **ne sont pas fiables**.

Le shim experimental `fuzz/scripts/libraw_signal.so` (SHA-256 `7d19ccb088907ca1645ba45ca777a3dd0e92093e94db0a75c869201a40924d49`) laisse les signaux fatals a l'action par defaut. Il est charge par `LD_PRELOAD` dans les scripts `run_campaign.py --raw-signals` et `run_structured_ffi.py`, pour le preflight et AFL. `strace` confirme SIGSEGV sur P3-008. Avec ce shim, le controle `afl-fuzz -C` de 10 s/261 executions a sauve 6 mutations crashantes ; les 6 rejouees sur le compilateur CRUG original produisent le meme ICE SIGSEGV de la famille `own own`/`c_lex_one_token`, sans nouveau finding (`fuzz/logs/triage/control/`). Avant le FFI structure, `fuzz/logs/ffi-structured-known-crash.log` affiche `Program killed by signal 11` sur le temoin P3-008 avec le meme shim. Le resume de ce segment porte `raw_signals=true`, le chemin et le hash du shim. Tout futur crash doit toujours etre rejoue sans shim sur le compilateur original.

## Campagnes executees

`Duree` est la borne ou le temps actif AFL constate ; la colonne `mur` est le chronometre monotone du lanceur, qui inclut sa mise en route et son arret. `Cellules` est `edges_found` sur la carte AFL globale, jamais une mesure de lignes ou de branches source. `Corpus` est la taille finale du dossier retenu pour le segment. Tous les segments de decouverte ont `cycles_done=0`, stabilite AFL 100 %, zero hang et zero crash sauvegardes. Les compteurs `execs_done` redemarrent a chaque reprise ; les executions des segments sont donc additionnables.

| Cible | Mode et segment | Duree active / borne | Mur mesure | Executions | Corpus | Cellules | Crashs |
|---|---|---:|---:|---:|---:|---:|---:|
| Parseur | gestionnaire GCC initial | 300 s | 306,023 s | 16 036 | 1 326 | 30 979 | 0 non fiable |
| Parseur | signaux bruts, initial | 300 s | 305,455 s | 16 217 | 1 341 | 31 077 | 0 |
| Parseur | signaux bruts, reprise | 3 640 s | 3 640,136 s | 163 676 | 4 458 | 35 191 | 0 |
| Borrow/provenance | gestionnaire GCC initial | 120 s | 124,514 s | 6 040 | 621 | 26 706 | 0 non fiable |
| Borrow/provenance | signaux bruts, initial | 120 s | 123,433 s | 6 356 | 651 | 26 819 | 0 |
| Borrow/provenance | signaux bruts, reprise | 3 640 s | 3 640,305 s | 163 202 | 4 525 | 33 350 | 0 |
| Ownership/drop | signaux bruts, initial | 120 s | 124,474 s | 7 101 | 628 | 23 322 | 0 |
| Ownership/drop | signaux bruts, reprise | 3 640 s | 3 640,096 s | 167 761 | 3 412 | 28 003 | 0 |
| FFI, sidecars initiaux | gestionnaire GCC initial | 60 s | 64,056 s | 17 488 | 130 | 18 233 | 0 non fiable |
| FFI, sidecars initiaux | signaux bruts, initial | 120 s | 124,229 s | 28 349 | 149 | 18 287 | 0 |
| FFI, sidecars initiaux | signaux bruts, reprise | 30 s | 42,661 s | 8 853 | 167 | 18 348 | 0 |
| FFI, sidecars initiaux | signaux bruts, segment stagnant | 1 328 s AFL | 1 339,337 s | 247 818 | 310 | 18 433 | 0 |
| FFI, structure | signaux bruts, nouveau segment | 2 272 s AFL | 2 271,071 s | 488 785 | 511 | 19 198 | 0 |

Les trois reprises C demandaient `-V 3600`, mais ce parametre n'a pas interrompu AFL au terme sur un corpus repris. Le garde externe de 3 640 s a envoye SIGTERM proprement, et `timeout` a retourne 124. Leurs resumes et `fuzzer_stats` sont complets ; 124 est ici le code du garde, pas un crash de la cible. Les scripts de reprise phase 4 ont ensuite ete corriges : le garde externe vaut exactement la duree demandee, `prior_run_time` est enregistre, et le code 124 du garde est accepte si les statistiques ont ete sauvees. Le segment FFI structure s'est arrete naturellement a `-V 2272` avec code 0.

Le segment FFI initial a ete arrete proprement au checkpoint apres 1 328 s AFL : 247 818 executions pour seulement 85 cellules supplementaires par rapport aux 18 348 de depart. Son solde autorise de 2 272 s a ete consacre a un dossier AFL distinct, `ffi-structured-raw`, avec 12 sidecars plus riches, un dictionnaire de grammaire et un fichier C fixe de phase 4. Neuf des 12 seeds sont acceptees par le compilateur original ; trois provoquent des diagnostics CRUG attendus (`fuzz/logs/ffi-structured-seed-validation.json`). Une selection deterministe de 40/511 entrees finales donne 3 acceptations, 37 diagnostics CRUG, zero ICE et zero timeout (`fuzz/logs/queue-sample-ffi-structured-20261023T094430Z.json`) : c'est un indicateur de traversée, **pas** un oracle semantique. Les cartes des deux dossiers FFI ne doivent pas etre soustraites : la source fixe et les corpus different.

La duree nominale qualifiee par signaux bruts atteint environ 3 940 s pour le parseur, 3 760 s pour borrow, 3 760 s pour ownership et 3 750 s pour FFI (dont 1 328 s sur les anciens sidecars et 2 272 s structures). Leurs temps muraux exacts et commandes sont dans `fuzz/logs/*summary.json`. Au total, les lanceurs ont mesure **15 745,790 s mur cumules et 1 337 682 executions** en decouverte, dont **15 251,197 s et 1 298 118 executions avec shim** ; ces secondes sont additionnees entre workers paralleles. Le controle P3-008 de 10 s/261 executions est exclu de ces totaux.

| Cible | Baseline GCC | Signaux bruts, temps nominal/actif | Objectif | Solde indicatif qualifie |
|---|---:|---:|---:|---:|
| Parseur | 5 min | 1 h 05 min 40 s | 8 h | 24 860 s |
| Borrow/provenance | 2 min | 1 h 02 min 40 s | 12 h | 39 440 s |
| Ownership/drop | 0 | 1 h 02 min 40 s | 8 h | 25 040 s |
| FFI | 1 min | 1 h 02 min 30 s | 8 h | 25 050 s |

Les quatre budgets horaires restent **non atteints**. Le corpus n'a pas fini son premier cycle de mutation. Les trois cibles C gagnaient encore cellules et entrees dans la derniere fenetre ; elles restent candidates a une reprise. Le FFI structure n'a gagne que 89 cellules entre 09:13 et 09:39 UTC, malgre 154 entrees supplementaires ; une generation structuree plus profonde serait preferable a une prolongation aveugle. Aucun nouveau segment n'a ete lance apres ces budgets afin de reserver les CPU a GCOV. Les trois segments C ont depasse de 40 s chacun en raison du garde initial, fait compte dans le tableau.

Les cinq dossiers de decouverte a signaux bruts ont ete tries par `fuzz/scripts/triage_crashes.py` : zero fichier crash, zero hang enregistre et donc aucun nouveau cas a attribuer. Les 6 crashs du controle sont des variantes P3-008 deja connues. La selection deterministe de 40 entrees par dossier C/FFI initial a donne 1/40 acceptation parseur, 0/40 pour borrow, ownership et FFI initial, avec diagnostics CRUG dans 39 a 40 cas selon la cible (`fuzz/logs/queue-sample-*-20261023T084151Z.json`). Ce faible taux d'acceptation limite la valeur semantique des campagnes, meme quand le feedback AFL croit.

## Etat final et reprise

Apres les segments, aucun `afl-fuzz`, `run_campaign.py` ou `run_structured_ffi.py` ne reste actif. Le moniteur periodique a ete arrete. La preuve de processus vide et de depot de reference propre est dans `fuzz/logs/final-process-state.json` (09:53:37 UTC). Les checkpoints se trouvent dans `fuzz/snapshots/`; les corpus finaux restent dans `fuzz/results/`. Pour verifier ou sauvegarder une nouvelle session :

```sh
python3 /home/matt/audit-crug-phase4/fuzz/scripts/manage_campaign.py snapshot parser-raw
python3 /home/matt/audit-crug-phase4/fuzz/scripts/manage_campaign.py stop parser-raw
python3 /home/matt/audit-crug-phase4/fuzz/scripts/triage_crashes.py parser
```

Remplacer `parser-raw` par `borrow-raw`, `ownership-raw`, `ffi-raw` ou `ffi-structured-raw` et la cible de triage par `borrow`, `ownership`, `ffi` ou `ffi_structured`. Le script de gestion n'envoie SIGTERM qu'au PID AFL dont la commande pointe sur le dossier de phase 4.

Commandes de reprise vers les budgets horaires, **une cible a la fois** si d'autres charges CPU sont presentes ; les secondes sont les soldes indicatifs du tableau et sont strictement bornees par `timeout` externe. Recalculer le solde apres chaque segment.

```sh
python3 /home/matt/audit-crug-phase4/fuzz/scripts/run_campaign.py parser --raw-signals --seconds 24860
python3 /home/matt/audit-crug-phase4/fuzz/scripts/run_campaign.py borrow --raw-signals --seconds 39440
python3 /home/matt/audit-crug-phase4/fuzz/scripts/run_campaign.py ownership --raw-signals --seconds 25040
python3 /home/matt/audit-crug-phase4/fuzz/scripts/run_structured_ffi.py --seconds 25050
```

`run_campaign.py` et `run_structured_ffi.py` detectent `fuzzer_stats`, reprennent avec `-i -`, conservent le corpus et enregistrent le compteur AFL anterieur. Le FFI structure reste un dossier distinct des anciens sidecars. Le shim qualifie l'observation des signaux fatals ; il ne prouve ni la correction semantique, ni l'absence de bug dans les chemins non parcourus.




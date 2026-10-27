# Couverture du frontend CRUG - phase 4

## Deux mesures distinctes

AFL++ 4.09c instrumente le `cc1` de phase 3 et donne un feedback sur une carte de **8 388 608 cellules** couvrant l'ensemble du frontend et ses bibliotheques instrumentees. `edges_found` compte les cellules decouvertes, et `bitmap_cvg` leur occupation de cette carte. Ces valeurs ne sont **ni** des branches source **ni** des lignes CRUG. Les arrets GDB prouvent seulement l'entree dans des fonctions ciblees.

| Dossier AFL avec shim de signaux | Cellules finales | Occupation de la carte | Fonction CRUG dont l'entree est prouvee |
|---|---:|---:|---|
| Parseur | 35 191 | 0,42 % | `crug_qualifier_type_name_p`, `gcc/c/c-parser.cc` |
| Borrow/provenance | 33 350 | 0,40 % | `crug_check_function`, `gcc/c/crug-ownership.cc` |
| Ownership/drop | 28 003 | 0,33 % | `collect_drops`, `gcc/c/crug-ownership.cc` |
| FFI, anciens sidecars | 18 433 | 0,22 % | `parse_ffi_file`, `gcc/c-family/crug-ffi.cc` |
| FFI structure | 19 198 | 0,23 % | `parse_ffi_file`, `gcc/c-family/crug-ffi.cc` |

Traces : `fuzz/logs/{parser,borrow,ownership,ffi}-gdb.txt` et `ffi-structured-gdb.txt`. Les deux dossiers FFI ont des entrees et un programme C fixe differents : leurs cellules ne sont pas additionnables ou soustractibles. Toutes les campagnes de decouverte ont `cycles_done=0`, stabilite AFL 100 %, zero crash et zero hang sauvegardes. Le temoin P3-008 a ete detecte avec le shim ; son controle est hors de ces metriques.

## Construction GCOV independante

Une copie du commit `f86b7df26df4451e3d5952d7bb0fba9cff698b96` a ete extraite par `git archive` dans `fuzz/gcov-source`, sans ecrire dans le depot ou le build de reference. Les empreintes de `c-parser.cc`, `crug-ownership.cc` et `crug-ffi.cc` figurent dans `fuzz/logs/gcov-source.sha256` et correspondent au commit cible. La configuration de `fuzz/gcov-build` utilise le compilateur hote GCC 13.3.0 :

```sh
CC=/usr/bin/gcc CXX=/usr/bin/g++ CFLAGS="-O0 -g --coverage" CXXFLAGS="-O0 -g --coverage" LDFLAGS="--coverage" ../gcov-source/configure --enable-languages=c --disable-bootstrap --disable-multilib --disable-nls --without-isl --enable-checking=yes --with-arch=x86-64 --with-tune=generic
```

Une premiere tentative `make -j2 all-gcc` a ete interrompue sous charge AFL/oracle apres environ 194,5 s, selon les horodatages du log `fuzz/logs/gcov-make.log` (08:46:08–08:49:22 UTC ; aucun chrono monotone n'avait ete enregistre). Une fois les fuzzers et le stress arretes, la meme copie a ete reprise : `make -j2 all-gcc` pendant **324,862 s** (SIGTERM volontaire, log et resume `gcov-make-resume.*`), puis `make -j4 all-gcc` pendant **109,496 s** (code 0, log et resume `gcov-make-j4.*`). La reprise a donc consomme **434,358 s** de son plafond de 2 400 s ; le build total observe est d'environ 629 s en incluant la tentative initiale. Le `cc1` GCOV final est `fuzz/gcov-build/gcc/cc1`, SHA-256 `d8261e9767600e0183f64a0b777583b6763fd89b7264ac1e3453114225181c58`.

Un smoke test `-fcrug-modern` a compile `qualifier_sizeof_single.c` avec code 0. Les douze `.gcda` des fichiers mesures crees par ce controle ont ete archives dans `fuzz/gcov-report/preflight-gcda/` avant la mesure, avec manifeste SHA-256. Le corpus de mesure frais contient **90 compilations deterministes** : toutes les 40 seeds des cinq familles (14 parseur, 4 borrow, 4 ownership, 6 FFI anciens, 12 FFI structures) et 10 entrees tirees de chaque file AFL finale par `random.Random(41620).sample` sur une liste triee. Le script `fuzz/scripts/run_gcov_corpus.py` enregistre les commandes, hashes d'entree et statuts dans `fuzz/gcov-report/representative-corpus.json`. Resultat : 20 acceptations, 70 rejets, aucun timeout ou ICE ; les rejets parcourent aussi du code et contribuent aux compteurs GCOV. Il s'agit d'un echantillon reproductible, pas de l'ensemble des plus de 13 000 fichiers retenus par AFL.

`fuzz/scripts/gcov_measure.py` appelle `gcov --json-format -b -c` sur les objets produits par ce build et lit leurs compteurs. Les douze fichiers ci-dessous ont chacun un `.gcno` et un `.gcda` ; aucune erreur GCOV. Les 12 paires finales (24 fichiers, 7,6 MiB) ont ete copiees hors du gros build dans `fuzz/gcov-report/final-profiles/` ; son `manifest.json` enregistre les chemins d origine, hashes SHA-256, commit cible, hash du `cc1` et du corpus (SHA-256 du manifeste : `5a9abdc390f1a910344f38e351cbed7e624cc121354ee9ea5c0ec0a08bf7cdba`). Une ligne est « executable » si elle apparait comme ligne instrumentee dans le JSON GCOV, et « executee » si son compteur est > 0. Une fonction est « executee » si `execution_count > 0`. Les details et noms de fonctions executes sont dans `fuzz/gcov-report/crug-source-coverage.json`.

## Nouveaux modules CRUG

Le denominateur de chaque module est l'ensemble de ses lignes executables et fonctions que GCOV a instrumentees dans son fichier `.cc`. Les en-tetes ne sont pas comptes separement.

| Module `.cc` | Lignes executees / executables | Fonctions executees / instrumentees |
|---|---:|---:|
| `gcc/c-family/crug-common.cc` | 32 / 57 | 2 / 2 |
| `gcc/c-family/crug-diagnostics.cc` | 23 / 30 | 3 / 4 |
| `gcc/c-family/crug-ffi.cc` | 510 / 727 | 33 / 41 |
| `gcc/c/crug-chan.cc` | 95 / 193 | 19 / 30 |
| `gcc/c/crug-ownership.cc` | 1 249 / 2 373 | 97 / 119 |
| `gcc/c/crug-safe.cc` | 138 / 452 | 20 / 46 |
| `gcc/c/crug-send-sync.cc` | 51 / 131 | 7 / 9 |
| `gcc/c/crug-tasks.cc` | 169 / 233 | 11 / 18 |
| **Total des huit modules** | **2 267 / 4 196 (54,03 %)** | **192 / 269 (71,38 %)** |

La mesure montre une entree effective dans chaque nouveau module `.cc`, mais des parties importantes restent non executees. Par exemple `crug-safe.cc` n'a que 138/452 lignes executees sur ce corpus. Le total ne mesure pas le runtime ni les en-tetes inline separement.

## Plages modifiees de fichiers GCC

La base de comparaison est le commit d'import GCC 16.2.0 `57ccbaee04f793567789037db9c36be2fa285b4b`. Pour chaque fichier GCC `.cc` modifie, `git diff --unified=0` entre cette base et le commit cible selectionne seulement les **lignes ajoutees ou remplacees dans la version actuelle** ; l'intersection avec les lignes executables GCOV forme le denominateur. Les fonctions ci-dessous sont celles dont la plage source contient au moins une de ces lignes. Leur execution est un indicateur plus large : une fonction peut etre appelee sans que toutes ses lignes CRUG modifiees soient executees.

| Fichier GCC modifie | Lignes modifiees executees / executables | Fonctions touchant ces lignes executees / instrumentees |
|---|---:|---:|
| `gcc/c-family/c-cppbuiltin.cc` | 23 / 25 | 1 / 1 |
| `gcc/c-family/c-opts.cc` | 5 / 8 | 2 / 2 |
| `gcc/c/c-decl.cc` | 48 / 64 | 9 / 9 |
| `gcc/c/c-parser.cc` | 674 / 1 230 | 40 / 51 |
| **Total des quatre fichiers** | **750 / 1 327 (56,52 %)** | **52 / 63 (82,54 %)** |

Les portions non executables des changements (declarations, commentaires, directives, autres elements sans compteur) sont exclues de ce pourcentage. Les changements de `c.opt`, `Make-lang.in`, des en-tetes, d'autres modules GCC et les chemins du runtime ne sont pas couverts par cette table. Les noms de fonctions et numeros de lignes exacts peuvent etre recalcules avec le script et le diff epingle.

## Portee des conclusions

Ces pourcentages sont **une couverture source mesuree sur les 90 cas decrits**, pas une couverture de toutes les entrees AFL ni une preuve d'exhaustivite fonctionnelle. Une fonction appelee peut echouer sur un diagnostic avant ses branches profondes ; GCOV compte aussi ces parcours. Aucune couverture de branches n'est revendiquee, bien que les JSON GCOV contiennent des compteurs de branches. Les cellules AFL ne sont jamais converties en lignes source. La qualification du crash par shim s'applique aux campagnes AFL ; la mesure GCOV a execute le `cc1` isole sans shim et a observe zero ICE sur ce corpus.

Pour reproduire sur le build GCOV deja produit : archiver ou remettre a zero **uniquement** les `.gcda` des douze fichiers mesures avant un nouveau corpus, lancer `python3 /home/matt/audit-crug-phase4/fuzz/scripts/run_gcov_corpus.py`, puis `python3 /home/matt/audit-crug-phase4/fuzz/scripts/gcov_measure.py`. Le script de corpus refuse de demarrer si des compteurs CRUG existent afin d'eviter un melange involontaire de sessions. Pour etendre la mesure, choisir et publier un autre echantillon de corpus, ainsi que ses hashes et les compteurs remis a zero.


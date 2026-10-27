# c2crug --safe — vérification ciblée de la preuve de libération

Référence : commit `f86b7df26df4451e3d5952d7bb0fba9cff698b96`. Les sources et objets originaux sont restés en lecture seule. Le contre-exemple et les sorties se trouvent exclusivement dans ce dossier de phase 4.

## R4-TOOL-001 — libération conditionnelle masquée par un label

Le contrat accepté ADR 0023 D1 impose une unique libération sur chaque chemin atteignable, au même niveau d'accolades et hors d'un contrôle sans accolades. `docs/crug/C2CRUG.md` §3 dit que l'outil conserve en `raw` une libération conditionnelle et que `--safe` ne transforme que les déclarations dont la preuve est établie.

Dans `c2crug/conditional_label.c`, le corps de `if (c)` est un énoncé étiqueté `label: free(p);`, réparti sur deux lignes. L'étiquette ne rend pas la libération inconditionnelle : le témoin original lié à `--wrap=free` imprime `after_false=0`, puis `after_true=1` dans `c2crug_original_control.stdout`. Le C original compile avec `crugc` et les mêmes drapeaux ownership/borrow.

`c2crug --safe` transforme pourtant `int *p` en `own int *p` et `free(p)` en `drop(p)`. Le rapport JSON accepté par le validateur de structure du projet classe cette transformation `own`, `confidence=1.00`, `applied=true`. Le diff exact est récupérable avec la commande ci-dessous ; les fichiers produits sont `c2crug_safe/conditional_label.c` et `c2crug_safe_report.json`. Le compilateur CRUG rejette ensuite le fichier généré à O0 et O2 avec `[crug-DROP-006]` : la destruction exacte dépend de la branche. Les diagnostics bruts sont dans `c2crug_compile_O0.stderr` et `c2crug_compile_O2.stderr`. Cette vérification confirme une erreur de preuve de l'outil ; elle ne prétend pas que le compilateur accepte un programme unsafe.

La cause est dans `c2crug:257-269` : `unbraced_conditional` n'examine que la dernière ligne non vide avant `free(p)`. Cette ligne est `label:`, si bien que le `if` précédent n'est plus vu. `analyse_source` se contente alors du même niveau d'accolades et de l'absence de sortie anticipée. Il ne construit pas le flot de contrôle promis par la classification.

## Témoins et rejeu borné

- `c2crug_controls/straight/straight.c` : libération inconditionnelle, classée `own` et transformée ; la sortie transformée compile.
- `c2crug_controls/if_no_label/if_no_label.c` : le même `if` sans étiquette, correctement classé `raw` avec raison « conditional path ».
- `c2crug_else/else_release.c` : variante sans label où `else` occupe la ligne précédant `free(p)`; `--safe` applique aussi `own` avec confiance 1,00, puis CRUG rejette la sortie avec DROP-006. Rapport structurellement valide dans `c2crug_else_report.json` et diagnostic dans `c2crug_else_compile.stderr`.
- `c2crug/conditional_label.c` : cas différentiel fixe, sans seed aléatoire ; `--safe` produit une sortie que CRUG refuse. Le rapport `c2crug_safe_report.json` passe `scripts/validate-c2crug-report.py`.

Depuis Ubuntu-CRUG, les commandes suivantes relisent les preuves sans modifier le dépôt ; pour refaire `--safe`, choisir un nouveau chemin de sortie sous le dossier de phase 4, car l'outil refuse un dossier déjà existant.

```sh
python3 /home/matt/src/crug/c2crug --diff /home/matt/audit-crug-phase4/c2crug
python3 /home/matt/src/crug/scripts/validate-c2crug-report.py /home/matt/audit-crug-phase4/c2crug_safe_report.json
/home/matt/src/crug/gcc/crugc -O0 -fcrug-ownership -fcrug-borrow -fno-pie -c /home/matt/audit-crug-phase4/c2crug_safe/conditional_label.c -o /home/matt/audit-crug-phase4/c2crug_safe/replay.o
/home/matt/audit-crug-phase4/c2crug_original_control
```

La compilation négative est attendue avec le code `crug-DROP-006`. Si elle était acceptée par une autre version du compilateur, il faudrait refaire l'analyse de ses drops avant de conclure sur son comportement. Aucune évaluation d'exploitabilité n'est faite.

## R4-TOOL-002 — garde de nullité détachée d'un autre retour anticipé

Le second contre-exemple `c2crug_nullguard/nullguard.c` a deux sorties avant `free(p)` :

```c
int *p = malloc(sizeof *p);
if (!p) return 0;
if (c) return 1;
free(p);
```

La première sortie est une vraie garde de nullité. La seconde, lorsque `c=1` et l'allocation a réussi, saute la libération d'une ressource non nulle. ADR 0023 D1 n'autorise que l'exception de la garde de nullité ; cette source doit donc rester `raw`.

`c2crug --safe` marque pourtant `own`, `confidence=1.00`, `applied=true` dans `c2crug_nullguard_report.json`, lequel passe le validateur du projet. Le fichier généré `c2crug_nullguard_safe/nullguard.c` compile avec CRUG à O0 et O2. À O0, le même wrapper `free` lié aux objets compilés avec les mêmes flags montre `after_false=1, after_true=1` pour le C original et `after_false=1, after_true=2` pour la migration (`c2crug_nullguard_{original,migrated}.stdout`). Valgrind observe 4 octets définitivement perdus dans l'original et 0 dans la migration (`*.valgrind.stderr`). La sortie `--safe` ajoute donc une destruction sur un chemin où le programme d'origine ne libérait pas l'objet. Ce constat porte sur l'équivalence promise par l'outil, sans présenter la fuite d'origine comme un défaut de CRUG ou inférer un UAF.

Cause précise : `c2crug:245-255`, `is_null_guard`, cherche `if (!p)` dans les quatre lignes précédentes, sans lier cette garde au `return` examiné. À `c2crug:510-515`, `analyse_source` accepte ainsi le second `return` comme s'il était protégé par la première garde. Le contrôle `c2crug_controls/early_no_nullguard` est correctement `raw` ; le contrôle `legit_nullguard` est correctement `own`. Le résultat fautif dépend de leur juxtaposition, sans seed aléatoire.

Rejeu court, à partir des sorties sauvegardées :

```sh
python3 /home/matt/src/crug/scripts/validate-c2crug-report.py /home/matt/audit-crug-phase4/c2crug_nullguard_report.json
/home/matt/audit-crug-phase4/c2crug_nullguard_original
/home/matt/audit-crug-phase4/c2crug_nullguard_migrated
```

Le mécanisme diffère de R4-TOOL-001 : l'un manque un `if` au travers d'un label avant la libération ; l'autre associe une garde de nullité à un retour anticipé qui n'en dépend pas. Les deux montrent que les heuristiques locales du script ne suffisent pas à établir la preuve de chemin annoncée.

Compilation et instrumentation du second contre-exemple (toutes les sorties restent dans le dossier de phase 4) :

```sh
/home/matt/src/crug/gcc/crugc -O0 -fcrug-ownership -fcrug-borrow -fno-pie -c /home/matt/audit-crug-phase4/c2crug_nullguard/nullguard.c -o /home/matt/audit-crug-phase4/c2crug_nullguard/nullguard.original.o
/home/matt/src/crug/gcc/crugc -O0 -fcrug-ownership -fcrug-borrow -fno-pie -c /home/matt/audit-crug-phase4/c2crug_nullguard_safe/nullguard.c -o /home/matt/audit-crug-phase4/c2crug_nullguard_safe/nullguard.o
gcc -O0 -no-pie -Wl,--wrap=free -o /home/matt/audit-crug-phase4/c2crug_nullguard_original /home/matt/audit-crug-phase4/c2crug_nullguard/nullguard.original.o /home/matt/audit-crug-phase4/c2crug_harness.c
gcc -O0 -no-pie -Wl,--wrap=free -o /home/matt/audit-crug-phase4/c2crug_nullguard_migrated /home/matt/audit-crug-phase4/c2crug_nullguard_safe/nullguard.o /home/matt/audit-crug-phase4/c2crug_harness.c
valgrind --leak-check=full /home/matt/audit-crug-phase4/c2crug_nullguard_original
valgrind --leak-check=full /home/matt/audit-crug-phase4/c2crug_nullguard_migrated
```

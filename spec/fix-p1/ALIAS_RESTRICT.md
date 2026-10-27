# Exclusivité et faits d'alias — phase 4

Référence : `f86b7df26df4451e3d5952d7bb0fba9cff698b96`. Norme : `spec/000-PRECEDENCE.md` §2 ; ADR 0010 D2/D4/D7. Le paramètre `mut` reçoit un fait `restrict` seulement si l'appelant prouve une vue exclusive. Le modèle indépendant de cette matrice suit l'identité du pointeur dans des affectations C explicites. Pour les sept formes retenues, l'argument ordinaire de `probe` désigne certainement l'objet de `p`.

## Expérience

`alias/run_matrix.py` génère 12 formes de source et compile chacune à `-O0/-O1/-O2/-O3/-Os`, avec `-fcrug-alias-facts -fcrug-strict` puis `-fcrug-borrow -fcrug-strict` comme témoin sans faits d'alias. Chaque compilation produit son objet et son exécutable indépendant ; `alias/matrix.json` conserve commandes, retours, diagnostics, résultats et SHA-256 de la source. Budget par compilation/lien : 30 s ; exécution : 5 s. Temps cumulé mesuré des 120 lignes : 26,56 s. L'hôte GCC 13.3.0 ne sert qu'au lien des objets CRUG.

Les sept formes de même objet acceptées sont : copie directe dans un pointeur ordinaire, chaîne de copies, affectation conditionnelle, stockage dans un tableau, stockage dans un champ de structure, double indirection, et copie des octets du pointeur par `__builtin_memcpy`. Toutes compilent avec les faits activés aux cinq optimisations. Chaque forme produit le résultat attendu sans faits à cinq niveaux, et avec faits à `-O0`. Avec faits à `-O1/-O2/-O3/-Os`, les 28 exécutions retournent 1 : `probe` rend 1 alors que l'objet contient 2. `alias/pointer_struct_dump.crug-alias` atteste `alias probe: parameter a restrict (mut)`.

Le témoin à deux allocations distinctes compile et retourne 0 dans les dix configurations. Le passage direct de `p`, sa variante conditionnelle et `p+0` sont rejetés. L'appel via `identity(p)` est rejeté pour absence de contrat d'ownership du wrapper en mode strict ; ce refus ne démontre donc rien sur la preuve d'alias. Les formes rejetées ont été exclues du décompte des sept voies acceptées.

## Interprétation

Ces sept formes sont des **variantes de P3-003**, pas de nouveaux défauts indépendants. `gcc/c/crug-ownership.cc:3308-3347` compare le prêteur de `borrow_mut(p)` à la déclaration syntaxique de l'autre argument. Cette comparaison ne suit pas l'identité d'un pointeur copié dans un autre objet, tableau ou champ. Le fait `restrict` est pourtant émis sur le paramètre `mut`. Le résultat différent sous optimisation montre la conséquence possible d'une prémisse d'exclusivité non prouvée par le frontend ; il ne prouve pas une erreur de GCC sur un programme C défini. L'acceptation du contrat invalide est le défaut CRUG déjà enregistré sous P3-003.

L'ADR 0010 D8 exclut la preuve chez un appelant compilé sans le flag ; toutes les sources de cette matrice sont compilées **avec** le flag. Les cas d'appels indirects de P3-004 restent un mécanisme séparé. La matrice LTO complémentaire `alias/run_lto.py` a compilé et lié 16 configurations (les sept formes acceptées et le témoin disjoint, faits on/off) à O2 avec `-flto` et une limite de 60 s par build. Les 16 builds et exécutions ont réussi : les sept formes aliasées donnent retour 1 avec faits et 0 sans faits ; le témoin disjoint donne 0 dans les deux modes. `alias/lto/matrix.json` conserve chaque commande, durée, diagnostic et code retour. Le driver/plugin LTO est celui de CRUG GCC 16 ; le lien a utilisé `-nodefaultlibs -B/usr/lib/gcc/x86_64-linux-gnu/13/ -lgcc -lc -lgcc` pour les bibliothèques de démarrage absentes du build local, comme en phase 3. Aucun modèle général d'alias partiel, de plages chevauchantes ou de provenance FFI n'est revendiqué.

Rejeu : `python3 /home/matt/audit-crug-phase4/alias/run_matrix.py`. Les sorties restent dans ce dossier indépendant.

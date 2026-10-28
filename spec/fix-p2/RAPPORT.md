# Revue CodeRabbit de CRUG — c0dcb18

## Périmètre et provenance

- Révision examinée : c0dcb18d649a909846e8b2bcefe4eb39b1841dc9, comparée à f86b7df26df4451e3d5952d7bb0fba9cff698b96.
- Outil : CodeRabbit CLI 0.9.1, revue --deep --agent --committed --fresh --base-commit f86b7df26df4451e3d5952d7bb0fba9cff698b96. Une consigne locale limitait la revue au code spécifique à CRUG, y compris ses ajouts dans GCC ; GCC amont était hors périmètre.
- [Sortie brute](coderabbit-findings.txt) : 32 observations, 16 « major » et 16 « minor », aucune « critical ». Cinq paires signalent le même emplacement et la même cause, soit 27 groupes après dédoublonnage par emplacement.
- La revue porte sur les changements commités de cette plage. Le dossier audit/ non suivi n'a pas été soumis. Le niveau « major » de CodeRabbit n'est pas une cote P0/P1/P2 de la roadmap CRUG.

La sortie brute contient des propositions de correctifs automatiques. Elles sont conservées comme contexte, sans validation ni application. Ce rapport distingue les constats vérifiés des hypothèses à reproduire et des comportements déjà documentés.

## Constats vérifiés

### CR-01 — Durée de vie invalide dans le parseur FFI

**CodeRabbit : major. Preuve locale : lecture du code.** Dans [parse_argref](../../gcc/c-family/crug-ffi.cc), lignes 325–333, la branche arg N place n.c_str() dans digits, puis détruit la chaîne locale n avant de parcourir digits. La lecture ultérieure utilise donc un pointeur hors de la durée de vie de son objet. Le défaut de durée de vie est certain dans la source ; le résultat dépend de la mémoire réutilisée. Aucun échec de compilation ni crash n'a été reproduit pendant cette vérification. La forme argN passe par la chaîne w, dont la durée de vie couvre la boucle.

**Impact possible :** métadonnées FFI arg N acceptées ou rejetées de manière instable, voire crash du compilateur. **Criticité CRUG : à établir par un témoin dynamique.**

### CR-02 — Faux diagnostic après un switch avec default

**CodeRabbit : major. Preuve locale : témoin compilé et contrôle.** Dans [l'analyse du switch](../../gcc/c/crug-ownership.cc), lignes 4915–4927, merged démarre avec l'état antérieur même quand un default couvre tous les chemins et que chaque bras termine par break. L'état d'entrée impossible est ensuite joint aux états de sortie. Un programme où les deux bras détruisent p a produit crug-UAM-001 et crug-DROP-006 ; la forme if/else équivalente a été acceptée. Ces commandes lisent les sources sur l'entrée standard et n'écrivent aucun fichier :

~~~sh
gcc/crugc -x c -fsyntax-only -O2 -fcrug-ownership -Wno-crug-unadopted-own - <<'C'
#include <stdlib.h>
void f(int c) {
  own int *p = malloc(4);
  switch (c) { case 0: drop(p); break; default: drop(p); break; }
}
C
~~~

Résultat observé : code de sortie 1 ; crug-UAM-001 sur le second bras et crug-DROP-006 à la déclaration de p. Le contrôle remplace la ligne switch par « if (c) { drop(p); } else { drop(p); } » ; code de sortie 0, aucun diagnostic. Le cc1 utilisé avait l'empreinte SHA-256 6c45ed968b2121f5f02d50aa3f72f94c7b5288c744a5897ff1f014d21fdaaa2f.

**Impact observé :** rejet d'un programme valide. Aucune exécution erronée de programme compilé n'a été démontrée. Cette cause n'est pas assimilée sans preuve à F-01, qui concernait les expressions conditionnelles des campagnes précédentes.

### CR-03 — Provenance des mesures de couverture insuffisamment vérifiée

**CodeRabbit : major. Preuve locale : lecture du script.** [run-coverage.sh](../../scripts/run-coverage.sh), lignes 138–141, échoue si un champ commit présent diffère de HEAD, mais laisse passer un champ absent. L'archive produite peut ainsi porter l'étiquette de HEAD sans que les entrées déclarent la révision mesurée. Le chemin de génération complet et l'acceptation par les autres contrôles n'ont pas été rejoués. **Impact :** fiabilité de la traçabilité des mesures, sans défaut démontré du compilateur.

## Pistes techniques à reproduire

| Emplacement | Observation et preuve manquante |
|---|---|
| [crug-ownership.cc](../../gcc/c/crug-ownership.cc), 2140–2159 | Une écriture indirecte de variable pointeur pourrait laisser des racines d'alias périmées, puis conduire -fcrug-alias-facts à émettre un restrict injustifié. Seule alerte « Security » de CodeRabbit. Il faut un programme accepté, une preuve des attributs émis et un résultat divergent sous optimisation avant d'affirmer une corruption ou un P0. |
| [crug-ownership.cc](../../gcc/c/crug-ownership.cc), 2325–2332 | Un emprunt présent sur un seul chemin perd son ensemble de prêteurs au join ; une recherche de prêt pourrait ensuite attribuer ce prêt à un autre propriétaire. Le vidage de l'ensemble est visible, mais le faux diagnostic proposé n'a pas été rejoué. |
| [crug-ownership.cc](../../gcc/c/crug-ownership.cc), 2131–2132 | move(x) ou une allocation passée à un paramètre own pourrait produire des racines « inconnues » et rejeter un appel valide. Aucun témoin compilé pour cette revue. |
| [c2crug](../../c2crug), 951–955 | Deux retours sur une même ligne pourraient faire prendre la garde du premier pour la couverture du second, puis autoriser une migration --safe dangereuse. Il faut vérifier la sortie migrée et l'exécuter sous ASan ; sa proximité avec S-02 ne prouve pas une même cause. |
| [task.c](../../libcrug/core/task.c), 1085–1096 | Un second appel d'arrêt attend au plus 60 s puis retourne 3, valeur documentée comme échec de join. La condition de timeout est visible ; il manque un témoin de vidange normale supérieure à 60 s. |
| [task.c](../../libcrug/core/task.c), 861–863 et 975–976 | CodeRabbit soupçonne une perte de réveil dans l'échange sleepers/epoch avec les ordres mémoire actuels. Argument de modèle mémoire uniquement ; aucun blocage ni entrelacement démonstratif fourni. |
| [.github/workflows/ci.yml](../../.github/workflows/ci.yml), 23–27 | Une exécution planifiée pourrait devenir le dernier workflow de HEAD, tandis que check-ci-gate.sh exige son succès. À vérifier sur l'historique Actions et la politique de sélection du gate. |

## Autres observations et triage

| Groupe | Statut dans cette revue |
|---|---|
| [select send](../../gcc/c/c-parser.cc), ligne 33250 | CodeRabbit signale l'évaluation de la valeur avant le résultat de claim. Cette limite est explicitement annoncée dans [RELEASE-NOTES.md](../../docs/crug/RELEASE-NOTES.md), lignes 167–172. Ce n'est pas présenté comme une nouvelle découverte. |
| [spawn pendant shutdown](../../libcrug/core/task.c), lignes 964–969 | L'abort crug-CONC-029 est explicitement annoncé dans [TASKS.md](../../docs/crug/TASKS.md), lignes 195–199. L'alerte décrit le contrat actuel. |
| [skill CRUG](../../spec/skills/crug/SKILL.md), lignes 26, 155, 162–167 | Trois alertes documentaires : garantie d'alias, appel de shutdown depuis une tâche et ancien problème des sends de capacité zéro. Elles s'appuient en partie sur des rapports antérieurs ; leur actualité doit être comparée aux corrections de c0dcb18 avant de les retenir. L'alerte de la ligne 155 est présente deux fois. |
| [gate.sh](../../scripts/gate.sh), lignes 245 et 270–274 | Deux risques distincts, visibles dans la source : substitution de «  N » supprimant l'espace, et *, ? et barre oblique inverse non échappés comme littéraux dans une expression régulière. Aucun rejeu complet du gate. |
| [documentation FFI](../../docs/crug/FFI.md), lignes 254–255 | La prose laisse croire que ret porte un indice, contrairement au parseur. Alerte présente deux fois. |
| [documentation PAL](../../docs/crug/PAL.md), lignes 119–125 ; [ADR 0011](../../docs/crug/adr/0011-pal-contract.md), lignes 180–186 | Noms du treizième cas, valeur de retour join(NULL) et tableau de version signalés comme périmés. Écarts documentaires à confronter au test et aux quatre PAL. |
| [CHANNELS.md](../../docs/crug/CHANNELS.md), lignes 90–150 et 224–227 | Sections anciennes décrivant encore le probe et le buffer par thread, alors que la nouvelle section décrit le claim et le scratch par tâche. |
| [BORROW.md](../../docs/crug/BORROW.md), lignes 219–223 | Règle de join décrite plus strictement que l'implémentation signalée. |
| [RELEASE-POLICY.md](../../docs/crug/RELEASE-POLICY.md), lignes 83–89 ; [raytracer/README.md](../../bench/raytracer/README.md), lignes 111–112 | Texte de performance contredit par des mesures ou par le cache d'allocations. |
| [TESTING.md](../../docs/crug/TESTING.md), lignes 139–142 ; [CONCURRENCY.md](../../docs/crug/CONCURRENCY.md), ligne 272 ; [MIGRATION.md](../../docs/crug/MIGRATION.md), lignes 127–144 | Respectivement rôle annoncé de run-coverage.sh, nombre de tests ancien, et titre inséré avant les puces de la section précédente. |

Les cinq paires de doublons portent sur CR-01, CR-02, la piste des prêteurs conditionnels, l'alerte du skill sur shutdown et la documentation FFI. Les 32 observations brutes ne représentent donc pas 32 causes indépendantes. Les écarts documentaires ne démontrent pas, à eux seuls, un défaut d'exécution.

## Limites et état du dépôt

Cette revue de diff ne remplace pas les trois [campagnes d'audit précédentes](../2026-10-27-adversarial-3-c0dcb18/RAPPORT.md). Elle n'a pas rejoué leurs matrices, les autres PAL, DejaGNU ou TSan. Elle ne permet pas de conclure que les défauts P0 déjà rapportés sont résolus, ni que toutes les pistes CodeRabbit sont réelles.

Après la revue et les deux compilations de témoin sur stdin : HEAD était toujours c0dcb18d649a909846e8b2bcefe4eb39b1841dc9 ; git status --short --branch indiquait « ## main...origin/main » et « ?? audit/ » uniquement. Aucun fichier de production suivi n'a été modifié, aucun correctif ni commit créé.


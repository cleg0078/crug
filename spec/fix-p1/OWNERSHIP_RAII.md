# Propriété, transferts et destruction — phase 4

Référence : `f86b7df26df4451e3d5952d7bb0fba9cff698b96`. Règles examinées : `spec/000-PRECEDENCE.md` §2, ADR 0003/0004/0005 et `docs/crug/DROP.md` §§2–4. Le compteur indépendant `raii/run_matrix.py` enregistre dans le destructeur un identifiant par ressource ; son nombre et l'ordre des identifiants sont l'oracle d'exécution. Chaque pointeur provient de `malloc` et est détruit par `recfree`.

## Matrice exécutée

Treize formes valides : ordre inverse des déclarations, scopes imbriqués, branche avec et sans ressource locale, retour anticipé, `goto` sortant, `switch/break`, boucle avec `break`, boucle avec `continue`, `move`, `drop` explicite, valeur de retour évaluée avant destruction, sortie anticipée après `move`, et transfert dans les deux branches. Ces formes comportent 20 exécutions de chemins distincts par niveau, puisque certaines sources testent les deux valeurs de condition.

Les 13 sources compilent et les compteurs attendus sont observés à `-O0`, `-O2` et `-O3` (39 compilations/exécutions). Chaque source a aussi été compilée et exécutée à `-O0 -fsanitize=address,undefined` (13 cas, aucun signalement dans les journaux). Quatre formes invalides — double drop, destruction pendant emprunt vivant, réaffectation d'un propriétaire déjà actif, et utilisation après jonction entre état owned et moved — sont rejetées aux trois optimisations avec respectivement `crug-UAM-001`, `crug-BORROW-005`, `crug-DROP-008` et `crug-UAM-004` (12 rejets attendus). Valgrind Memcheck sur quatre représentants (scopes imbriqués, continue, sortie après move, goto) retourne 0 sans erreur ni fuite ; commande et sorties dans `raii/valgrind.json`. Total : 64 lignes dans `raii/matrix.json`. Les commandes, sorties et SHA-256 des sources s'y trouvent ; les délais maximaux sont de 30 s par compilation et lien, 10 s par exécution.

Extension LTO : `raii/run_lto.py` a compilé et lié les 13 formes valides à `-O2 -flto`, puis exécuté leurs contrôles de compte et d'ordre intégrés. Résultat **13/13 compilations et 13/13 exécutions conformes**, sans timeout ; `raii/lto/matrix.json` conserve commandes, SHA-256, durées et codes de retour. Le driver et le plugin LTO sont ceux de CRUG GCC 16 ; le lien utilise les bibliothèques de démarrage du GCC hôte 13 avec `-nodefaultlibs -B/usr/lib/gcc/x86_64-linux-gnu/13/ -lgcc -lc -lgcc`, car le build GCC local n'installe pas ses bibliothèques cible. Cette extension ne remplace pas les mesures ASan/UBSan ni Valgrind.
La première version du banc avait classé à tort `if(x) { b=move(a); return; } return *a;` comme une utilisation après une jonction ambiguë. Le chemin qui déplace `a` quitte la fonction avant la lecture, donc la source est valide. La première mesure est conservée dans `raii/matrix_initial.json` ; `early_return_after_move` est maintenant un témoin positif, et `maybe_moved_join` couvre une véritable jonction réalisable. Il s'agit d'une erreur de l'oracle initial, pas d'un défaut CRUG. Le banc corrigé a été rejoué intégralement.

## Portée

Ces essais prouvent seulement les 13 formes et quatre refus ci-dessus. Ils ne couvrent pas exhaustivement les tableaux d'owned, agrégats imbriqués, captures de tâches, types algébriques, FFI, toutes les permutations de `goto`, ni tous les échecs d'allocation. Les limites documentées (mouvements partiels et payloads non suivis) ne sont pas présentées comme des défauts.

Rejeu : `python3 /home/matt/audit-crug-phase4/raii/run_matrix.py`. Toutes les écritures restent dans le dossier d'audit indépendant.



# Constats de phase 4

Référence commune : `f86b7df26df4451e3d5952d7bb0fba9cff698b96`, CRUG 0.1.0/GCC 16.2.0, Ubuntu-CRUG 24.04 x86-64. `CONFIRMED` porte sur le mécanisme et l'interleaving reproduit ; aucune exploitabilité mémoire n'est inférée. Les 17 constats indépendants des phases 2 et 3 sont conservés séparément et non recomptés.

## R4-RT-001 — CONFIRMED / HIGH — l'émetteur d'un canal capacité zéro attend la valeur d'un autre émetteur

1. **Identifiant :** R4-RT-001.
2. **Sévérité :** HIGH, car un envoi déjà reçu peut rester bloqué sans borne si une valeur postérieure n'est jamais reçue.
3. **Niveau de preuve :** CONFIRMED ; interleaving imposé, 20/20 répétitions natives, 5/5 ASan+UBSan, 1/1 TSan et 1/1 Valgrind, témoin 5/5, puis rejeu indépendant contre les objets runtime originaux.
4. **Commit :** `f86b7df26df4451e3d5952d7bb0fba9cff698b96`.
5. **Module :** `libcrug/core/chan.c:160-195` et `:236-242`.
6. **Invariant :** sur un handoff de capacité zéro, l'envoi de A doit pouvoir se terminer une fois **sa** valeur prise par un récepteur. La présence d'une valeur ultérieure de B ne doit pas retenir A.
7. **Source normative :** `docs/crug/adr/0025-channels-and-select.md` D2 (« synchronous handoff »), `docs/crug/CHANNELS.md` §1 ; le commentaire d'implémentation de `chan.c:182-184` précise la condition de complétion prévue.
8. **Cause racine :** après le dépôt, le send teste le `count` global du canal (`:185-195`), sans associer l'attente au dépôt qu'il a effectué. Le récepteur de A met `count` à zéro, mais B peut le remettre à un avant la prochaine lecture d'A.
9. **Programme minimal :** `runtime/handoff_sender_identity.c`. Un wrapper de `crug_pal_sleep_ns` ne modifie pas le runtime ; il impose l'ordre A dépose 11 → réception de 11 → B dépose 22 → A relit le compte. Valeurs et ordres sont fixes.
10. **Seed/paramètres :** interleaving déterministe, valeurs 11 et 22, capacité 0, taille `int`, `CRUG_TASKS_WORKERS=1` ; aucune génération aléatoire.
11. **Prédiction de l'oracle :** après réception de 11, A n'a plus de valeur en transit et son envoi doit se terminer même si 22 demeure dans le canal. Cette prédiction vient de l'identité des valeurs déposées et reçues, pas d'un diagnostic CRUG.
12. **Résultat CRUG :** avant la seconde réception, `first=11 len=1 A_sleeps=2 B_sleeps=1 A_returned=0`. Après réception de 22, A et B terminent. Le programme retourne 77 pour signaler le retard indu.
13. **Reproduction :** `python3 /home/matt/audit-crug-phase4/runtime/run_matrix.py` ; puis `CRUG_TASKS_WORKERS=1 /home/matt/audit-crug-phase4/runtime/bin/handoff_sender_identity-plain`. La commande de compilation intégrale est `compile_handoff_sender_identity_plain` dans `runtime/logs/matrix.json`. Le rejeu contre les objets originaux figure dans `runtime/logs/handoff_original_objects.json`.
14. **Logs :** `runtime/logs/handoff_sender_identity_w1_00.stdout` à `_19.stdout`, journaux sanitizer, TSan, Valgrind, et JSON ci-dessus. Le programme réduit a aussi été recompilé indépendamment contre les fichiers source originaux, lus sans modification, avec le même état observé.
15. **Témoins :** `runtime/handoff_control.c` avec un seul expéditeur donne `before_receive_returned=0 after_receive_returned=1 len=0` et réussit 5/5 ; la seconde réception dans le contre-exemple libère les deux expéditeurs.
16. **Variantes :** seule la fenêtre à deux expéditeurs est confirmée ici ; aucun défaut de fermeture ou de `select` n'est inféré.
17. **Relation antérieure :** distinct d'AUD-002 et AUD-004, qui concernent le sondage/lowering de `select`. Ce programme n'utilise ni `select` ni readiness. Distinct du buffer TLS AUD-001.
18. **Conséquence technique :** perte de progression d'un appel `send` dont la propre valeur a déjà été livrée. Aucun UAF, double free, ni défaut exploitable n'est démontré par ce cas.
19. **Correction recommandée, non appliquée :** suivre une génération ou une réservation propre à chaque dépôt et signaler la consommation à l'émetteur correspondant ; conserver la synchronisation du cas fermé.
20. **Régression suggérée :** deux producteurs et une seule réception, barrières aux trois points d'interleaving, puis cas témoin à une émission et matrice 1/plusieurs workers.

## R4-TOOL-001 — CONFIRMED / MEDIUM — c2crug --safe prouve à tort une libération conditionnelle

1. **Identifiant :** R4-TOOL-001.
2. **Sévérité :** MEDIUM : la migration annoncée comme sûre peut produire un fichier refusé par CRUG et un rapport de preuve faux ; le compilateur bloque ici la sortie invalide.
3. **Niveau de preuve :** CONFIRMED ; source réduite, contrôle dynamique des deux branches, rapport JSON structurellement valide, deux optimisations, témoins positif/négatif.
4. **Commit :** `f86b7df26df4451e3d5952d7bb0fba9cff698b96`.
5. **Module :** `c2crug:257-269` (`unbraced_conditional`) et `c2crug:489-539` (`analyse_source`).
6. **Invariant :** `--safe` ne doit insérer `own` que si la libération est unique sur tous les chemins atteignables ; une libération dans le corps de `if` sans accolades reste conditionnelle même précédée d'un label.
7. **Source normative :** ADR 0023 D1/D6 et `docs/crug/C2CRUG.md` §§1–3, qui exigent la preuve structurelle et le classement `raw` des cas conditionnels.
8. **Cause racine :** le détecteur de contrôle sans accolades n'inspecte que la ligne non vide immédiatement avant `free(p)`; il voit `label:` et manque le `if (c)` antérieur. Le test de profondeur d'accolades ne distingue pas ces chemins.
9. **Programme minimal :** `c2crug/conditional_label.c`, huit lignes ; le `label:` sépare `if (c)` de `free(p)`.
10. **Seed/paramètres :** aucun aléa ; `c=0` et `c=1`, `--safe`, compilation O0/O2 avec `-fcrug-ownership -fcrug-borrow`.
11. **Prédiction indépendante :** en C, l'énoncé étiqueté est le corps du `if`. La libération n'est donc pas exécutée lorsque `c=0` et la conversion doit être classée `raw`.
12. **Résultat observé :** `--safe` produit `own int *p` et `drop(p)`, déclare `confidence=1.00`/`applied=true` ; CRUG rejette le fichier généré à O0 et O2 avec `[crug-DROP-006]`.
13. **Reproduction :** `python3 /home/matt/src/crug/c2crug --diff /home/matt/audit-crug-phase4/c2crug` ; commandes exactes et rejeu borné dans `C2CRUG_MIGRATION.md`.
14. **Logs :** `c2crug_safe_report.json`, `c2crug_safe/conditional_label.c`, `c2crug_compile_O0.stderr`, `c2crug_compile_O2.stderr`, `c2crug_original_control.stdout`.
15. **Témoins :** C original compilé sous les mêmes flags ; wrapper `free` : `after_false=0`, `after_true=1`. Sans label, `if_no_label.c` est correctement `raw` ; `straight.c` est correctement `own` et sa sortie compile.
16. **Variantes :** un `else` sur la ligne précédant `free(p)` contourne aussi `unbraced_conditional` : `c2crug_else/else_release.c` est marqué `own` à confiance 1,00 puis rejeté par CRUG avec DROP-006. Même cause : le contrôle sans accolades n'est pas reconnu.
17. **Relation antérieure :** aucune cause c2crug parmi les 17 défauts indépendants des phases 2/3 ; distinct des faux succès de gates AUD-003/009, car l'erreur est la décision de migration elle-même.
18. **Conséquence technique :** rapport de preuve invalide et sortie `--safe` inutilisable pour ce C d'entrée valide ; aucune acceptation unsafe ni corruption mémoire du compilateur n'est démontrée.
19. **Correction recommandée, non appliquée :** faire porter le test de libération sur un flot de contrôle capable de traverser les labels, ou classer conservativement `raw` dès qu'un label sépare le contrôle de l'appel de libération.
20. **Régression suggérée :** cas `if` sans accolades avec et sans label, branche vraie/fausse, validation du rapport, compilation de la sortie `--safe` ; garder un témoin inconditionnel.

## R4-TOOL-002 — CONFIRMED / MEDIUM — c2crug --safe attribue une garde de nullité au mauvais retour

1. **Identifiant :** R4-TOOL-002.
2. **Sévérité :** MEDIUM : la migration modifie silencieusement la durée de vie d'une allocation sur un chemin réalisable, malgré sa promesse d'équivalence ; aucun UAF du résultat n'est démontré.
3. **Niveau de preuve :** CONFIRMED ; rapport structurellement valide, objets originaux/migrés compilés, comptage des appels `free`, Valgrind, témoins indépendants et O0/O2.
4. **Commit :** `f86b7df26df4451e3d5952d7bb0fba9cff698b96`.
5. **Module :** `c2crug:245-255` (`is_null_guard`) et `c2crug:510-515` (`analyse_source`).
6. **Invariant :** l'exception de sortie anticipée n'est valable que pour le `return` effectivement conditionné par `p == NULL` ; un autre `return` doit empêcher l'insertion de `own`.
7. **Source normative :** ADR 0023 D1/D6 ; `docs/crug/C2CRUG.md` §§1–3, règle « No early escape » et sortie `--safe` équivalente.
8. **Cause racine :** `is_null_guard` cherche un texte `if (!p)` dans une fenêtre de quatre lignes, sans vérifier que ce `if` contrôle le `return` courant. La première garde blanchit un second retour indépendant.
9. **Programme minimal :** `c2crug_nullguard/nullguard.c` : `if (!p) return 0; if (c) return 1; free(p);`.
10. **Seed/paramètres :** aucun aléa ; `c=0` et `c=1`, O0 pour la mesure des destructions, O2 pour l'acceptation compilateur.
11. **Prédiction indépendante :** si l'allocation réussit et `c=1`, le C d'origine quitte sans `free(p)`; ce cas doit rester `raw` suivant la règle de preuve, même si cette source fuit.
12. **Résultat observé :** `--safe` marque `own` confiance 1,00 et la sortie compile. Le témoin original donne 1 appel `free` après les deux appels `f(0), f(1)` ; le migré en donne 2. Valgrind : 4 octets définitivement perdus contre 0.
13. **Reproduction :** `python3 /home/matt/src/crug/c2crug --diff /home/matt/audit-crug-phase4/c2crug_nullguard` puis exécuter les deux binaires de `C2CRUG_MIGRATION.md`.
14. **Logs :** `c2crug_nullguard_report.json`, sources originales/migrées, `c2crug_nullguard_{original,migrated}.stdout` et `*.valgrind.stderr`.
15. **Témoins :** sans garde précédente, `early_no_nullguard.c` reste `raw` ; une véritable garde seule, `legit_nullguard.c`, est `own`. Les deux cas de base sont classés correctement.
16. **Variantes :** seule la fenêtre de trois lignes et le retour indépendant ont été confirmés ; aucune autre distance ou syntaxe de garde n'est revendiquée.
17. **Relation antérieure :** distinct de R4-TOOL-001, qui manque un contrôle masqué par label au niveau de la libération ; ici un contrôle non lié est associé à un retour. Aucun équivalent parmi les 17 causes antérieures.
18. **Conséquence technique :** transformation acceptée mais non équivalente en durée de vie ; le programme d'origine fuite, la sortie RAII libère à la sortie. Aucune sûreté mémoire générale ni exploitabilité n'est inférée.
19. **Correction recommandée, non appliquée :** associer chaque garde de nullité à la sortie qu'elle contrôle dans un CFG minimal, ou classer `raw` lorsque cette relation n'est pas démontrée.
20. **Régression suggérée :** garde de nullité puis sortie conditionnelle indépendante avant `free`, comparer les deux valeurs de `c`, comptage des destructions, témoins avec chaque garde isolée et validation du rapport.

## R4-RT-002 — LIKELY / contrat à clarifier — shutdown invoqué par une tâche

1. **Identifiant :** R4-RT-002.
2. **Sévérité :** HIGH si l'appel depuis une tâche est autorisé ; cette qualification dépend du contrat à clarifier.
3. **Niveau de preuve :** LIKELY comme défaut public. L'état incorrect et les timeouts sont reproduits, mais la validité de l'appel depuis une tâche n'est pas explicitée. Ce cas n'entre pas dans le total des défauts confirmés.
4. **Commit :** f86b7df26df4451e3d5952d7bb0fba9cff698b96.
5. **Module :** libcrug/core/task.c:568-610, task.c:323 et libcrug/pal/linux/pal_linux.c:133-143.
6. **Invariant :** si l'API autorise un shutdown depuis une tâche, l'arrêt ne doit ni joindre le worker appelant, ni remettre le compteur live à zéro avant la fin de cette tâche.
7. **Source normative :** libcrug/core/task.h:18-24 montre surtout l'arrêt depuis main, sans interdire formellement l'appel depuis une tâche ; cette lacune empêche de conclure au contrat public.
8. **Cause racine observée :** le scheduler tente de joindre tous les workers, y compris l'appelant ; le PAL ignore l'échec de pthread_join et libère son handle. Avec un worker, live est remis à zéro puis décrémenté à la fin de la tâche.
9. **Programme minimal :** runtime/self_shutdown.c, un appel de shutdown lancé par une tâche, sans wrapper.
10. **Seed/paramètres :** interleaving reproductible sans aléa, CRUG_TASKS_WORKERS=1, 2 et 4 ; cinq répétitions à un worker, trois à deux et quatre workers.
11. **Prédiction indépendante :** si cet appel est valide, la tâche doit terminer sans corruption de compteur ni attente circulaire ; si le contrat l'interdit, il faudrait au minimum le préciser avant de qualifier un défaut.
12. **Résultat CRUG :** à un worker, 5/5 sorties avec live=4294967295 et lost=1 ; à deux et quatre workers, 3/3 timeouts par configuration après 2 s.
13. **Reproduction :** python3 /home/matt/audit-crug-phase4/runtime/run_matrix.py ; la commande de compilation et les paramètres exacts sont dans runtime/logs/matrix.json.
14. **Logs :** runtime/logs/self_shutdown_w{1,2,4}_*.{stdout,stderr}, matrix.json et self_shutdown_original_objects.json.
15. **Témoins :** runtime/scheduler_controls.c arrête depuis le thread principal, termine à 1, 4 et 8 workers avec spawned=completed=3000 et live=lost=0. Le contre-exemple a aussi été lié aux objets runtime originaux.
16. **Variantes :** un worker expose le sous-débordement ; plusieurs workers exposent l'attente circulaire. Aucun autre scénario d'auto-arrêt n'est revendiqué.
17. **Relation antérieure :** distinct d'AUD-005, qui exige deux shutdowns simultanés, et de P3-007, qui concerne spawn concurrent à l'arrêt ; ici un seul appel vient d'une tâche déjà active.
18. **Conséquence technique :** état du compteur invalide ou blocage si l'appel est dans le contrat. Aucun UAF ni double free n'est déduit.
19. **Correction recommandée, non appliquée :** clarifier si une tâche peut demander l'arrêt ; si oui, prévoir une transition sans jointure de soi-même, et traiter l'échec de pthread_join avant libération du handle.
20. **Régression suggérée :** arrêt depuis main et depuis tâche, 1/2/4 workers, compteur live/lost, timeout borné et rejeu sur le PAL concerné.

## Candidats écartés et variantes

Les sept formes d'alias confirmées dans `ALIAS_RESTRICT.md` sont des variantes de P3-003. Une première prédiction RAII sur un `move` dans une branche quittée par `return` était erronée ; `OWNERSHIP_RAII.md` documente sa correction. Le simple déréférencement d'un `own` non initialisé ne reçoit pas d'ID sans obligation normative non ambiguë ; l'oracle le classe `UNKNOWN`.

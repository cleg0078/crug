# Validation de l'oracle — phase 4

## Tests de référence indépendants

Commande : python3 -m unittest discover -s /home/matt/audit-crug-phase4/oracle -p 'test_*.py' -v. Résultat final : 21 tests, 0 échec, environ 0,002 s de calcul Python. Les 18 tests de test_model.py sont des jugements manuels écrits avant les campagnes massives ; trois tests de test_metamorphic.py vérifient des propriétés internes.

| Cas vérifié à la main | Règle attendue |
| --- | --- |
| Deux propriétaires vivants | destruction automatique en ordre inverse, valeurs 2 puis 1 |
| Lecture du propriétaire vivant | lecture 1, acceptation |
| Lecture d'un ref puis drop du prêteur | acceptation après dernière utilisation |
| Drop ou move avant la dernière lecture du ref | rejet |
| ref et mut vivants sur une ressource | rejet |
| Deux ref vivants sur une ressource | acceptation |
| Move vers un propriétaire vide, puis lecture de la destination | acceptation et un seul drop |
| Réaffectation d'un ref dans les deux bras vers deux prêteurs | rejet du drop de tout prêteur encore possible |
| Drop d'un propriétaire indépendant après jonction | acceptation |
| Move conditionnel unilatéral | rejet de la responsabilité ambiguë en sortie |
| Move identique dans les deux bras | acceptation |
| Copie owned implicite | rejet |
| Écriture par ref ; écriture par mut | rejet ; acceptation |
| Usage après move | rejet |
| Alias brut ; lecture EMPTY ambiguë | UNKNOWN |

Les trois propriétés métamorphiques côté modèle sont le renommage bijectif des bindings locaux, l'inversion simultanée des bras et de la condition, et la permutation de deux lectures indépendantes. Leurs préconditions sont codées dans test_metamorphic.py ; elles ne couvrent pas les transformations susceptibles de modifier les durées de vie ou l'ordre des drops.

## Vérification métamorphique du compilateur

oracle/metamorphic_compiler.py a sélectionné 12 programmes ACCEPT sans alias brut parmi les 1 000 premiers du corpus final : quatre sans branche et huit avec branche. Il a comparé 12 paires de renommage alpha et huit paires « bras inversés + prédicat négatif ». Les 32 variantes ont été compilées sous -O0 -fcrug-borrow -fcrug-strict, liées avec GCC hôte -no-pie puis exécutées sur 140 couples variante/masque faisables. Résultat final : 32/32 compilations et liens, 140/140 exécutions réussies, zéro différence de sortie avec la base. Les sources exactes, journaux, résultats JSONL et résumé résident dans oracle/metamorphic/.

Un premier essai du script de renommage avait modifié le spécificateur %d dans les chaînes printf et produit de faux écarts ; il a été corrigé pour limiter le renommage aux bindings avant main, puis les 32 variantes ont été régénérées et toutes rejouées. Aucun résultat de ce premier essai n'entre dans les mesures finales. Cette correction concerne le harness indépendant, pas CRUG.

## Première tranche comparée au compilateur

Le corpus raffiné de 4 096 est un préfixe bit-à-bit du corpus final de 10 000 pour son manifeste : SHA-256 des 4 096 premières lignes dans chacun = 359de9b8359fd714a616f9cf0785bc3e3470abe4eddec4d78e1d8011ed77df9d. La comparaison bornée des 1 000 premiers cas a réellement duré 110,842 secondes :

| Prédiction / résultat CRUG | Nombre |
| --- | ---: |
| ACCEPT / ACCEPT | 333 |
| REJECT / REJECT | 566 |
| REJECT / ACCEPT | 19 |
| UNKNOWN / ACCEPT | 82 |
| Total compilé | 1 000 |

Les 333 programmes ACCEPT/ACCEPT ont tous été liés et exécutés pour tous leurs masques faisables : 1 056 exécutions, aucune différence de somme des lectures ou d'ordre de destruction ; aucun échec de lien. Les 566 rejets concordants portent chacun au moins un code CRUG, et aucun ne repose uniquement sur une erreur C sans code CRUG. Le comptage par présence de code (plusieurs codes peuvent figurer dans un même cas) est : UAM-001 300 ; BORROW-005 122 ; BORROW-004 90 ; DROP-008 86 ; BORROW-016 44 ; DROP-007 39 ; DROP-006 39 ; BORROW-015 23 ; UAM-004 21. Voir oracle/corpus_v2_seed41620/diagnostic_review.json et les journaux bruts.

Les 19 REJECT/ACCEPT appartiennent tous à la famille join_lender et utilisent la même forme causale que P3-001 : ref local réaffecté sur les deux bras vers deux propriétaires, drop d'un prêteur possible après jonction, puis lecture du ref. Ce sont des variantes du défaut connu ; aucun nouvel identifiant n'est proposé. Les 82 UNKNOWN ne sont pas utilisés comme preuve de conformité ou d'anomalie.

## Contrôles de qualité et limites

Le pilote initial pilot/ a révélé une collision entre le paramètre de condition et un propriétaire nommé c ; le générateur a été corrigé avant les mesures finales. Le premier essai de liaison avec GCC hôte échouait sur PIE ; la commande finale emploie -no-pie. Ces erreurs de harness ne sont pas des défauts CRUG et leurs premiers résultats ne figurent pas dans le tableau. Le corpus préliminaire corpus_seed41620 retenait des lectures EMPTY à verdict incertain ; la version raffinée les évite dans les cas ordinaires et conserve UNKNOWN pour les alias bruts.

Aucune erreur de prédiction de l'oracle n'a été identifiée sur les cas validés ; cela décrit l'échantillon seulement. Une acceptation CRUG d'un cas REJECT reste une piste tant que la source normative, le C généré et la cause racine n'ont pas été revérifiés. Les sorties d'exécution ne vérifient pas les programmes rejetés et n'établissent aucune sûreté mémoire générale.

## Campagne complète de 10 000 cas

Les dix tranches de 1 000 ont été achevées et oracle/aggregate_campaign.py a vérifié l'ordre, l'unicité et la complétude des identifiants case_00001 à case_10000. L'agrégat brut est oracle/corpus_10000_seed41620/campaign_aggregate.json. Les drivers séquentiels ont totalisé 1 329,563 secondes de temps mural (22 min 9,563 s), avec un timeout de 12 s par compilation ou exécution.

| Prédiction / résultat CRUG | Nombre final |
| --- | ---: |
| ACCEPT / ACCEPT | 3 122 |
| REJECT / REJECT | 5 807 |
| REJECT / ACCEPT | 191 |
| UNKNOWN / ACCEPT | 880 |
| Total compilé | 10 000 |

Les 3 122 programmes ACCEPT/ACCEPT ont tous été liés ; les 10 210 exécutions correspondant à tous les masques faisables ont toutes produit exactement la somme de lectures et la séquence de destructions prédites. Aucun échec de lien ni écart d'exécution. Les 5 807 rejets concordants portent tous au moins un code CRUG ; aucun ne contient une ligne d'erreur C ordinaire supplémentaire. Les journaux ne contiennent aucun indicateur d'ICE ni timeout. Les 191 écarts de compilation relèvent tous de join_lender, la même perte de prêteur à la jonction que P3-001 ; aucun nouvel identifiant indépendant n'en résulte. Les 880 UNKNOWN ont été compilés, mais leur acceptation ne tranche pas le contrat des alias bruts.

Les occurrences de codes sur les 5 807 cas rejetés (un cas peut porter plusieurs codes) sont : UAM-001 2 884 ; BORROW-005 1 344 ; BORROW-004 872 ; DROP-008 958 ; BORROW-016 489 ; DROP-006 422 ; DROP-007 422 ; UAM-004 248 ; BORROW-015 149. La famille générée implicit_copy écrase aussi un propriétaire déjà actif et ne constitue pas, seule, un test isolé de copie. Le témoin oracle/manual_controls/implicit_copy_empty.c écrit dans une destination EMPTY et reçoit crug-UAM-003 ; son témoin move_into_empty.c compile. Les sources et journaux sont conservés dans le même dossier.

Le dernier journal des 21 tests Python est oracle/tests_final.log. Les 12 paires alpha et huit paires d'inversion de branche côté compilateur restent une campagne séparée et réussie (32 variantes, 140 exécutions). Aucune erreur de prédiction de l'oracle n'a été identifiée sur ce corpus borné ; ce résultat ne valide pas les constructions hors du sous-ensemble défini.


# Corpus généré et campagne de comparaison — phase 4

## Corpus final et reproductibilité

Le corpus final est oracle/corpus_10000_seed41620/. Générateur Python : oracle/generate.py ; seed décimale 41620 ; 11 285 tentatives pour conserver 10 000 programmes abstraits distincts. Chaque entrée possède case_NNNNN.json (programme abstrait), case_NNNNN.c (source CRUG), une prédiction précompilation et les SHA-256 de la forme abstraite canonique et de la source dans manifest.jsonl. summary.json et structural_metrics.json fixent les métriques. Les programmes sont générés sans consulter les diagnostics CRUG.

La clé de déduplication remplace les noms de propriétaires par leurs indices et exclut leurs valeurs littérales, mais conserve nombre/état initial des propriétaires, ordre et nature des opérations, relations source-destination, conditions et structure des bras. Ainsi, 10 000 signifie 10 000 formes abstraites distinctes selon cette clé et 10 000 SHA-256 de sources distincts, et non 10 000 simples renommages ou changements de constantes. Cette métrique ne signifie pas 10 000 CFG non isomorphes ; les formes de flot restent limitées aux quatre bins ci-dessous.

| Prédiction de l'oracle | Programmes générés |
| --- | ---: |
| ACCEPT | 3 122 |
| REJECT | 5 998 |
| UNKNOWN | 880 |
| Total | 10 000 |

Les 880 UNKNOWN appartiennent à la famille raw_unknown : la relation de pointeur brut ne permet pas de prouver le contrat vérifié dans ce modèle. Ces cas sont conservés comme corpus de robustesse du frontend, pas comme assertions de conformité.

## Diversité mesurée

| Mesure définie | Répartition |
| --- | --- |
| Propriétaires déclarés par programme | 2 : 2 909 ; 3 : 3 528 ; 4 : 3 563 |
| Nombre de branches binaires dynamiques | 0 : 1 883 ; 1 : 3 406 ; 2 : 3 718 ; 3 : 993 |
| Chemins faisables par programme | 1 : 1 883 ; 2 : 3 406 ; 4 : 3 718 ; 8 : 993 |
| Formes abstraites canoniques distinctes | 10 000 |
| Événements de destruction explicite dans la source | 3 333 dans 3 333 programmes |
| Emprunts directs ref / mut | 6 449 / 970 |
| Réaffectations de ref conditionnelles | 1 978 dans 989 programmes |
| Alias bruts classés UNKNOWN | 880 |
| Traces de chemins ACCEPT prédites | 10 210 |
| Nombre de drops par trace ACCEPT | 1 : 192 ; 2 : 3 104 ; 3 : 3 613 ; 4 : 3 301 |

Les occurrences récursives d'opérations dans les arbres générés sont : read_owner 45 580, read_borrow 7 419, borrow 7 419, rebind 1 978, move 2 919, drop 3 333, copy_owner 958, if 13 821, raw_alias 880. Une occurrence dans chaque bras est comptée une fois par position syntaxique, pas par exécution.

Onze familles structurelles ont été produites : borrow_conflict 969, borrow_last 892, drop_order 892, implicit_copy 958, join_borrow 930, join_lender 989, join_move 821, move_conflict 872, move_last 827, mut_conflict 970, raw_unknown 880. Elles mélangent valides et invalides, sauf les familles volontairement dédiées au rejet/UNKNOWN. Le nombre de familles d'expression suivies est limité : lecture directe d'owner/ref, écriture par mut, intrinsics borrow/borrow_mut/move/drop/adopt et condition if. Aucun agrégat, appel avec paramètre emprunté, retour de borrow, boucle, goto ou FFI n'est généré dans ce corpus final ; ces dimensions demandent une extension normative et des tests indépendants avant classement massif. Les alias conditionnels correspondent ici à des bindings ref réaffectés, pas à un modèle complet des pointeurs C. La famille implicit_copy affecte un propriétaire déjà actif : elle viole aussi le contrat de réaffectation sans drop. Ses rejets ne prouvent donc pas isolément le contrôle de copie. Un témoin séparé avec destination EMPTY dans oracle/manual_controls/implicit_copy_empty.c reçoit crug-UAM-003 ; move_into_empty.c compile.

## Compilations réellement effectuées

Les 10 000 entrées uniques du corpus final ont été compilées par CRUG original sous -O0 -fcrug-borrow -fcrug-strict, en dix tranches bornées de 1 000. La première tranche a été compilée dans oracle/corpus_v2_seed41620/ ; les 4 096 premières lignes de son manifeste sont identiques à celles du corpus final (SHA-256 359de9b8359fd714a616f9cf0785bc3e3470abe4eddec4d78e1d8011ed77df9d). Les neuf autres tranches, offsets 1000 à 9000, ont leurs objets, journaux, JSONL et résumés sous oracle/corpus_10000_seed41620/. Le script oracle/aggregate_campaign.py vérifie que chaque identifiant case_00001 à case_10000 apparaît exactement une fois.

| Prédiction / compilation CRUG | Nombre final |
| --- | ---: |
| ACCEPT / ACCEPT | 3 122 |
| REJECT / REJECT | 5 807 |
| REJECT / ACCEPT | 191 |
| UNKNOWN / ACCEPT | 880 |
| Total compilé | 10 000 |
| Programmes ACCEPT liés | 3 122 |
| Exécutions sur tous les masques faisables | 10 210 |
| Écarts d'exécution | 0 |

Les dix drivers séquentiels ont cumulé 1 329,563 s de temps mural, soit 22 min 9,563 s ; ce n'est pas une mesure de temps CPU. Les 5 807 REJECT/REJECT portent tous au moins un code sémantique CRUG et aucune ligne d'erreur C ordinaire supplémentaire. Les 191 REJECT/ACCEPT sont des variantes de join_lender, même cause racine que P3-001 déjà confirmée en phase 3 ; aucune cause indépendante nouvelle n'est inférée. Les 880 UNKNOWN ont été compilés mais ne constituent aucune assertion de conformité. Aucun timeout ni indicateur d'ICE dans les journaux de cette campagne. Les 3 122 programmes ACCEPT ont tous été liés et leurs 10 210 sorties comparées, sans écart de lectures ou d'ordre de destruction. L'agrégat brut exact est oracle/corpus_10000_seed41620/campaign_aggregate.json.

## Versionnement des itérations

oracle/pilot/ et pilot2/ sont des bancs initiaux de 100 cas chacun, utiles pour vérifier le harness ; leurs nombres ne sont pas additionnés à ceux du corpus final. corpus_seed41620/ est une première version à 4 096 cas qui autorisait des lectures de propriétaire EMPTY sur des chemins C ambigus ; elle est conservée pour traçabilité mais ne sert pas aux chiffres finaux. corpus_v2_seed41620/ est la version raffinée de 4 096 cas et un préfixe exact de corpus_10000_seed41620/ ; elle n'ajoute aucun cas au total de 10 000. Le générateur a ensuite reçu un mode d'inversion de condition utilisé uniquement par la vérification métamorphique ; il ne modifie pas les sources du corpus sans la propriété invert.

## Reprise et vérification

La comparaison complète est terminée ; aucune tranche ne reste à compiler. Le script de reprise idempotent ci-dessous saute les tranches dont le résumé existe et exécute seulement les tranches manquantes. Chaque compilation et exécution est bornée à 12 s :

    python3 /home/matt/audit-crug-phase4/oracle/resume_campaign.py

Pour revérifier la complétude, les diagnostics, les erreurs C, les ICE, les liens, les sorties et les timeouts sans recompiler :

    python3 /home/matt/audit-crug-phase4/oracle/aggregate_campaign.py

Pour reproduire le corpus depuis la seed dans un autre dossier indépendant :

    python3 /home/matt/audit-crug-phase4/oracle/generate.py --out /home/matt/audit-crug-phase4/oracle/replay --seed 41620 --count 10000

Les dix premiers enregistrements du manifeste régénéré ont été comparés octet pour octet avec le corpus final : SHA-256 identique 512c1252a2aca4f7c9849336d7e221b151f0ed39af0530e51f2d9e1ea89b6078. Le dépôt et le build originaux sont restés en lecture seule pendant cette campagne.

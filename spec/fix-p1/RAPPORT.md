# CRUG — phase 4 : audit indépendant des extensions à GCC 16.2.0

## Synthèse

Référence examinée : `f86b7df26df4451e3d5952d7bb0fba9cff698b96`, CRUG 0.1.0/GCC 16.2.0, Ubuntu-CRUG 24.04 x86-64 sous WSL2. Le dépôt et son build d'origine ont été traités en lecture seule ; toutes les expérimentations sont dans `/home/matt/audit-crug-phase4`. Les 17 défauts indépendants des phases 2 et 3 ont servi à la déduplication.

**Trois nouveaux défauts indépendants sont confirmés.** R4-RT-001 (HIGH) retient l'émetteur d'un canal capacité zéro après réception de sa propre valeur lorsqu'un second émetteur occupe le slot avant son réveil. R4-TOOL-001 et R4-TOOL-002 (MEDIUM) sont deux erreurs de preuve distinctes de `c2crug --safe` : une migration génère du CRUG refusé ; l'autre compile mais ajoute une destruction sur un chemin où le C d'origine ne la réalise pas. R4-RT-002, auto-arrêt du scheduler depuis une tâche, reste **LIKELY** à cause d'un contrat public ambigu et ne compte pas parmi les trois.

L'oracle indépendant et la matrice d'alias retrouvent P3-001 et P3-003 sous des formes supplémentaires ; ces variantes ne sont pas de nouvelles causes. Les résultats n'établissent pas une qualification exhaustive.

## Méthode et résultats mesurés

| Mesure | Résultat | Définition et portée |
|---|---:|---|
| Programmes abstraits générés | 10 000 | Formes canoniques distinctes et 10 000 sources de SHA-256 distinct ; seed 41620, 11 285 tentatives |
| Oracle ACCEPT / REJECT / UNKNOWN | 3 122 / 5 998 / 880 | Classification avant toute compilation ; UNKNOWN exclu des assertions de conformité |
| Topologies CFG | 4 | 1 chemin : 1 883 ; 2 : 3 406 ; 4 : 3 718 ; 8 : 993 ; diversité de séquences, mais gamme CFG étroite |
| Programmes compilés par CRUG | 10 000 | 3 122 ACCEPT/ACCEPT, 5 807 REJECT/REJECT, 191 REJECT/ACCEPT, 880 UNKNOWN/ACCEPT ; dix tranches vérifiées |
| Exécutions prévues vs observées | 10 210/10 210 conformes | Tous les masques faisables des 3 122 acceptations communes ; lectures et ordre des destructions |
| Divergences de compilation | 191 | Toutes `join_lender`, variantes de P3-001 ; zéro nouvelle cause de borrowing |
| Erreurs d'oracle établies | 0 modèle final ; 1 banc RAII initial | 21 tests Python passent ; la fausse alerte RAII sur un `return` avant jonction a été corrigée puis rejouée |
| Matrice alias sans LTO | 120 compilations | 12 formes × faits on/off × cinq optimisations ; sept alias certains acceptés, variantes de P3-003 |
| Divergences alias optimisées | 28 + 7 LTO | Sept formes × O1/O2/O3/Os, puis sept à O2 LTO ; prémisse `restrict` non démontrée, sans preuve de miscompilation de C défini |
| RAII | 52 exécutions valides + 13 LTO ; 12 rejets attendus | Treize formes positives à O0/O2/O3, ASan/UBSan O0 et LTO O2 ; quatre négatives × trois optimisations ; quatre représentants passent Valgrind |
| Fuzzing frontend achevé | 15 745,790 s mur cumulés, 1 337 682 exécutions | Cinq cibles et reprises ; 15 251,197 s/1 298 118 exécutions avec shim de signaux, les temps des cibles parallèles sont additionnés |
| Crashs uniques nouveaux / hangs sauvegardés | 0 / 0 | Cinq dossiers de découverte triés ; six mutations du contrôle P3-008 sont des variantes connues |
| Stress runtime prolongé | 29 633/29 633, 3 600,055 s | 88 899 000 tâches, 7 408 250 scopes, 1 481 650 redémarrages ; zéro timeout ou sortie anormale |
| Défauts indépendants nouveaux | 3 CONFIRMED | R4-RT-001, R4-TOOL-001, R4-TOOL-002 ; R4-RT-002 reste LIKELY |
| Couverture source GCOV | 2 267/4 196 lignes et 192/269 fonctions des huit nouveaux modules | Sur 90 entrées représentatives ; quatre fichiers GCC modifiés : 750/1 327 lignes ajoutées, 52/63 fonctions touchant ces plages |

Le pilote de 4 096 cas est conservé séparément : il autorisait des lectures de propriétaire `EMPTY` dont la règle de rejet est contestable au regard de `spec/000-PRECEDENCE.md`. Le corpus raffiné complet fonde les chiffres ci-dessus. Les 5 807 rejets concordants portent tous un code sémantique CRUG, sans erreur C ordinaire supplémentaire. Aucun ICE ni timeout compilateur dans ces 10 000 cas. Vingt paires métamorphiques contrôlées ont donné 32 variantes compilées et 140 exécutions sans écart. Le temps mural cumulé des tranches de comparaison est 1 329,563 s.

## Résultats par axe

**Oracle et borrowing.** Le modèle Python énumère les chemins, conserve l'identité des ressources et prêteurs, suit `ref`/`mut`, last-use, `move` et `drop`, et répond UNKNOWN hors de son sous-ensemble. Les 191 acceptations inattendues reprennent la perte de prêteur à jonction P3-001. J'ai relu un cas invalidé et un témoin valide à quatre chemins. Voir `SEMANTIC_ORACLE.md`, `ORACLE_VALIDATION.md`, `GENERATED_CORPUS.md` et `BORROW_LIFETIMES.md`.

**Ownership/RAII.** Les sorties testées couvrent `return`, `break`, `continue`, `goto`, scopes, branches, moves et drops. Nombre et ordre des destructeurs concordent à O0/O2/O3 et sur les 13 formes à O2 LTO. ASan/UBSan et Valgrind ne signalent pas de défaut dans leurs sous-ensembles. Agrégats owned et payloads imbriqués restent insuffisamment couverts.

**Exclusivité.** Sept transports certains du pointeur propriétaire vers un autre argument passent la vérification `mut` alors que la fonction appelée reçoit `restrict`. Les sept reproduisent l'écart avec faits d'alias à O1/O2/O3/Os et O2 LTO ; le témoin disjoint donne 0 dans ses dix configurations ordinaires et deux LTO. Ils prolongent P3-003. Le code optimisé n'est pas présenté comme une erreur de GCC sur du C défini.

**c2crug.** R4-TOOL-001 montre qu'un label entre `if` et `free` masque le caractère conditionnel de la libération : `--safe` annonce une preuve à confiance 1,00 puis la sortie est rejetée par CRUG avec DROP-006. R4-TOOL-002 montre qu'une garde `if (!p)` précédente est attribuée à un autre `return` ; la sortie compile, mais le nombre de libérations change sur ce chemin (contrôle par wrapper et Valgrind). Les deux causes sont dans des fonctions de décision différentes du script. Voir `C2CRUG_MIGRATION.md` et `FINDINGS.md`.

**Frontend/FFI.** Les cinq campagnes AFL++ à signaux bruts atteignent les fonctions cibles CRUG contrôlées ; le témoin P3-008 est détecté après ajustement du traitement des signaux dans un shim extérieur au compilateur. Parseur, borrow et ownership ont chacun dépassé une heure de reprise ; le FFI a été partagé entre corpus initial et sidecars structurés. Au total : 1 298 118 exécutions sous shim, zéro nouveau crash/hang, et six variantes du P3-008 connues dans le contrôle séparé. Les quatre objectifs de 8/12/8/8 heures restent non atteints ; les temps réels, corpus, checkpoints et scripts de reprise figurent dans `FRONTEND_FUZZING.md`.

**Runtime.** R4-RT-001 a été confirmé par interleaving déterministe, contrôle positif, objets runtime originaux, ASan/UBSan, TSan sur pthread et Valgrind. Le stress initial positif de 60 s (528/528) puis une campagne bornée de 3 600,055 s (29 633/29 633) vérifient une charge fixe de transitions du scheduler. Les sorties restant identiques, une seconde heure de la même charge ne donnerait aucun gain de diversité démontré. R4-RT-002 demande une décision normative sur l'arrêt du scheduler depuis une tâche. Voir `RUNTIME_CONCURRENCY.md`.

## Verdict de maturité et limites

La référence examinée ne satisfait pas toutes les garanties de sûreté et de progression annoncées : les causes antérieures d'UAF accepté en strict, de fausse exclusivité et de contrats FFI invalides restent ouvertes ; R4-RT-001 ajoute un défaut indépendant de progression. Deux erreurs de preuve de l'assistant de migration sont confirmées, sans nouveau défaut de sûreté du compilateur déduit de ces deux seuls cas. Aucun taux de fréquence réel ou exploitabilité mémoire n'a été mesuré.

La couverture source des apports CRUG a été mesurée sur un échantillon de 90 entrées, mais ne représente pas le corpus AFL entier. Les budgets AFL complets, les agrégats owned imbriqués, les alias partiels/FFI, les fermetures concurrentes de channels, les autres PAL et les CFG généraux restent à poursuivre. Le GCC 16.2 original n'a pas été audité à nouveau ; la comparaison différentielle bornée de phase 3 est conservée. Aucune valeur de bitmap AFL n'est présentée comme pourcentage de couverture source.

## Reprise et intégrité

Les commandes, empreintes et inventaires sont dans `MANIFEST.md`. Points d'entrée : `oracle/resume_campaign.py` (idempotent), `alias/run_matrix.py`, `alias/run_lto.py`, `raii/run_matrix.py`, `raii/run_lto.py`, `runtime/stress_scheduler.py`, `fuzz/scripts/run_campaign.py`. Tous les fichiers produits sont dans le dossier indépendant. Aucun correctif, commit, push, PR, issue ou publication n'a été effectué.
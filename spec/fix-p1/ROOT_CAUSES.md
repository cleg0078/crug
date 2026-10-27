# Causes racines et invariants — phase 4

La déduplication compare les neuf causes `CRUG-AUD-001..009` de phase 2 et les huit causes `P3-001..008` de phase 3, telles que documentées dans leurs `FINDINGS.md` et `ROOT_CAUSES.md`. Un nouvel ID exige une transition ou une perte d'information différente, et pas une nouvelle syntaxe pour la même lacune.

## Provenance des emprunts

**Invariant :** si un `ref` vivant peut désigner un propriétaire, l'analyse doit conserver ce prêteur jusqu'à sa dernière utilisation sur chaque chemin réalisable. Le sink d'un emprunt doit être reconnu même lorsque sa valeur traverse une expression composée.

**Causes connues :** AUD-008 omet certaines destinations statiques ; P3-001 perd un prêteur à la jonction CFG ; P3-002 perd la provenance de la valeur dans une expression conditionnelle. Les divergences du corpus d'oracle de phase 4 sont comparées à ces causes. Un déréférencement de propriétaire `EMPTY` avant affectation est classé `UNKNOWN` si la seule obligation invoquée est formulée sans mot RFC 2119 majuscule, conformément à `spec/000-PRECEDENCE.md` §3. Il ne donne pas un nouvel ID sans décision normative.

**Point de perte :** le frontend conserve des métadonnées statiques par déclaration et par expression. Les emprunts à prêteurs multiples demandent une union à la jonction ; la validité d'un sink demande une propagation récursive de la provenance. Une information inconnue ne saurait devenir l'absence démontrée d'emprunt.

## État ownership et lowering

**Invariant :** une ressource owned locale non transférée doit être détruite exactement une fois sur toute sortie modélisée, après l'évaluation de `return expr`, dans l'ordre inverse des déclarations ; un `MAYBE_MOVED` réellement atteignable doit être refusé. ADR 0003/0004/0005 et `docs/crug/DROP.md` §§2–4 fixent le modèle MVP.

**Preuve phase 4 :** la matrice `OWNERSHIP_RAII.md` exécute 13 formes valides sur trois optimisations, les vérifie sous ASan/UBSan à O0 et quatre représentants sous Valgrind ; quatre formes invalides sont rejetées avec le bon code. Aucun nouveau mécanisme RAII n'est confirmé sur ce sous-ensemble. Une première fausse alerte de l'oracle confondait une branche quittée par `return` avec une jonction ; la correction et le rejeu sont conservés. Les agrégats et mouvements partiels explicitement hors MVP ne sont pas interprétés comme défauts.

## Faits d'exclusivité

**Invariant :** `-fcrug-alias-facts` ne peut transmettre `restrict` pour un `mut` ou `own` que si l'appelant en a établi les prémisses (ADR 0010 D2/D4/D7).

**Causes connues :** P3-003 suit la déclaration syntaxique de l'autre argument, pas l'objet pointé après copie ; P3-004 saute la preuve lors d'un appel indirect. Les sept formes de phase 4 (copie, chaîne, condition, tableau, structure, double indirection, `memcpy`) prolongent P3-003. Dans `gcc/c/crug-ownership.cc:3308-3347`, la comparaison de `expr_base_decl` avec le prêteur ne constitue pas une analyse de points-to. À `:3357`, l'appel indirect sort avant la vérification du contrat `mut`. Un fait `restrict` accepté sur des alias identiques rend les optimisations inadaptées à la promesse de vérification de CRUG ; ce n'est pas une preuve de miscompilation de C défini.

## Contrats FFI

**Invariant :** les métadonnées `.crugffi` doivent être validées avant de guider la génération d'un appel et ses effets d'ownership. P3-005 accepte un destructeur à arité incompatible ; P3-006 parse `argN` de façon permissive et ignore certaines clauses inapplicables. Les deux mécanismes restent distincts. Cette phase évalue la surface via des seeds et des campagnes frontend documentées dans `FRONTEND_FUZZING.md` ; aucun nouvel ID FFI ne sera attribué sans reproduction réduite sur le compilateur original.

## Frontend GCC modifié

**Invariant :** le lookahead et la récupération après erreur doivent préserver les bornes et l'état du parseur CRUG. P3-008 est le débordement du tampon de quatre tokens provoqué par une demande de cinquième lors de qualificateurs contextuels doubles. Un crash ultérieur avec la même pile est une variante. La campagne phase 4 distingue également crash brut et diagnostic d'ICE capturé par GCC ; les mesures exactes figurent dans `FRONTEND_FUZZING.md`.

## Runtime

**Invariant :** pour une communication de capacité zéro, la complétion d'un `send` dépend de la consommation de **sa** valeur. R4-RT-001 est une nouvelle cause : `libcrug/core/chan.c:185-195` consulte `count`, compteur de tous les dépôts, sans identité de transaction. Le récepteur peut prendre A puis B peut réoccuper le slot avant que A relise ce compteur. La tâche A attend alors B, ce qui viole la progression du rendez-vous. Ce mécanisme n'utilise aucun `select` et ne réplique pas AUD-002/004.

Les autres mécanismes restent : AUD-001/007 autour du tampon TLS, AUD-005 et P3-007 autour des transitions shutdown/admission, AUD-006 autour des tailles de frame. R4-RT-002 constate qu'un shutdown depuis une tâche peut faire joindre le worker appelant, mais le contrat de cet appel doit être clarifié avant de compter une violation indépendante confirmée. Les 528 exécutions de stress positives ne qualifient pas tous les interleavings.

## Assistant de migration c2crug

**Invariant :** `--safe` n'insère `own` que lorsque l'unique libération est sur tous les chemins atteignables (ADR 0023 D1/D6). **R4-TOOL-001** viole la preuve revendiquée : `if (c)` suivi d'un label sur la ligne intermédiaire puis de `free(p)` laisse un chemin sans libération. `c2crug:257-269` ne regarde que la dernière ligne non vide, donc le label masque le contrôle sans accolades ; `analyse_source` transforme malgré tout et produit un rapport `confidence=1.00`. Le compilateur CRUG rejette la sortie `--safe` avec DROP-006 à O0 et O2, tandis que l'original C compile. Le cas sans label est correctement conservé `raw` ; une variante avec `else` sur la ligne précédente contourne le même test et reçoit aussi une fausse preuve. C'est un défaut de l'outil de migration, indépendant des faux succès des gates AUD-003/009 ; aucune acceptation unsafe du compilateur n'est établie. Voir `C2CRUG_MIGRATION.md`.
**R4-TOOL-002** est une cause distincte dans le test des sorties anticipées : `is_null_guard` à `c2crug:245-255` accepte tout `return` précédé à trois lignes ou moins par un texte `if (!p)`, même si ce retour dépend d'une autre condition. La migration `--safe` ajoute alors une destruction automatique sur un chemin où le C d'origine ne l'effectue pas ; les deux sorties compilent et les compteurs `free` ainsi que Valgrind confirment la différence. Le contrôle sans garde reste `raw`, celui avec la seule vraie garde est `own`. La correction de R4-TOOL-001 au niveau de la libération ne corrigerait pas cette association erronée d'une garde à une sortie.

## Validation du projet

AUD-003 et AUD-009 montrent deux mécanismes de gate à faux succès (CHECK non exécuté et archive incomplète). Ils ne sont pas des défauts du compilateur ou du runtime et ne sont pas recomptés. La campagne phase 4 n'a modifié aucun test officiel ni rejoué une qualification exhaustive des gates.

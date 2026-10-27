# Oracle sémantique indépendant — phase 4

## Autorité et périmètre

La hiérarchie vient de spec/000-PRECEDENCE.md, sections 2–3 : RFC-0001, ADR acceptées, puis MASTER_SPEC pour les points non tranchés. Seuls les mots normatifs en majuscules créent une obligation. Les règles employées ici sont RFC-0001 sections 7.3–7.4, 14–19 ; ADR 0003 D1–D4 ; ADR 0004 D4 et D8 ; ADR 0005 D3 et A-3–A-6 ; ADR 0019 D1–D5 ; ADR 0020 D1–D3. docs/crug/OWNERSHIP.md, BORROW.md et DROP.md servent à identifier la surface effectivement annoncée et les limites, sans renverser la hiérarchie.

Le modèle concerne un sous-ensemble volontairement borné : deux à quatre propriétaires locaux de pointeurs vers int, création par adopt(make(valeur), dispose), propriétaire initialement vide, lecture d'un propriétaire, emprunt local ref ou mut, réaffectation d'un emprunt, lecture ou écriture par emprunt, transfert move vers un propriétaire vide, drop explicite, copie owned illégale, et jusqu'à trois conditions dynamiques binaires. Chaque propriétaire créé porte une identité de ressource et une valeur entière. Les conditions utilisent des bits distincts d'un masque d'entrée.

Le modèle ne parse pas le C et n'importe aucun module CRUG. Le générateur construit d'abord un programme abstrait JSON, le classe, puis émet séparément le C. Les dumps, diagnostics et résultats de CRUG ne participent jamais à la prédiction.

## État et décisions

L'état d'un chemin contient, pour chaque propriétaire, EMPTY, OWNED ou MOVED avec l'identité de la ressource ; pour chaque emprunt, sa nature ref/mut, sa ressource prêteuse et son point de création ; les valeurs, lectures, destructions, erreurs et motifs UNKNOWN. L'identité de ressource reste distincte du nom du propriétaire afin de suivre un move. Les relations de prêteur possibles résultent de l'ensemble des chemins réalisables : un emprunt réaffecté sur deux branches conserve les deux possibilités lors de la décision globale.

L'interpréteur énumère chaque combinaison faisable de conditions, au plus 64 chemins. Une lecture future d'un binding, avant sa prochaine réaffectation, rend son emprunt vivant au point courant. Le contrôle de conflit considère toute ressource possiblement prêtée sur un chemin. Un move ou un drop pendant une durée de vie active est rejeté ; ref et mut incompatibles sont rejetés ; un usage après move est rejeté. La fin de fonction détruit les ressources toujours possédées en ordre inverse des déclarations. Une jonction qui laisse un propriétaire possédé sur un chemin et non possédé sur un autre est rejetée : ADR 0003 D2 et ADR 0005 A-6 interdisent de supposer un drapeau de destruction caché.

Le modèle répond UNKNOWN avant toute conclusion lorsque la source emploie un alias brut, une opération non modélisée, plus de 64 chemins, ou une lecture/destruction de pointeur EMPTY dont le contrat de diagnostic n'est pas clairement normatif. Cette dernière décision suit spec/000-PRECEDENCE.md section 3 : MASTER_SPEC section 10.2 décrit une erreur d'utilisation non initialisée, mais RFC-0001 section 15 ne formule pas d'obligation majuscule explicite pour une simple déréférence C d'un pointeur EMPTY. Les candidats initiaux de cette forme ne sont donc pas des findings.

## Indépendance et validité

Le code est dans oracle/model.py ; oracle/generate.py crée les sources, les JSON abstraits et le manifeste ; oracle/compare.py compile et exécute une tranche bornée. Les résultats de l'oracle figurent dans le manifeste avant toute comparaison. La validation manuelle et métamorphique est détaillée dans ORACLE_VALIDATION.md. Les 191 écarts de la campagne complète de 10 000 compilations sont tous des variantes de la cause P3-001 déjà confirmée en phase 3 : la provenance du binding ref est perdue lors d’une jonction. Ils ne créent pas de nouvel identifiant. Les 3 122 programmes ACCEPT ont été liés et leurs 10 210 chemins faisables exécutés ; aucune sortie ou séquence de destruction n’a divergé.

Cette indépendance ne prouve pas la complétude du modèle. Les expressions composées, alias bruts, sous-objets, agrégats owned, tableaux, scopes imbriqués, sorties anticipées, boucles, appels, paramètres, retours, FFI, tâches et garanties restrict sont hors du sous-ensemble exécutable actuel. Le moteur peut représenter un alias brut comme UNKNOWN, sans lui attribuer une exclusivité. Les sorties prédites sont la somme des lectures et la suite des valeurs remises aux destructeurs ; elles ne remplacent ni ASan ni une preuve de sûreté mémoire générale.

## Reprise

Depuis Ubuntu-CRUG, les commandes suivantes n'écrivent que dans /home/matt/audit-crug-phase4 :

    python3 -m unittest discover -s /home/matt/audit-crug-phase4/oracle -p 'test_*.py' -v
    python3 /home/matt/audit-crug-phase4/oracle/generate.py --out /home/matt/audit-crug-phase4/oracle/replay --seed 41620 --count 10000
    python3 /home/matt/audit-crug-phase4/oracle/compare.py --corpus /home/matt/audit-crug-phase4/oracle/replay --offset 0 --limit 1000

Le pilote compare avec le binaire original /home/matt/src/crug/gcc/crugc, options -O0 -fcrug-borrow -fcrug-strict, produit des objets hors dépôt et utilise GCC hôte avec -no-pie seulement pour l'édition de liens. Un timeout de 12 secondes borne chaque invocation.


Pour rejouer les dix tranches sans recompter les résumés déjà présents : python3 /home/matt/audit-crug-phase4/oracle/resume_campaign.py. Le contrôle intégral des IDs, codes de diagnostic, liens, sorties, ICE et timeouts est python3 /home/matt/audit-crug-phase4/oracle/aggregate_campaign.py. Le dernier agrégat est oracle/corpus_10000_seed41620/campaign_aggregate.json.

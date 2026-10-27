# Emprunts, prêteurs et durées de vie — phase 4

Référence : `f86b7df26df4451e3d5952d7bb0fba9cff698b96`. Les règles du sous-ensemble proviennent de `spec/000-PRECEDENCE.md`, des ADR acceptées 0003/0004/0020 et de `docs/crug/BORROW.md` §§2.1–2.4. Le modèle indépendant `oracle/model.py` suit les identités de ressources, propriétaires, emprunts `ref`/`mut`, prêteurs et dernières utilisations. Il énumère les deux côtés des branches dynamiques ; le diagnostic CRUG ne sert pas de vérité. Détails et limites : `SEMANTIC_ORACLE.md`.

## Jonctions de prêteurs

Le corpus final, seed 41620, comporte 10 000 formes abstraites canoniques distinctes, dont 844 formes `join_lender` que l'oracle classe invalides et 145 qu'il classe valides. Les **10 000** programmes ont été compilés par tranches vérifiées de 1 000. Les **191** divergences `REJECT` prévu / `ACCEPT` CRUG sont toutes de la famille `join_lender`, variantes de **P3-001** : un emprunt réaffecté vers plusieurs prêteurs selon la branche reste utilisable après la destruction d'un prêteur possible. Aucun nouvel ID de borrowing n'est justifié. Les sources, diagnostics, prédictions et sorties sont dans `oracle/corpus_10000_seed41620/`, particulièrement `comparison_*_1000.jsonl` et `campaign_aggregate.json`.

J'ai relu indépendamment `case_02077` : sur le chemin où `r = borrow(b)`, `drop(b)` précède `*r`, tandis que CRUG accepte. `case_02020` conserve deux prêteurs possibles mais détruit seulement `d`, indépendant de `r` ; ses quatre chemins ont les sorties attendues. L'oracle conserve la provenance par chemin réalisable et ne rejette pas simplement toute jonction.

## Rejets et exécutions

Matrice complète : **3 122 ACCEPT/ACCEPT**, **5 807 REJECT/REJECT**, **191 REJECT/ACCEPT**, **880 UNKNOWN/ACCEPT**. Les 3 122 acceptations communes ont toutes été liées et exécutées sur leurs **10 210** masques faisables ; lectures et ordre de destructions concordent sur chaque chemin, sans échec de lien. Aucun ICE ni timeout compilateur n'est signalé. Les 5 807 rejets concordants portent tous un code sémantique CRUG et aucune simple erreur C de syntaxe/type ; un accord de rejet ne prouve cependant pas à lui seul que les deux analyses appliquent exactement la même règle. Les 880 UNKNOWN restent hors de tout décompte de conformité.

Le corpus pilote, conservé séparément, avait révélé des lectures de propriétaire `EMPTY` avant initialisation. L'obligation normative exacte de rejet n'étant pas suffisamment tranchée à la plus haute précédence, ces cas sont `UNKNOWN` dans le modèle raffiné. Une exécution C indéfinie n'est pas utilisée comme référence.

## Portée et interactions

Le générateur final couvre quatre topologies de chemins (1/2/4/8 chemins faisables) et des séquences variées d'opérations, mais pas tous les CFG C, ni `goto`, boucles et scopes profonds. Le modèle renvoie `UNKNOWN` pour l'alias brut hors de son contrat formalisé. Les 20 paires métamorphiques contrôlées (12 renommages et huit inversions de branches) ont produit 32 variantes compilées et 140 exécutions sans divergence ; voir `ORACLE_VALIDATION.md`.

La matrice `OWNERSHIP_RAII.md` vérifie quatre rejets d'état/borrowing et 13 formes positives de destruction ; une première fausse alerte indépendante sur un `return` anticipé a été corrigée avant le rejeu. `ALIAS_RESTRICT.md` démontre sept voies d'alias certain prolongeant P3-003. Les escapes en destination globale/statique AUD-008 et en expression conditionnelle P3-002 restent des causes connues ; ce corpus n'en établit pas de nouvelle.
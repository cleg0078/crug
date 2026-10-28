# Audit CRUG du 27 octobre 2026

Commit figé pour les deux missions : `c0dcb18d649a909846e8b2bcefe4eb39b1841dc9`.

| Livrable demandé | Fichier |
|---|---|
| Inventaire, frontières de confiance et plan | [PLAN.md](PLAN.md) |
| Synthèse des risques, matrice, constats et reproductions | [SECURITE.md](SECURITE.md) |
| Revalidation K-001 à K-023 | [SECURITE.md](SECURITE.md#revalidation-du-registre-k-001-à-k-023) |
| Baseline, résultats et plan d’optimisation | [PERFORMANCE.md](PERFORMANCE.md) |
| Commit, binaires, machine, commandes et limites | [MANIFESTE.md](MANIFESTE.md), [manifest.json](manifest.json) |
| Résultats bruts | `*_raw.json`, journaux `*.log`, scripts et sources de ce dossier |

Deux constats confirmés : S-01 (course au démarrage/arrêt du runtime, P1 disponibilité) et S-02 (`c2crug --safe` accepte un retour anticipé caché dans une macro, P2). A-01 documente une rétention du scratch de réception, L-01 une limitation de durée de vie des channels. L’audit est ciblé et non exhaustif ; les zones ouvertes sont détaillées dans la matrice. Aucun correctif de production, commit ou publication n’a été effectué.
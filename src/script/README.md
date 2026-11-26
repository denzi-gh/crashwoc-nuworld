# src/script: curve e raffiche dell'ATS

Due campionatori C puri, senza file, SDL, renderer o stato del gioco. Li usa il
compilatore ATS/TexAnim (`src/port/nutexanm.c`), che legge i file `.ats` e passa
loro i dati già convalidati.

| File | Cosa fa |
|---|---|
| `curves.c/h` | Le tracce `param` di ATS V2: chiavi (a gradini, lineari o `smooth`), onde e impulsi, con i loro eventi e il loro avvio. `NwCurvePlayer` campiona un insieme di curve al tempo dell'host, ognuna dalla sua origine, e scrive un segnale per curva (`EFFETTO.TRACCIA`). |
| `bursts.c/h` | Le raffiche dei fulmini: un programma intero deterministico (frame, semi, ritardi, lampi), con budget esplicito e senza RNG di stato. |

Nel gioco la libreria è `nuworld_script` e si compila come il codice del thread di
gioco (`cmake/decomp.cmake`): conversioni dei double come sull'EE, arrotondamento
verso zero.

Il formato e i suoi esempi: [`docs/ats-format.md`](../../docs/ats-format.md),
[`docs/signals.md`](../../docs/signals.md). I test sono `tests/c/test_bursts.c` e le
sezioni `param` di `tests/c/test_texanim.c`.

Il vecchio lettore SC2 (profili, coordinatore di sequenze, manifest, librerie di
materiali e shader GLSL esterni) è stato tolto; il perché e cosa lo sostituisce è
in [`docs/sc2-removal.md`](../../docs/sc2-removal.md).

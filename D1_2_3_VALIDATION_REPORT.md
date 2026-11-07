# FAIRWORLD — D1.2.3 Mathematical Validation

**Esito:** **FAIL** — un controllo D1.2 mirato è fallito. La validazione è stata fermata prima della suite automatica, come richiesto.

## 1. Comando eseguito

Da `D:\FAIRWORLD\FAIRWORLD`:

```powershell
& '.\bin\Debug\FairworldEditor.exe' --validate-d12
$LASTEXITCODE
```

La stdout e stderr sono state raccolte insieme con `Tee-Object` nel log [D1_2_3_validate_d12.log](/D:/FAIRWORLD/FAIRWORLD/D1_2_3_validate_d12.log).

- Durata misurata: **0,136 s**.
- Exit code effettivo dell’eseguibile: **1**.
- L’eseguibile era la build Debug prodotta nella precedente diagnosi: `D:\FAIRWORLD\FAIRWORLD\bin\Debug\FairworldEditor.exe`.

## 2. Output completo

```text
[D1.2] Esecuzione mirata delle convenzioni planetarie.

====================================================
 FAIRWORLD - D1.2 SPATIAL CONVENTION TESTS
====================================================
      UV/chart/direction round-trip, faces, edges, corners and ties [PASS]
      Six planet sizes, valid rows, endpoints and rejected invalid rows [FAIL]
      Single row conversion through TerrainSolver and mesh mapping [PASS]
      Legacy spawn JSON/binary values and direction preserved [PASS]
 D1.2 STATUS: FAIL
====================================================
```

## 3. Risultati D1.2

| Area | Esito | Evidenza |
|---|---|---|
| Conversioni UV/chart/direction, sei facce, bordi, angoli e tie-break | **PASS** | Riga `UV/chart/direction round-trip, faces, edges, corners and ties`. |
| Sei dimensioni: coerenza `2*extent+1=N`, righe, limiti, centri cella e bordi UV | **FAIL** | Riga `Six planet sizes, valid rows, endpoints and rejected invalid rows`. Implementata da `TerrainValidation::TestD12GridRowsAndBounds()` in `src/world/terrain/validation/TerrainValidation.cpp:692`. |
| Conversione legacy → canonico singola nel flusso TerrainSolver/mesh | **PASS** | Riga `Single row conversion through TerrainSolver and mesh mapping`. |
| Spawn legacy e compatibilità JSON/binaria | **PASS** | Riga `Legacy spawn JSON/binary values and direction preserved`; entrambi i round-trip di persistenza sono passati. |

La funzione di griglia restituisce soltanto un esito aggregato e non stampa la dimensione, la riga, la cella o l’assert specifico che ha fallito. Il sorgente mostra che il gruppo comprende: coerenza dimensione/extent, tutte le righe legacy e la conversione inversa, rifiuto di `-1`, `N` e risoluzione `0`, round-trip del centro di ogni cella, endpoint UV `(0,0)/(1,1)` e tie-break su ogni bordo interno `edge/N`. I log runtime non consentono di attribuire il FAIL a uno di questi casi in particolare; la causa matematica resta quindi da isolare.

## 4. Suite automatica e GUI

- Suite automatica: **BLOCKED / non eseguita**. Il gate del test mirato è fallito e la sequenza si è fermata prima di avviarla.
- Planet Mapper / PlayState GUI: **SKIPPED / non eseguiti**. Non è stata avviata alcuna sessione GUI.
- Test GUI manuali ancora necessari dopo la correzione e il nuovo PASS dei gate automatici: aprire Planet Mapper e verificare coordinate/riga ai limiti della griglia; passare a PlayState e verificare lo spawn sui casi interessati; ripetere su tutte le taglie/facce applicabili e controllare una riapertura del documento per la compatibilità JSON/binaria.

## 5. Raccomandazione

**FAIL — non promuovere D1.2.3.** Il test mirato ha exit code `1`; non va dichiarato PASS. Il gruppo fallito non fornisce diagnostica sufficiente per scegliere una patch sicura. Il passo successivo è aggiungere o usare diagnostica che identifichi il singolo assert/caso nel controllo griglia, quindi correggere solo dopo revisione e rieseguire la validazione. Nessuna patch, modifica ai salvataggi, commit o push è stato effettuato in questa fase. D1 non è FROZEN.

# FAIRWORLD — D1.2.4 Grid Test Failure Isolation

**Esito:** isolato il primo fallimento. La validazione è stata fermata dopo aver raccolto tutte le diagnostiche emesse dal test mirato. Nessuna correzione matematica è stata applicata.

## 1. Strumentazione

È stata aggiunta diagnostica esclusivamente in `TerrainValidation::TestD12GridRowsAndBounds()` in `src/world/terrain/validation/TerrainValidation.cpp`. Le asserzioni e le aspettative preesistenti sono state conservate; i controlli ora emettono il sottogruppo A–F e i relativi input/output quando falliscono.

Nessuna modifica a `CubeSphereMapping`, `PlanetMath`, `PlanetGridCompatibility`, `MapWorldGenerator` o ai dati persistenti.

## 2. Build Debug

Comando eseguito dalla root del progetto con la toolchain e il generatore della cache esistente `build_r3z7`:

```powershell
& 'D:\Visual Code\vs\MSBuild\Current\Bin\MSBuild.exe' `
  'build_r3z7\FairworldEditor.vcxproj' `
  '/p:Configuration=Debug' '/m:1' '/v:minimal' '/nologo' `
  '/flp:logfile=D:\FAIRWORLD\FAIRWORLD\build_r3z7\D1_2_4_rebuild.diag.log;verbosity=diagnostic'
```

- **PASS**, exit code `0`.
- Durata: **25,514 s**.
- Output: `TerrainValidation.cpp`, `FairworldCore.lib` e `bin\Debug\FairworldEditor.exe` compilati/linkati.
- MSBuild ha mostrato `Checking File Globs`, poi è proseguito fino al completamento effettivo. Log dettagliato: [D1_2_4_rebuild.diag.log](/D:/FAIRWORLD/FAIRWORLD/build_r3z7/D1_2_4_rebuild.diag.log).

## 3. Test mirato

Comando eseguito dalla root del progetto:

```powershell
& '.\bin\Debug\FairworldEditor.exe' --validate-d12
```

- Exit code dell’eseguibile: **1**.
- Durata: **0,098 s**.
- Output completo: [D1_2_4_validate_d12.log](/D:/FAIRWORLD/FAIRWORLD/D1_2_4_validate_d12.log).

### Primo controllo fallito

Il primo fallimento, nell’ordine di esecuzione, è:

| Campo | Valore |
|---|---|
| Controllo | `F.internal_edge_tie_break` |
| PlanetSize / risoluzione | `Huge`, `N=41` |
| Faccia | Non applicabile: il controllo UV→Grid non prende una faccia |
| Edge index | `k=1` |
| UV esatta usata | `(float(1)/float(41), float(1)/float(41))`; il log con precisione predefinita stampa `(0.0243902, 0.0243902)` |
| Colonna/riga attese | `(1,1)` |
| Colonna/riga effettive | `(0,0)` |
| Differenza indici | `(-1,-1)` |
| Tolleranza | Non applicabile alla discretizzazione intera |

### Altri controlli falliti

Sono falliti altri cinque casi dello stesso sottogruppo F. In ciascuno la colonna e la riga effettive sono entrambe una cella sotto quelle attese:

| `k` | UV esatta usata | Valore stampato | Atteso | Effettivo | Differenza |
|---:|---|---|---|---|---|
| 2 | `(float(2)/float(41), float(2)/float(41))` | `(0.0487805, 0.0487805)` | `(2,2)` | `(1,1)` | `(-1,-1)` |
| 4 | `(float(4)/float(41), float(4)/float(41))` | `(0.097561, 0.097561)` | `(4,4)` | `(3,3)` | `(-1,-1)` |
| 8 | `(float(8)/float(41), float(8)/float(41))` | `(0.195122, 0.195122)` | `(8,8)` | `(7,7)` | `(-1,-1)` |
| 16 | `(float(16)/float(41), float(16)/float(41))` | `(0.390244, 0.390244)` | `(16,16)` | `(15,15)` | `(-1,-1)` |
| 32 | `(float(32)/float(41), float(32)/float(41))` | `(0.780488, 0.780488)` | `(32,32)` | `(31,31)` | `(-1,-1)` |

Non sono state emesse diagnostiche per `k=1..40` diverse da `1, 2, 4, 8, 16, 32`, né per altre dimensioni.

## 4. Esito dei sottogruppi

| Sottogruppo | Esito | Note |
|---|---|---|
| A. `2*E+1=N` | **PASS** | Nessuna diagnostica A; il controllo prosegue su tutte le taglie. |
| B. Legacy→canonico per ogni riga valida | **PASS** | Nessuna conversione, riflessione o round-trip di riga fallito. |
| C. Rifiuto di `-1`, `N` e risoluzione `0` | **PASS** | Nessun input invalido è stato accettato. |
| D. Round-trip dei centri cella | **PASS** | Nessuna coppia `(col,row)` differente durante UV centro→Grid. |
| E. Endpoint UV `(0,0)` e `(1,1)` | **PASS** | Entrambi gli endpoint hanno prodotto le celle previste. |
| F. Bordi interni `k/N` e tie-break | **FAIL** | Sei casi, tutti `Huge`, `N=41`, descritti sopra. |

Gli altri gruppi D1.2 sono rimasti PASS: round-trip UV/chart/direction per facce, bordi e angoli; percorso legacy→canonico verso TerrainSolver/mesh; compatibilità spawn JSON/binaria.

## 5. Causa probabile

`CubeSphereMapping::UVToGrid()` calcola l’indice con la conversione a intero di `uv * resolution`, che tronca verso zero, poi applica il clamp. Per i sei input elencati, il risultato effettivo è il bin immediatamente precedente al tie-break atteso; ciò è coerente con il prodotto in floating point che cade appena sotto `k` prima del troncamento. Il difetto osservato riguarda quindi il tie-break numerico ai bordi interni, non la riflessione delle righe legacy né i limiti delle griglie.

Il test mantiene esplicitamente l’aspettativa che il bordo `k/N` appartenga alla cella `k`. In base a tale contratto, la diagnosi punta a una lacuna nell’implementazione del tie-break numerico. Se quel contratto non fosse quello desiderato per le coordinate float rappresentabili, sarebbe invece necessario chiarirlo prima di cambiare l’implementazione; non è stato modificato il test per nascondere il problema.

## 6. Correzione minima proposta — non applicata

Dopo revisione, valutare un adattamento localizzato di `UVToGrid()` che mantenga la mappatura ai centri e agli endpoint, ma riconosca in modo deterministico i valori float che rappresentano i bordi interni `k/N` e li assegni al lato superiore (`k`). La regola dovrebbe essere definita con una soglia numerica motivata e coperta da casi adiacenti appena sotto/sopra il bordo, evitando di spostare coordinate realmente interne. Nessuna modifica è stata fatta a `CubeSphereMapping` in questa fase.

## 7. Suite e GUI

- Suite automatica: **non eseguita**, perché il test mirato D1.2 ha fallito.
- Planet Mapper / PlayState GUI: **non eseguiti**.
- Nessuna modifica a salvataggi, commit o push.
- **D1 non è FROZEN.**

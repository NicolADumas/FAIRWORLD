# D1.2.6 — EXT-10 Scientific Test Isolation

## Esito

**Causa identificata; correzione non applicata.** EXT-10 confronta byte per byte due documenti prodotti da due salvataggi successivi. L'unica differenza osservata è il campo JSON `catalogGeneration`, che passa da `1` a `2` secondo il comportamento monotono del serializer. Il payload scientifico restante risulta uguale. La failure è riproducibile e il percorso di serializzazione non dipende dalle modifiche D1.2.

## Test e componenti coinvolti

- Test: `ScientificMaterialRegistryContractTests::TestEXT10_DeterministicSerialization()` in `src/blocks/ScientificMaterialRegistryContractTests.cpp` (circa righe 1913–1983).
- Oggetti: due istanze di `ScientificMaterialRegistry`; viene registrato un materiale con ID `710`, canonical ID `fairworld:mat/det_test` e proprietà estese `scientific:prop/isothermal_bulk_modulus` (`2.2e9`, `Pa`) e `scientific:prop/poissons_ratio` (`0.25`, unità vuota).
- Flusso: salva `det_test1.json`, carica il primo documento in una seconda registry, salva `det_test2.json`, legge i file completi e confronta le stringhe con `s1.str() == s2.str() && !s1.str().empty()`.
- Contratto effettivamente verificato: uguaglianza testuale/byte dell'intero file, non soltanto equivalenza strutturale del contenuto scientifico. La descrizione e il messaggio del test richiedono output identico, ma includono così anche metadati di generazione che devono avanzare.

## Riproduzione isolata

Comando eseguito dalla directory del progetto:

```powershell
& '.\bin\Debug\FairworldScientificTests.exe'
```

Non è stata rilanciata la suite CTest completa. I quattro run isolati sul target Debug hanno tutti exit code **1**, 38 test interni, **37 PASS / 1 FAIL**, sempre EXT-10:

| Run | Durata interna | Esito |
|---|---:|---|
| 1 | 328.08 ms | 37 PASS, EXT-10 FAIL; exit 1 |
| 2 | 330.56 ms | 37 PASS, EXT-10 FAIL; exit 1 |
| 3 | 331.06 ms | 37 PASS, EXT-10 FAIL; exit 1 |
| 4, dopo ripristino della sorgente non strumentata | 329.63 ms | 37 PASS, EXT-10 FAIL; exit 1 |

Run 1–3: `D1_2_6_scientific_run1.log`, `D1_2_6_scientific_run2.log`, `D1_2_6_scientific_run3.log`. Run 4: `D1_2_6_scientific_run4_restored.log`.

## Diagnostica della differenza JSON

È stata aggiunta temporaneamente diagnostica circoscritta al ramo di fallimento del test, mantenendo invariate tutte le asserzioni. È stata poi rimossa integralmente; nessuna modifica al serializer di produzione o alle aspettative è rimasta. Entrambi i target di build temporaneo e di ripristino sono terminati con exit code 0. La verifica `git diff --check` è terminata con exit code 0.

Diagnostica rilevata (`D1_2_6_ext10_diagnostic.log`):

- Lunghezze: **30,957 / 30,957 byte**.
- Prima differenza: offset **27** (zero-based), caratteri ASCII `49` (`'1'`) e `50` (`'2'`).
- Percorso JSON: **`/catalogGeneration`**.
- Valore nel primo documento: JSON integer `1`; il valore è prodotto e letto nel codice C++ tramite `uint64_t`.
- Valore nel secondo documento: JSON integer `2`; stesso tipo C++.
- Diff strutturale completo: una sola operazione `replace` su `/catalogGeneration`, da 1 a 2. Non sono emerse differenze di ordinamento, spaziatura, tipo o valore in altri campi.
- La comparazione dei documenti completi è quindi semanticamente falsa; normalizzando esclusivamente `catalogGeneration`, i documenti risultano semanticamente uguali.

Il campo è caricato in `ScientificMaterialRegistry::LoadFromJson` (`m_loadedCatalogGeneration`) e aggiornato in `SaveToJson` usando il risultato di `AtomicWriteAndReplaceJson`. Quest'ultima calcola `nextGen = std::max(existingGen, expectedGeneration) + 1`, inserisce `nextGen` nel JSON e lo restituisce al chiamante. Perciò il primo salvataggio genera 1; dopo il load, il secondo salvataggio genera 2 anche se la destinazione è un altro file. È il metadato monotono della generazione catalogo, non una variabilità accidentale del payload.

Serializer coinvolti: `ScientificMaterialRegistry::SaveToJson` / `LoadFromJson`, `AtomicWriteAndReplaceJson` e gli overload `to_json` / `from_json` per `ScientificMaterialDef` e `ExtendedPropertyStorage`. Il writer emette il documento con `payload.dump(4)`.

## Relazione con D1.2

L'audit del percorso di chiamata EXT-10 non ha trovato riferimenti diretti o indiretti a `CubeSphereMapping`, `UVToGrid`, `PlanetGridCompatibility` o `MapWorldGenerator`: il test esercita registry scientifica, JSON e file temporanei. La differenza osservata è prodotta dal contatore monotono `catalogGeneration` e non dipende da coordinate planetarie. In base al percorso del codice e all'unico diff misurato, il fallimento è **causalmente non correlato alla patch D1.2**. Non è stata verificata una riproduzione storica anteriore a D1.2, quindi non si afferma che il test fallisse già prima della patch.

## Correzione minima proposta (non applicata)

Limitare l'adeguamento a EXT-10: analizzare entrambi i file come JSON, verificare esplicitamente che `catalogGeneration` avanzi (`gen2 > gen1`), rimuovere/normalizzare soltanto tale campo dalle copie usate per il confronto, quindi confrontare strutturalmente il resto del documento. Conservare le verifiche esistenti su materiali, proprietà estese, definizioni e indici locali. Non modificare il serializer né il formato JSON v3.

## Stato delle modifiche e validazione

- Sorgenti di produzione: nessuna modifica.
- Aspettative del test: invariate.
- Diagnostica temporanea: rimossa; `src/blocks/ScientificMaterialRegistryContractTests.cpp` corrisponde alla versione di partenza.
- Build Debug del target `FairworldScientificTests` dopo il ripristino: PASS, exit code 0 (`build_r3z7/D1_2_6_final_restore_build.log`).
- `git diff --check`: PASS, exit code 0.
- `FairworldScientificTests` isolato: FAIL atteso da investigazione, 37/38 in tutti e quattro i run, exit code 1.
- Suite CTest completa: non rieseguita in questa fase, come richiesto.

**Raccomandazione:** correggere soltanto il contratto del test EXT-10 come sopra e poi ripetere il test mirato prima di riprendere la suite. Questo rapporto non dichiara D1 FROZEN.

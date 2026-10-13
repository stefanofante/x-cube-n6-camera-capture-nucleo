# Analisi critica del firmware Nucleo N6

Autore: **stefanofante**

Data: 13 ottobre 2026

Revisione analizzata: `946af4ddc0f87cf21014ea74953a0a9f1fcec3f7` (tip di `develop` alla verifica).

## Esito e metodo

Il firmware compila per Cortex-M55, ma il successo del linker non dimostra che l'immagine possa avviarsi correttamente tramite il FSBL vendorizzato. I problemi prioritari sono l'incoerenza degli indirizzi di caricamento, la copia dell'applicazione limitata a 64 KiB, un errore riproducibile nella divisione dei frame USB in pacchetti e la gestione non atomica della proprietà dei buffer durante lo stop.

La revisione comprende applicazione, adattatori FreeRTOS/newlib, UVC/USBX, configurazione memoria, bootloader, CMake e task VS Code; il middleware vendorizzato è stato esaminato nei punti necessari a seguire questi percorsi. Non è un audit completo di ogni sorgente ST. Nessun codice del firmware è stato modificato. La proposta C++ è un progetto di migrazione, non una conversione già realizzata.

Le evidenze hanno tre livelli: **confermato** da codice o build; **riprodotto** mediante calcolo indipendente della stessa logica; **rischio** con una sequenza plausibile che richiede test concorrenti o hardware. Non sono stati eseguiti firmware, signing, flashing, acquisizione video né benchmark sulla scheda. Non viene attribuito un incremento di prestazioni a modifiche non misurate.

**Vincolo del progetto: CI/CD su GitHub vietato.** Le verifiche proposte devono essere eseguite manualmente o tramite comandi locali espliciti, senza GitHub Actions, workflow di deployment o integrazioni equivalenti. Nel checkout esaminato non è presente una directory `.github`. I commit di questo lavoro devono avere solo `stefanofante` come autore e committer, senza trailer di coautore.

## Verifiche eseguite nel cloud

Toolchain locale Debian: GCC ARM **14.2.1**, CMake **3.31.6**, Ninja **1.12.1**, newlib **4.5.0.20241231**. È diversa dal GCC 14.3.1 con patch ST indicato nel README.

| Verifica | Risultato | Limite |
| --- | --- | --- |
| Lettura Git del branch `develop` | Passata; SHA remoto uguale al checkout iniziale | Branch locale della piattaforma denominato `work` |
| Configure con preset Debug originale | Passato | Preset contiene un'opzione specifica ST |
| Build con preset originale e GCC Debian | Fallito: `-fcyclomatic-complexity` sconosciuta | Problema di portabilità della toolchain |
| Build app con override locale che rimuove solo tale opzione | Passato, 228 passi | Warning nel middleware; nessuna esecuzione sulla scheda |
| Build FSBL Release con toolchain esplicita | Passato, 26 passi | Non dimostra il corretto caricamento dell'app |
| Ricompilazione incrementale app e FSBL | Passata: `ninja: no work to do` | Stessa istanza cloud |
| Generazione app `.bin` con `objcopy` | Passata | Il CMake app non la genera autonomamente |
| Compilazione ARM di un esempio C++20 con `std::span`, `std::array`, `constexpr`, senza RTTI/eccezioni | Passata | Solo compilazione di un oggetto; nessuna integrazione/link del firmware C++ |
| Riproduzione matematica della packetizzazione UVC | Difetto riprodotto a HS e FS | Non è un test di trasmissione USB |

Dimensioni ELF riportate da `arm-none-eabi-size`:

| Target | text | data | bss |
| --- | ---: | ---: | ---: |
| App | 310548 B | 1372 B | 1567516 B |
| FSBL | 31476 B | 192 B | 3092 B |

La somma non coincide necessariamente con la dimensione del file binario: sezioni `NOLOAD`, allineamenti e intervalli di indirizzi vanno considerati separatamente. L'app `.bin` prodotta occupa circa 305 KiB; il FSBL circa 31 KiB. Log e strumenti di onboarding sono fuori dal repository, in `/workspace/cloud-tools`.

## Problemi prioritari

### F01 — Indirizzi di app e FSBL incompatibili: priorità P0, confermato

Riferimenti: `STM32N657xx_nucleo.ld:5-19,125`; `Tools/FSBL/STM32N657XX_AXISRAM2_fsbl.ld:47`; `Tools/FSBL/Inc/stm32_extmem_conf.h:72-81`; `STM32Cube_FW_N6/Middlewares/ST/STM32_ExtMem_Manager/boot/stm32_boot_lrun.c:136-158,277-290`.

L'app ha vettori a **0x34180400**, verificati nell'ELF. Anche il FSBL è linkato nella RAM che parte da **0x34180400**. Il loader copia invece l'immagine firmata a **0x34000000** e cerca i vettori dopo l'header di 0x400 byte, quindi a **0x34000400**. Le istruzioni e i dati dell'app restano linkati per l'altro indirizzo: questa copia non effettua una rilocazione.

Il problema non va risolto cambiando soltanto la destinazione del loader a 0x34180000: si sovrascriverebbe il bootloader mentre sta copiando. Occorre definire una mappa coerente fra area FSBL, app, stack, heap e DMA, e allineare linker, destinazione, vettori, header firmato e configurazione di startup.

Anche l'asserzione `_edata < _fsbl_start_address` è incoerente con la mappa corrente: l'ELF riporta `_edata = 0x341cc674` e `_fsbl_start_address = 0x34180000`, ma il link termina correttamente e la map registra l'ASSERT come vero. Non si deve quindi usare quel successo come prova di assenza di sovrapposizioni. In una correzione dedicata, rendere i controlli del linker espliciti sui valori assoluti finali e verificare le intersezioni di tutti gli intervalli, inclusi BSS e stack.

Accettazione: controllare ELF/map e vettori dell'immagine firmata; avvio a freddo senza debugger; verificare che nessuna scrittura di caricamento o inizializzazione cancelli codice/stack del FSBL ancora in uso.

### F02 — Copia dell'app troncata a 64 KiB: priorità P0, confermato

Riferimenti: `Tools/FSBL/Inc/stm32_extmem_conf.h:78-81`; `stm32_boot_lrun.c:154-158,267-270`.

`EXTMEM_LRUN_SOURCE_SIZE` vale **0x10000**. L'implementazione weak di `BOOT_GetApplicationSize()` restituisce questa costante; nei sorgenti FSBL non è presente un override. Il loader copia quindi soltanto 65536 byte, mentre l'app binaria di questo build supera 300 KiB, prima ancora dell'header di firma.

Definire una lettura della lunghezza dal formato ST documentato, validando header, dimensione minima/massima, overflow, spazio nella NOR e destinazione RAM. In alternativa, una dimensione fissa deve essere derivata e verificata rispetto all'immagine effettiva. Non aumentare arbitrariamente la costante senza correggere F01.

Accettazione: confrontare lunghezza dell'immagine firmata e byte copiati; rifiutare header corrotti o lunghezze fuori area; verificare in RAM anche la parte finale dell'app.

### F03 — Numero di pacchetti USB errato: priorità P0, riprodotto

Riferimenti: `Lib/uvcl/Src/uvcl.c:71-79`; `Lib/uvcl/Src/usbx/uvcl_usbx.c:85-111`; `Lib/uvcl/Src/uvcl_internal.h:44-45`.

Il payload utile è `P = packet_size - 2`, ma `packet_nb` usa `(fsize + packet_size - 1) / P`. Il numeratore corretto per una divisione con arrotondamento superiore è `fsize + P - 1`, con controllo di overflow. La compensazione applicata ai multipli esatti non corregge il caso `fsize % P == P - 1`.

| Modalità | Payload P | Frame | Pacchetti attuali | Pacchetti corretti | Byte di frame consumati dalla logica attuale |
| --- | ---: | ---: | ---: | ---: | ---: |
| HS, 3 transazioni | 3070 | 3069 | 2 | 1 | 6139 |
| HS, 3 transazioni | 3070 | 6139 | 3 | 2 | 9209 |
| FS | 1021 | 1020 | 2 | 1 | 2041 |

Sono letture oltre la lunghezza valida del frame; vicino alla capacità dell'output possono oltrepassare anche il buffer fisico. I frame H.264 hanno lunghezza variabile e possono raggiungere questi resti. Correggere il conteggio e testare ogni resto modulo P, multipli esatti, frame di un byte e limiti del buffer. Una formula che evita l'addizione potenzialmente in overflow è `fsize / P + (fsize % P != 0)`, dopo aver validato `fsize > 0` e `P > 0`.

### F04 — Stato dei pacchetti non reinizializzato dopo abort: priorità P1, confermato nel codice

Riferimenti: `Lib/uvcl/Src/uvcl.c:71-99,467-495`.

La fine normale porta `packet_index` a zero tramite modulo. `UVCL_AbortOnFlyCtx()` elimina invece solo il puntatore al contesto attivo. Il riuso di `on_fly_storage_ctx` non azzera esplicitamente `packet_index` in `UVCL_FillSentData()`. Dopo uno stop a metà frame, il nuovo frame può quindi partire dall'indice precedente, con un numero di pacchetti e un rilascio anticipato o ritardato errati.

Inizializzare esplicitamente tutti i campi del contesto per ogni nuovo frame. Testare stop/reset dopo ciascun indice, seguito da start con frame più corto e più lungo. La conseguenza sulla scheda resta da misurare.

### F05 — Rilascio non atomico e rimozione che altera il conteggio su miss: priorità P1, confermato/rischio concorrente

Riferimenti: `Src/app.c:256-280,447-457,531-559`; `Lib/uvcl/Src/uvcl.c:623-645`; `Lib/uvcl/Src/usbx/uvcl_usbx.c:114-129,297-307,438-457`.

`lp_remove_buffer()` consuma il semaforo prima di cercare il puntatore e decrementa `level_dbg` anche se non trova nulla. Non ripristina il token su miss. Se la lista è vuota, l'asserzione su `xSemaphoreTake()` fallisce prima che il chiamante possa gestire `removed == NULL`: il controllo difensivo in `send_h264_frame()` non è sufficiente.

Una sequenza da riprodurre è: `ShowFrame` pubblica il puntatore; lo stop USB rilascia e azzera quel frame; `ShowFrame` osserva STOP e restituisce errore; il chiamante prova a rimuovere un buffer già rilasciato. Questo può provocare assert, conteggio corrotto o riuso del buffer nella generazione sbagliata.

I commenti applicativi descrivono il rilascio come callback ISR. Nel preset corrente `UVCL_USBX_USE_FREERTOS` differisce il lavoro USB a `cb_thread_fct()`, che chiama `ux_system_tasks_run()`. Non è corretto dichiarare senza traccia runtime che ogni rilascio passi dall'ISR. Restano accessi concorrenti fra task; eventuali altri percorsi di callback devono essere tracciati. Se eseguito in ISR, `lp_remove_buffer()` vieta esplicitamente quel contesto e usa mutex non adatti.

Soluzione: contratto univoco per accettazione/rilascio, ID del buffer più generazione di stream, un solo proprietario delle transizioni di stato; ricerca/rimozione che non corrompa la coda su miss. Un ring o una coda statica di ID elimina la combinazione fragile lista+mutex+semaforo. Per ISR usare API `FromISR` e consegnare l'evento a un task.

### F06 — Stop e teardown della camera non sincronizzati: priorità P1, rischio

Riferimenti: `Src/app.c:185-188,410-430,619-639,666-703`; `Src/app_cam.c:198-211`.

Il task capture può essere risvegliato da un evento precedente mentre il task UVC esegue `CAM_Deinit()` e svuota le liste. Non c'è un handshake di quiescenza né un controllo dello stato camera nel worker. Il task ISP può analogamente chiamare `CMW_CAMERA_Run()` durante la deinizializzazione. `sr_is_valid()` legge il contatore senza il mutex usato dagli scrittori; inoltre il ciclo principale lo verifica soltanto quando non trova un buffer, permettendo altri invii dopo una richiesta di stop finché la coda resta alimentata.

Definire stati `Stopped/Starting/Streaming/Stopping/Fault`, impedire nuove operazioni dopo Stopping, arrestare le sorgenti di eventi, attendere i worker, riconciliare i buffer in volo e solo dopo liberare l'encoder. Leggere stato e generazione con la medesima sincronizzazione degli aggiornamenti. `volatile` o una sola barriera DMB non risolvono una transazione con più campi.

Accettazione: start/stop ripetuti, scollegamento USB durante encode/invio, eventi camera pendenti, invarianti di proprietà dei buffer verificate dopo ogni transizione.

### F07 — Scrittura OTP automatica al boot: priorità P1, confermato

Riferimenti: `Src/main.c:348`; `Src/app_fuseprogramming.c:36-85`; `Tools/FSBL/Src/main.c:97-102,341-396`.

L'app programma OTP 124 per HSLV VDDIO2/VDDIO3 se i bit non sono già settati. Anche il FSBL contiene una programmazione OTP, esclusa solo definendo `NO_OTP_FUSE`; il CMake FSBL corrente non la definisce. Queste scritture persistono sull'hardware e richiedono la conferma delle condizioni elettriche previste dal datasheet della specifica board.

Separare provisioning della scheda e avvio normale. Prevedere un firmware/strumento esplicito di provisioning, con controlli sulle tensioni e verifica finale. Non eseguire questa procedura durante una semplice prova del firmware. Questa revisione non ha scritto OTP né connesso una scheda.

### F08 — Assert al posto di recupero e attese senza limite: priorità P1, confermato

Riferimenti: `Src/app_enc.c:190-245`; `Src/app_cam.c:165-211,214-217`; `Src/main.c:359-360,429-430,460-467`; `Gcc/Src/console.c:25-35`; `Inc/FreeRTOSConfig.h:171`.

Gli errori di init/encode/camera sono spesso assert; il callback di errore DCMIPP è vuoto. Alcuni ritorni NOR sono ignorati. L'attesa `USB33RDY` e la trasmissione UART con timeout `~0` non hanno una scadenza operativa utile. `assert_failed()` non conserva file e riga e usa BKPT anche senza debugger.

Introdurre codici di errore distinti, deadline, contatori, record del fault e recupero per periferica. Conservare gli assert per invarianti interne, senza usarli come unica gestione di sensore assente, memoria insufficiente o USB scollegato. Con `NDEBUG`, `configASSERT` attuale può disabilitare gli interrupt e poi proseguire perché `assert(0)` scompare: definire un handler di fault che resti valido in tutte le configurazioni.

### F09 — Allocatore HW non garantisce allineamento per ogni richiesta: priorità P2, difetto latente

Riferimenti: `Src/app_enc.c:40-41,298-310`; `.../VideoEncoder/source/common/encasiccontroller_v2.c:185-206`.

L'arena parte allineata a 32 byte, ma l'avanzamento è `pos += size` senza arrotondamento. L'allineamento delle richieste successive dipende quindi dalle dimensioni richieste dal middleware. Diverse richieste attuali sono già arrotondate, perciò non è dimostrato un fault nel profilo corrente; il contratto dell'allocatore rimane fragile per nuove configurazioni. Anche `pos + size` può creare un puntatore fuori oggetto prima del controllo.

Usare offset `size_t`, allineamento richiesto esplicito, controlli per sottrazione prima dell'addizione e high-water mark. Verificare i requisiti reali delle strutture DMA, non imporre indiscriminatamente 32 byte se l'hardware richiede altro. Resettare l'arena soltanto dopo quiescenza DMA e rilascio completo dell'istanza.

### F10 — Semantica H.264 da rendere esplicita: priorità P2, confermato/rischio di compatibilità

Riferimenti: `Src/app_enc.c:74-105,109-163,201-245`; `.../VideoEncoder/inc/h264encapi.h:402-437`.

Il filler aggiunto per allineare il bitstream usa header `0x2c`: per filler NAL tipo 12 il `nal_ref_idc` dovrebbe essere zero (`0x0c`). Scrive solo byte `0xff` senza il terminatore `rbsp_trailing_bits` (`0x80`). L'allineamento non dovrebbe produrre un NAL sintatticamente non conforme. Verificare con un parser H.264 e almeno due decoder.

`enc_in.timeIncrement` è sempre 1, mentre il contratto vendorizzato indica 0 per la prima immagine e la durata trascorsa per le successive. Con drop o attese USB, i tempi nominali non descrivono l'intervallo reale; valutarne l'impatto sul rate control. `H264EncIn` è inizializzato per campi e lascia `busLumaStab` indeterminato: oggi stabilizzazione non attivata, ma usare zero-init rende sicura l'evoluzione. Conservare il codice errore originale invece di ridurre ogni fallimento a -1.

Il livello 5.1 è molto alto rispetto a 480×272@30: scegliere il livello minimo compatibile con macroblocchi/s, bitrate e buffer effettivi migliora la compatibilità dei decoder, senza pretendere che abbassare il livello renda automaticamente più veloce l'encoder.

### F11 — Documentazione e build non corrispondono completamente al codice: priorità P2, confermato

Riferimenti: `README.md` sezioni Toolchain/Building/Limitations; `Inc/app_config.h:32-34`; `Src/app_enc.c:141-144,215-220`; `CMakeLists.txt`; `cmake/vscode_generated.cmake:251-253`; `.vscode/tasks.json`.

Il percorso corrente configura input **YUYV interleaved** e non costruisce un piano UV grigio statico nell'app. I puntatori chroma zero sono documentati come inutilizzati per questo formato nell'API vendorizzata: non dimostrano da soli che l'immagine sia monocromatica. L'origine di eventuale monocromia deve essere verificata su sensore, DCMIPP e decoder, poi descritta correttamente.

Il README promette `.bin` dell'app al build, ma il CMake non contiene il relativo `POST_BUILD`; nel cloud è stato necessario `objcopy`. Il comando app senza preset nel README non carica tutte le variabili `CUBE_CMAKE_*` del preset Debug. Il FSBL richiede il toolchain file esplicito per evitare una compilazione nativa. I task di signing/programmazione usano percorsi/eseguibili Windows.

Documentare comandi effettivamente verificati, distinguere build portabile e operazioni ST/hardware; mantenere l'opzione di complessità ciclomatica soltanto quando supportata dal compilatore. I warning di formato e bounds nel middleware richiedono triage, non soppressione globale. Alcuni warning array-bounds USBX riguardano accessi a indirizzi fissi dei descrittori: la sola diagnostica non prova una lettura fuori buffer runtime.

## Prestazioni: interventi utili e costo atteso

La pipeline usa già acceleratori DCMIPP, ISP e VENC. Una riscrittura C++ non accelera automaticamente tali periferiche; le opportunità maggiori sono nella sincronizzazione, nel backpressure USB e nelle copie lato CPU.

| Intervento | Beneficio previsto | Verifica necessaria |
| --- | --- | --- |
| Notifiche task/eventi per frame e start/stop | Elimina polling ogni 1 ms e riduce wake-up/latency | Misurare latenza e CPU; preservare cancellazione e proprietà dei buffer |
| Coda statica di ID invece di lista+mutex+semaforo | Meno bookkeeping, transizioni più semplici e verificabili | Test di saturazione, rilascio duplicato, restart e priorità |
| Evento di disponibilità USB invece di 30 retry a tick | Riduce attese artificiali e codifica di frame non inviabili | Budget end-to-end per frame, deadline e stop immediato |
| Separazione buffer DMA non cacheabili e metadati cacheabili | Evita costo CPU su grandi regioni non cacheabili | Misurare copie USB; clean/invalidate allineati e ownership esplicita |
| Logging UART asincrono con ring statico e limite | Evita che una riga da 100 byte occupi circa 8,7 ms a 115200 8N1 | Overflow log misurato, ISR senza printf, timeout/DMA corretti |
| Configurazione encoder entro budget RAM/USB | Riduce burst di I-frame e drop | Distribuzione dimensione frame, picco bitrate, qualità e tempi VENC |
| Riduzione priorità RTOS inutilizzate | Possibile riduzione costo selezione task | Profilare; port ottimizzato tipicamente limitato a 32 priorità |

`configMAX_PRIORITIES` è 56 e la selezione ottimizzata è disattivata (`Inc/FreeRTOSConfig.h:77,89`): si può ridurre dopo aver rimappato le priorità, senza modificare alla cieca i requisiti IRQ. Le notifiche contate sono utili soltanto se gli eventi corrispondono ancora a frame recuperabili; con acquisizione continua non si deve processare un indirizzo già riscritto dall'hardware.

La sezione DMA `.psram_section` occupa **0x15f800 = 1406 KiB** su 1536 KiB: 510 KiB di capture, 128 KiB di output e 768 KiB di arena VENC, lasciando **130 KiB**. Aumentare buffer o risoluzione richiede un nuovo budget. La capacità totale non sostituisce la misura delle allocazioni effettive. Aggiungere assert statici e del linker, high-water mark dell'arena e misura degli stack: 1024 `StackType_t` normalmente significa 4096 byte, non 1024 byte.

La telemetria attuale conta frame codificati, non consegnati al decoder; il valore `fps` non è normalizzato sul tempo effettivo se l'intervallo supera un secondo. Separare capture/encode/USB accepted/USB released/drop e normalizzare rispetto ai tick trascorsi.

Non eliminare il force-intra dopo perdita di un frame già codificato: il decoder può perdere la catena dei riferimenti. Un frame scartato **prima** della codifica non aggiorna invece quei riferimenti; in quel caso l'intra forzato può essere evitabile. Distinguere i due casi prima di ottimizzare. Non applicare MVE a task C++ senza verificare anche `configENABLE_MVE` e il salvataggio del contesto; la configurazione corrente lo disabilita.

## Proposta C++ moderno con memoria statica

Adottare C++20 per l'orchestrazione applicativa e mantenere HAL, FreeRTOS, USBX ed encoder ST in C. Evitare conversioni meccaniche dei sorgenti vendorizzati: aumenterebbero manutenzione e divergenza upstream senza un beneficio dimostrato.

Memoria statica e metodi statici sono concetti diversi. Le classi tutte statiche possono rappresentare periferiche uniche, ma diventano stato globale nascosto se ogni sottosistema accede liberamente agli altri. Preferire oggetti a durata statica con dipendenze esplicite, stato privato e API non bloccanti; metodi statici per configurazione, callback C e funzioni senza stato.

| Componente proposto | Responsabilità | Memoria/contratto |
| --- | --- | --- |
| `CameraConfig` | Dimensioni, fps, capacità, vincoli | `static constexpr`, `static_assert` |
| `FramePool<N, Capacity>` | Buffer, ID, stato e generazione | `std::array`, `alignas(32)`, sezione DMA controllata |
| `CameraPipeline` | Init/start/stop/ISP e handshake worker | Istanza statica, errori espliciti |
| `H264Encoder` | Arena, ciclo di vita e codifica | Singleton solo dove richiesto dall'IP; init esplicito dopo RTOS |
| `UvcTransport` | Accettazione, completamento, backpressure | Handle ID+generazione; rilascio esattamente una volta |
| `StreamController` | Macchina a stati e ownership | Un task proprietario delle transizioni |
| `Diagnostics` | Timestamp, contatori e record fault | Ring statico, snapshot coerenti |

Esempio di interfacce da implementare e testare, non codice già integrato:

```cpp
struct CameraConfig final {
    static constexpr std::size_t width = 480;
    static constexpr std::size_t height = 272;
    static constexpr std::size_t captureBytes = width * height * 2;
    static_assert(width % 16 == 0 && height % 16 == 0);
};

enum class FrameState : std::uint8_t {
    Free, Capturing, Ready, Encoding, UsbQueued, UsbInFlight
};

struct FrameHandle final {
    std::uint32_t generation;
    std::uint8_t index;
};

enum class Error : std::uint8_t {
    None, Busy, InvalidFrame, Timeout, OutOfMemory, HardwareFault
};

class H264Encoder final {
public:
    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;
    static Error start(const CameraConfig&) noexcept;
    static Error encode(std::span<const std::byte> input,
                        std::span<std::byte> output,
                        std::size_t& written) noexcept;
    static Error stop() noexcept;
};
```

`std::span` rappresenta puntatore e lunghezza senza allocare, ma non rende automaticamente checked gli accessi; serve validare formati, dimensioni e offset. Le firme reali dovranno includere fps, tipo pixel e richiesta IDR. Per `std::expected` occorre C++23: in C++20 usare un piccolo result esplicito o `Error` più valore di uscita. Un `FrameLease` move-only può rappresentare ownership in un task, ma non deve restituire il buffer al pool alla distruzione se USB/DMA lo sta ancora usando: il trasferimento asincrono deve spostare il lease al proprietario del completamento.

Usare RAII per lock di breve durata esclusivamente in task, non in ISR. Callback `extern "C"` devono inoltrare eventi limitati nel tempo, senza eccezioni, heap o blocchi. Evitare costruttori globali che chiamano HAL/RTOS prima dell'inizializzazione, singleton locali con guard dinamico nell'hot path, virtual dispatch non necessario e contenitori dinamici. `std::atomic` deve essere verificato lock-free sul target prima di usarlo in ISR; atomiche separate non garantiscono coerenza fra stato, lunghezza e puntatore.

La build C++ dovrà abilitare esplicitamente il linguaggio e `cxx_std_20`, mantenere ABI hard-float/CMSE e flag coerenti, usare `-fno-exceptions -fno-rtti`, verificare startup/`.init_array` e l'uso del driver G++ per le dipendenze runtime. Non disattivare indiscriminatamente le guardie degli statici per nascondere problemi di init. Controllare ELF/map e simboli `new`, `delete`, `__cxa_*`, heap e stack: il supporto statico va dimostrato sul link effettivo. Il vecchio preset C++ `gnu++14` deve essere aggiornato in una futura modifica dedicata.

## Piano di correzione e prove locali

1. **Boot e bounds prima delle prestazioni:** correggere F01/F02 insieme, F03/F04 e il contratto di release F05. Nessuna misura di fps è significativa con frame malformati o boot non coerente.
2. **Robustezza:** introdurre macchina a stati e quiescenza dei worker, deadline/errori, provisioning OTP separato, invarianti sul pool e gestione fault persistente.
3. **Baseline sulla scheda:** misurare tempi capture-ready→encode→USB complete, percentili e massimo, CPU per task, stack/arena, frame size e drop per causa. Usare DWT/TIM e timestamp separati dal logging, gestendo il wrap dei contatori.
4. **Ottimizzazioni singole:** prima notifiche/coda e backpressure, poi logging e cache; confrontare stessa scena, esposizione, sensore, risoluzione, bitrate e toolchain. Provare `-O2`/`-O3` e LTO solo dopo la baseline; valutare crescita codice, link, ISR e durata massima, senza assumere che siano migliori di `-Os`.
5. **Migrazione C++ graduale:** introdurre tipi/configurazione/pool e wrapper mantenendo ABI C, poi il controller. Ogni passo deve preservare firmware size, budget RAM e comportamento start/stop.

Prove manuali/locali da aggiungere in una futura attività di implementazione:

| Prova | Esito atteso |
| --- | --- |
| Packetizzazione su tutti i resti modulo P, canary ai bordi | Consumo esatto dei byte validi, nessun accesso ai canary |
| Modello pool con doppio release, miss, saturazione, generazione vecchia | Errore esplicito, nessun token perso né buffer duplicato |
| Abort a metà frame e restart | Indice azzerato, frame successivo completo |
| Arena con dimensioni arbitrarie e valori vicini a `SIZE_MAX` | Allineamento corretto, rifiuto senza overflow |
| Validazione immagine/vettori e header corrotti | Nessuna copia/jump fuori dalle aree autorizzate |
| Stress hardware con stop/start e disconnect in ogni fase | Recupero entro deadline, pool e periferiche quiescenti |
| Parser/decoder H.264 indipendenti | SPS/PPS/IDR e filler conformi; recupero dopo drop |
| Configurazione con e senza `NDEBUG` | Fault gestito e diagnosticabile in entrambe |

Queste sono prove pianificate, non risultati già ottenuti. Per codice puro, harness host con sanitizer può verificare bounds e macchina a stati; non sostituisce coherency DMA, IRQ e timing reali. Non creare CI/CD su GitHub per eseguirle.

## Stato finale dell'attività

Ambiente preparato per analisi e compilazione app/FSBL; sorgenti applicativi invariati. Le istruzioni di installazione/attivazione del cloud sono salvate separatamente nella configurazione dell'ambiente. Il firmware standalone e la webcam non sono validati: i difetti di boot e USB sopra descritti devono essere corretti e verificati prima di considerare questa revisione pronta per la scheda.

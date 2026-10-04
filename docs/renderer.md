# Il renderer di Vela

Progetto della scena e del renderer propri di Vela, nativi Vulkan. È il
fondamento della milestone 2 (sfocatura, angoli arrotondati, ombre) e di
tutto ciò che verrà dopo. Questo documento viene prima del codice: le
decisioni si prendono qui, il codice le segue.

> **Stato:** progetto discusso e approvato nelle scelte di fondo (§2,
> riassunte anche in §13). Tappe **S0–S6 fatte** (§11): la scena e il
> renderer di Vela sono gli unici, a ogni scala le app arrivano sullo
> schermo bit per bit, ogni frame si disegna il più tardi possibile prima
> del vblank, le finestre hanno angoli arrotondati e ombre, i pannelli
> della shell la sfocatura acrylic, e le app Qt/KDE la barra del titolo di
> Vela. Prossima: S7.

## 1. Obiettivi

Vela deve essere, su qualunque PC, il desktop più fluido e più nitido in
circolazione. Per il renderer questo significa, in ordine di importanza:

1. **Nitidezza perfetta a qualunque scala.** A 100%, 125%, 150%, 175%, 200%
   il testo e le linee di un'app scritta bene devono arrivare sullo schermo
   **pixel per pixel**, senza alcun ricampionamento. Nessuna sfocatura da
   scaling, nessuna riga di un pixel che diventa due righe grigie, nessuno
   "sfarfallio" delle finestre mentre si muovono.
2. **Fluidità a qualunque frequenza.** 60, 75, 120, 144, 165, 180, 240,
   360 Hz, e VRR. Le animazioni devono essere identiche nel tempo su ogni
   monitor e usare ogni frame che il monitor offre. Nessuna costante tarata
   su una frequenza.
3. **Latenza minima.** Dal movimento del mouse o dal tasto al pixel sullo
   schermo, il meno possibile: si disegna il più tardi possibile prima del
   vblank, non appena arriva il vblank precedente.
4. **Leggerezza.** Si disegna solo ciò che cambia; senza cambiamenti la GPU
   dorme. Gli effetti costano in proporzione a quanto si vede.
5. **Effetti di Windows 11 e oltre:** sfocatura dal vivo, angoli
   arrotondati, ombre, tutti antialiasati e nitidi a ogni scala.

## 2. Decisioni prese

| Decisione | Scelta | Conseguenza |
|---|---|---|
| Scena | **tutta nostra**, non `wlr_scene` | ciò che `wlr_scene` faceva (damage, feedback, scanout...) lo facciamo noi, e lo facciamo meglio (§5) |
| API grafica | **solo Vulkan**, versione **1.4** | nessun ripiego GLES/pixman; requisito minimo esplicito (§7.1) |
| Verso wlroots | il nostro renderer si presenta anche come **`wlr_renderer`** | ciò che wlroots disegna da sé (cursore hardware, screencopy, catture degli schermi, caricamento dei buffer delle app) usa il nostro device e i nostri shader; nessun secondo renderer (§7.2) |
| Sfocatura | **sempre dal vivo** | ciò che sta dietro viene sfocato a ogni frame in cui cambia; va reso economico per costruzione (§8) |
| Scala predefinita | **scelta da Vela dai DPI del monitor**, a passi del 25% | come Windows; l'utente può cambiarla (§3.8) |
| App X11 a scala frazionaria | **ingrandite dal compositor** (misura giusta, un po' sfocate) | più avanti un'impostazione per lasciarle scalare da sole, come Plasma (§3.9) |
| Batteria | **nessuna riduzione degli effetti** | i portatili moderni reggono tutto; niente modalità "a risparmio" automatica (§8.3) |
| VRR | **rinviato**, ma il progetto non deve precluderlo | §4.4 |
| Barra del titolo di Vela | **progettata qui**, disegnata dal compositor | §9 |
| Tinta della barra | **derivata dallo sfondo**, come Mica | §9.4 |
| Font della barra | **quello di KDE**, per ora | §9.5 |
| Icone | **sempre SVG**, disegnate alla dimensione fisica esatta | §9.6 |

wlroots resta l'"impianto idraulico": backend (DRM/KMS, sessione, libinput,
annidato, headless), `wlr_output` (modalità, commit atomici, VRR), i
protocolli (xdg-shell, layer-shell, seat, dmabuf, syncobj, fractional
scale, presentation, ...) e l'astrazione `wlr_buffer`. **Scena, disegno,
tempi dei frame e politica sono di Vela.**

## 3. Coordinate e scala: il cuore della nitidezza

La maggior parte dei desktop sbaglia qui. Le regole di Vela:

### 3.1 Due spazi, un solo punto di conversione

- **Spazio logico** (`double`): dove vivono finestre, animazioni, layout,
  input. Un'unità logica = un pixel a scala 100%.
- **Spazio fisico** (`int`): i pixel veri di ciascuno schermo.
- La conversione logico → fisico avviene **in un solo posto**, per schermo,
  al momento di disegnare. Mai prima, mai due volte.

### 3.2 Si arrotondano i bordi, non posizione e dimensione

Un rettangolo logico `[x, x+w)` diventa fisico come
`[round(x·s), round((x+w)·s))`. Arrotondare posizione e larghezza separatamente
fa comparire fessure di un pixel tra finestre affiancate (snap!) o
sovrapposizioni. Con i bordi, due rettangoli adiacenti in logico restano
adiacenti in fisico, sempre.

### 3.3 Buffer 1:1, niente ricampionamento

- Ogni superficie riceve la scala preferita esatta con `fractional-scale-v1`
  (in 120esimi: 125% = 150/120, 150% = 180/120, 175% = 210/120, tutte
  esatte) e disegna con `viewporter` un buffer grande quanto la sua area
  **fisica**.
- Il renderer riconosce quando un buffer corrisponde 1:1 ai pixel fisici
  in cui cade e allora lo copia **senza filtro** (campionamento nearest,
  coordinate intere): il risultato è bit per bit il buffer dell'app.
- Solo quando le dimensioni non coincidono (app vecchie con scala intera,
  animazioni di scala) si ricampiona, con un filtro di qualità: bicubico o
  Lanczos a 4 campioni per l'ingrandimento, mipmap per la riduzione. Mai il
  bilineare "di default" che ammorbidisce tutto.

### 3.4 Posizioni agganciate ai pixel fisici

**Come è fatto (S2).** Una superficie il cui buffer è grande quanto la sua
area fisica (a meno dell'arrotondamento) si aggancia al pixel fisico più
vicino e occupa esattamente i pixel del buffer: copia 1:1, campionamento
nearest. Arrotondare i due bordi separatamente, a 150%, dava a volte un
riquadro di un pixel più grande del buffer, e tutta la finestra passava
dal filtro.

La posizione **fisica** di ogni finestra (origine della superficie
principale) è intera. Le animazioni calcolano posizioni logiche frazionarie,
ma a ogni frame l'origine viene agganciata al pixel fisico più vicino:
niente testo che "trema" di mezzo pixel durante un movimento. Solo le
trasformazioni di scala (apertura, riduzione a icona) ricampionano, e per
la loro breve durata.

### 3.5 Dimensioni logiche scelte bene

**Come è fatto (S2).** Snap, massimizzazione e schermo intero partono
dall'area in pixel fisici (le metà si dividono in pixel); la posizione
logica della finestra è quella esatta, anche frazionaria, e la dimensione
intera si sceglie perché il buffer del client (round(dimensione × scala)
in 120esimi) copra esattamente quei pixel. Quando è impossibile (a 150%
2560 pixel sarebbero 1706,67 unità) il pixel in più sborda **fuori dallo
schermo**, se l'area ne tocca un bordo senza un altro schermo accanto;
altrimenti si resta un pixel dentro, senza sovrapporsi ad altro.

Con scale frazionarie una finestra può avere una dimensione logica che non
diventa un numero intero di pixel fisici. Il compositor, quando **decide**
una dimensione (massimizzata, snap, schermo intero), la sceglie in modo che
i bordi fisici cadano esattamente sui bordi dell'area: si parte dai pixel
fisici e si torna al logico, non il contrario. Uno snap a metà schermo su un
monitor 2560 px al 125% è 1280 px fisici per lato, non "1024 logici e poi
vediamo".

### 3.6 Più schermi con scale diverse

- Ogni schermo ha la sua scala; una finestra a cavallo di due schermi
  riceve come scala preferita quella dello schermo che ne mostra la parte
  maggiore, e sugli altri viene ricampionata con il filtro di qualità.
- Il cursore ha la dimensione giusta su ogni schermo (tema di cursori
  caricato per ogni scala in uso).

### 3.7 La shell

(S2: Qt 6 usa già `fractional-scale-v1` e l'arrotondamento PassThrough di
default; verificato nitido al 125% annidato in KDE.)

La shell Qt usa `fractional-scale-v1` e `viewporter` anche per le
superfici layer-shell (Qt ≥ 6.5), con
`QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough`. Le misure del tema sono
logiche; bordi e linee sottili vanno disegnati allineati ai pixel fisici.

### 3.8 La scala predefinita

Come Windows, Vela sceglie da sé la scala di ogni schermo la prima volta
che lo vede; l'utente può poi cambiarla (impostazioni, e già oggi
`VELA_SCALE`).

- DPI = diagonale in pixel / diagonale in pollici (dimensioni fisiche
  dall'EDID).
- Scala = DPI / riferimento, arrotondata al 25% più vicino, tra 100% e
  300%. Riferimento 96 DPI per i monitor esterni, 105,6 (96 × 1,1) per i
  pannelli interni dei portatili, che si guardano più da vicino.
- Dimensioni fisiche assenti o assurde (proiettori, TV, alcuni adattatori):
  100%.

Risultati della formula su schermi tipici (punto di partenza, da tarare
con monitor veri):

| Schermo | DPI | Scala |
|---|---|---|
| 24" 1920×1080 | 92 | 100% |
| 27" 2560×1440 | 109 | 125% |
| 32" 2560×1440 | 92 | 100% |
| 34" 3440×1440 | 110 | 125% |
| 27" 3840×2160 | 163 | 175% |
| 32" 3840×2160 | 138 | 150% |
| portatile 15,6" 1920×1080 | 141 | 125% |
| portatile 14" 1920×1200 | 162 | 150% |
| portatile 16" 2560×1600 | 189 | 175% |
| portatile 14" 2880×1800 | 243 | 225% |

### 3.9 Le app X11

Le app solo-X11 (Xwayland) non conoscono la scala frazionaria. Decisione:
**le disegnano a 1× e il compositor le ingrandisce**, con il filtro di
qualità del §3.3: hanno la misura giusta e sono un po' sfocate, come su
Windows. Più avanti un'impostazione, come in Plasma, permetterà di farle
scalare da sole (Xwayland a scala 1 e DPI comunicati via Xft/XSETTINGS):
nitide, ma dipende da quanto ogni app X11 supporta il ridimensionamento.

### 3.10 Come lo dimostriamo

Test automatici (sessione headless + `vela-shot`) con un **client di prova**
che disegna motivi "cattivi": righe alternate di un pixel, scacchiere,
testo a 1 px. A ogni scala supportata il test confronta **bit per bit** i
pixel sullo schermo con il buffer del client, anche dopo averlo spostato,
agganciato e massimizzato. Un solo pixel diverso fa fallire il test.

### 3.11 La lente di ingrandimento

La lente (Accessibilità, Win+più) non è un effetto sull'immagine finita:
lo schermo del cursore costruisce la scena a partire da un altro punto del
layout e a una scala più grande (`OutputFrame::setMagnifier`). Ogni
superficie si disegna quindi dal suo buffer alla dimensione ingrandita, col
filtro bicubico del §3.3, e il testo resta più leggibile che ingrandendo
dei pixel già disegnati. Il danno lo trova il confronto con il frame
precedente (§6): spostando la zona cambia tutto. Il cursore va dove si vede
il punto che indica (`wlr_output_cursor_move`); la zona lo segue quando
arriva ai bordi, e un cambio d'ingrandimento lascia fermo il punto sotto il
cursore.

## 4. Tempo e frame: qualunque frequenza

### 4.1 Un orologio per schermo

Ogni schermo ha il suo ciclo di frame, indipendente dagli altri: un 60 Hz e
un 240 Hz collegati insieme lavorano ciascuno al proprio ritmo.

### 4.2 Le animazioni guardano al futuro

Oggi le animazioni usano l'istante in cui arriva l'evento "frame". Il
renderer userà invece l'**istante previsto di presentazione** del frame che
sta disegnando (ultimo vblank reale + periodo, dal feedback di
presentazione del backend). Così il movimento è corretto al frame, anche a
360 Hz, anche con VRR, anche quando un frame arriva in ritardo.

**La latenza dello schermo si impara** (scoperto in S0). Tra la consegna di
un frame e la sua comparsa possono passare più vblank: in una finestra
annidata in KWin 1 o 2, a seconda di come KWin sta lavorando; su certi
driver o con certi piani anche sull'hardware. Il `FrameClock` confronta
ogni presentazione con la previsione: se le ultime 8 misure concordano su
uno scarto di N periodi interi, aggiunge N alla latenza. Misurato in S0: a
180 Hz annidati in KWin, dopo l'apprendimento, errore medio di 1 µs.

### 4.3 Disegnare tardi (late latching)

Il renderer misura quanto tempo CPU+GPU serve per un frame (percentile
alto delle misure recenti) e comincia a disegnare **quel tempo più un
margine prima del vblank**, non subito dopo il vblank precedente. Input e
animazioni campionati più tardi = latenza minore. Se la stima sbaglia e si
perde un vblank, il margine cresce da solo.

**Come è fatto (S3).**

- **Il ciclo.** Quando il backend dice "frame" (al vblank dopo una
  consegna, o subito se lo schermo era fermo), `FrameClock::plan` sceglie
  il vblank per cui disegnare, il primo raggiungibile cominciando almeno
  "costo + margine" prima, e un timer (`timerfd`, con il timer slack del
  processo a 1 ns) sveglia il compositor a quell'istante. Le richieste che
  arrivano nel frattempo (commit delle app, input) finiscono nello stesso
  frame.
- **Il costo** di un frame è misurato dall'istante in cui doveva cominciare
  a quello in cui era pronto: commit fatto *e* GPU finita. La fine della
  GPU si legge con i timestamp della GPU (`vkCmdWriteTimestamp2`) portati su
  `CLOCK_MONOTONIC` con `VK_KHR_calibrated_timestamps`. Senza timestamp
  calibrati conta la durata del lavoro della GPU sommata al commit. Il
  costo usato è il massimo dell'ultimo secondo (almeno 8 frame): un frame
  lento non deve far perdere il vblank a quelli dopo.
- **Il margine** parte da 1 ms (`VELA_LATCH_MARGIN`). Un frame comparso
  tardi lo fa crescere subito (+max(0,25 ms, periodo/10)); poi torna giù di
  0,25 ms al secondo. Il primo secondo dopo l'avvio o un cambio di modo non
  conta (allocazioni e modeset fanno arrivare tardi qualche frame una volta
  sola). Il budget (costo + margine) non supera mai "periodo −
  min(0,5 ms, periodo/4)": subito dopo un vblank si fa sempre in tempo per
  il successivo, quindi la frequenza non si dimezza mai.
- **Chi è in ritardo.** Se il frame arriva tardi e il margine può ancora
  crescere, il ritardo è nostro (margine). Se il margine è già al massimo,
  il ritardo è dello schermo e lo impara la latenza (§4.2). Imparata una
  latenza più alta, il margine in più si azzera.
- **Mai un commit senza buffer.** Con DRM un commit senza buffer è
  bloccante: ferma il compositor fino al vblank di quello schermo. Quando
  c'è solo il cursore da spostare si consegna comunque un buffer (con danno
  vuoto il disegno non costa quasi nulla). I frame si chiedono senza
  `wlr_output_schedule_frame`, che segnerebbe lo schermo come bisognoso di
  un commit anche quando su di lui non cambia nulla. Emerso dalla prima
  prova da TTY: un 75 Hz accanto al 180 Hz si bloccava ~12 ms a ogni
  movimento e trascinava il 180 Hz sotto i 60 fps.
- **Priorità.** Il thread principale chiede lo scheduling realtime
  (`SCHED_RR` a priorità 10, non ereditato da shell e app; serve
  `RLIMIT_RTPRIO` o `CAP_SYS_NICE`, `VELA_REALTIME=0` lo spegne). La coda
  della GPU chiede la priorità alta (`VK_KHR_global_priority`), che amdgpu
  concede solo con `CAP_SYS_NICE` (come a `kwin_wayland`); senza, resta
  normale.
- **Il vblank virtuale** dell'headless simula uno schermo vero: un frame
  compare al primo vblank in cui era pronto (anche la GPU deve aver
  finito); se non lo era, resta il precedente e il vblank è perso.
- **Le statistiche** (`VELA_DEBUG=1`, ogni 2 s) dicono: costo, margine,
  tempo medio **dal disegno alla luce** (la latenza che il compositor
  aggiunge a input e animazioni), errore della previsione, vblank persi.
  `VELA_LATCH=0` spegne il late latching per confronto.

Misure (headless, vkcube, RX 9070 XT): dal disegno alla luce 1,1–1,9 ms a
ogni frequenza tra 60 e 360 Hz, contro un periodo intero senza late
latching (2,78 ms a 360 Hz, 16,7 ms a 60 Hz).

**Annidati in KWin** il margine si adatta all'ospite. KWin a 180 Hz compone
subito dopo il vblank, quindi il margine sale al massimo e si disegna appena
arriva il frame callback. I frame isolati (testo scritto in una finestra)
ora hanno la stessa previsione esatta delle animazioni: errore 0,001 ms,
contro fino a un periodo in S1.

### 4.4 VRR (rinviato, non dimenticato)

Il VRR si tratterà dopo le prime tappe, ma il progetto non deve
precluderlo. Cosa ne sappiamo già:

- con VRR non c'è un vblank fisso: si presenta appena il frame è pronto,
  entro i limiti del monitor, e il ciclo di frame non deve aspettare un
  vblank che non arriva;
- le animazioni restano corrette grazie al §4.2 (tempo previsto di
  presentazione, non "un periodo fisso");
- resta da decidere se durante le animazioni si sale al massimo del
  monitor o si segue la frequenza del contenuto.

Fino ad allora il VRR resta spento di default (`VELA_VRR=1` per provarlo),
come oggi.

### 4.4.1 Tearing

Un gioco a schermo intero può chiedere con `wp-tearing-control-v1` di
mostrare ogni fotogramma appena è pronto, anche a metà schermo. Lo si
concede solo nello scanout diretto (§5.3), con lo scambio di pagina
asincrono di DRM (`tearing_page_flip`); se il driver non lo accetta il
frame va col vblank come sempre. Finché dura, il ciclo di frame di quello
schermo non pianifica il late latching (§4.3): disegna (cioè consegna il
buffer del gioco) appena il gioco fa il commit. Si spegne nelle
Impostazioni ("tearing" in vela.conf) o con `VELA_TEARING=0`.

### 4.5 Frame callback e feedback

- `wl_surface.frame` si manda dopo che il frame **è stato presentato**, alle
  sole superfici visibili su quello schermo; una superficie su due schermi
  segue lo schermo "principale" (quello con la parte maggiore; a parità,
  quello più veloce).
- `wp_presentation` riceve i tempi di presentazione veri.
- Le superfici nascoste (coperte, ridotte a icona, altro spazio di lavoro)
  non ricevono callback: le app smettono di disegnare e consumare.

## 5. La scena

### 5.1 Cosa contiene

- **Finestre** (toplevel xdg), **superfici della shell** (layer), **popup**.
- **Nodi di Vela**: rettangoli, istantanee (animazioni di chiusura e
  riduzione, che già esistono), anteprime dello snap.
- Ogni nodo ha: trasformazione (traslazione + scala attorno a un punto),
  opacità, ritaglio con **rettangolo arrotondato**, ombra, sfocatura dello
  sfondo, visibilità.

### 5.2 Le superfici si leggono dal vivo

La scena **non copia** l'albero di superfici e sottosuperfici di un'app: al
momento di disegnare lo percorre direttamente da wlroots
(`wlr_surface`, `wlr_subsurface`, stati sincronizzati già risolti da
wlroots). Meno stato duplicato = meno bug. La scena tiene solo ciò che è
nostro: posizione, effetti, animazioni.

### 5.3 Ciò che `wlr_scene` faceva e ora facciamo noi

| Compito | Come | Stato |
|---|---|---|
| Import dei buffer | shm → texture nostra, ricaricata solo dove l'app ha disegnato (con il riuso dei `wlr_client_buffer` di wlroots); dmabuf → import diretto (§7.3), una volta per buffer | ✔ S1 |
| Damage | a ogni frame la scena appiattita si confronta con il frame precedente (elementi apparsi, spariti, spostati, cambiati, saliti sopra altri); il contenuto delle superfici arriva dai commit, con il danno preciso dell'app; età dei buffer con `wlr_damage_ring` | ✔ S1 |
| Occlusione | ciò che è coperto da regioni opache non si disegna e non riceve frame callback | ✔ S1 |
| Frame callback, presentation | §4.5: solo alle superfici visibili, dallo schermo che ne mostra la parte visibile maggiore | ✔ S1 |
| Output enter/leave | per ogni superficie e sottosuperficie, dagli schermi su cui cade; una superficie nascosta ovunque non riceve leave (ricomparendo non cambia nulla per l'app) | ✔ S1 |
| Scala preferita | dallo schermo che ne mostra la parte maggiore (§3.6), intera e frazionaria | ✔ S1 |
| Catture | finestre (`ext-image-capture`, anteprime di Alt+Tab): il renderer disegna solo quella finestra; schermi (screencopy, `ext-image-copy-capture`): via il nostro `wlr_renderer` | ✔ S1 |
| Istantanee | buffer bloccati disegnati come nodi della scena | ✔ S1 |
| Cursore | hardware tramite wlroots (col nostro renderer); disegnato da noi quando non c'è il piano cursore | ✔ S1 |
| dmabuf feedback | per superficie: tranche di scanout quando la finestra è a schermo intero (dopo 30 frame di fila da candidata, come `wlr_scene`), di nuovo quella predefinita quando smette | ✔ S3 |
| Direct scanout | una sola superficie visibile, opaca (formato senza alfa o regione opaca su tutto), che copre lo schermo 1:1 con la sua trasformazione, senza cursore disegnato da noi né catture in corso → il suo buffer va direttamente sul piano primario; con sincronizzazione esplicita il backend aspetta il punto di acquisizione e fa scattare il rilascio quando smette di mostrarlo | ✔ S3 |
| Formati YUV (video) | NV12 e simili, con conversione nello shader | S7 |

## 6. Il frame, passo per passo

Per ogni schermo, quando è il momento (§4.3):

1. **Tempo**: si calcola l'istante previsto di presentazione; le animazioni
   avanzano a quell'istante.
2. **Lista di disegno**: la scena viene appiattita, dal fondo verso l'alto,
   in una lista di "quad" in coordinate fisiche di quello schermo:
   texture o colore, ritaglio arrotondato, opacità, trasformazione,
   filtro (nearest se 1:1, di qualità altrimenti), effetti.
3. **Occlusione**: ciò che è completamente coperto da superfici opache viene
   scartato (regioni opache dichiarate dai client + nodi nostri opachi).
4. **Danno**: regione da ridisegnare = danni accumulati per l'età del buffer
   di destinazione, **allargata** dove serve dalla sfocatura (§8.3).
5. **Scanout diretto?** Se possibile (§5.3), niente disegno: si presenta il
   buffer del client.
6. **Disegno** nella regione danneggiata, in un unico command buffer:
   sfondo → per ogni elemento: eventuale sfocatura dello sfondo sotto di
   esso, ombra, contenuto ritagliato.
7. **Presentazione**: commit atomico con fence di fine rendering; al vblank,
   frame callback e feedback di presentazione.

## 7. Vulkan

### 7.1 Requisito minimo

**Vulkan 1.4** (dynamic rendering e synchronization2 dalla 1.3, semafori
timeline dalla 1.2, push descriptor dalla 1.4: le texture si legano a ogni
disegno senza pool di descrittori) più le estensioni per scambiare buffer
con kernel e client:

- `VK_EXT_image_drm_format_modifier`, `VK_EXT_external_memory_dma_buf`,
  `VK_KHR_external_memory_fd`, `VK_EXT_queue_family_foreign`
- `VK_KHR_external_semaphore_fd` (sync_file, per la sincronizzazione
  implicita ed esplicita)
- `VK_EXT_physical_device_drm` (scegliere la GPU che pilota lo schermo)
- facoltativa: `VK_KHR_calibrated_timestamps` (a che ora la GPU finisce un
  frame, per il late latching §4.3; senza, si misura solo la durata)

In pratica: AMD GCN e successive (RADV) e Intel Gen9/Skylake e successive
(ANV) con Mesa ≥ 25.0, NVIDIA con driver proprietario ≥ 570 o NVK. Chi non
ha Vulkan 1.4 non può usare Vela: è scritto nel README e detto all'avvio con
un messaggio comprensibile, non con un crash.

### 7.2 Un device nostro

Vela crea il proprio `VkInstance`/`VkDevice` sulla GPU dello schermo
(`VK_EXT_physical_device_drm` confrontato con il device DRM del backend).
Non usiamo il renderer di wlroots per disegnare. Per allocare i buffer degli
schermi scriviamo un nostro `wlr_allocator` (`wlr_allocator_init` è
pubblica): buffer GBM con un modifier esplicito accettato dal piano
primario (`wlr_output_get_primary_formats`).

**Il nostro renderer parla anche la lingua di wlroots** (scelto in S1).
wlroots disegna da sé in alcuni punti: prepara il buffer del cursore
hardware, copia lo schermo per screencopy ed `ext-image-copy-capture`,
carica i buffer `wl_shm` delle app in texture a ogni commit (solo la parte
cambiata, con la logica di riuso dei `wlr_client_buffer`). Invece di
tenere per questo un secondo renderer, il nostro renderer implementa anche
l'interfaccia pubblica `wlr_renderer` (`wlr/render/interface.h`): texture,
render pass e lettura dei pixel sono nostri, con lo stesso device, gli
stessi shader, la stessa fusione in spazio lineare e la stessa
sincronizzazione. Il codice è in `compositor/src/render/`:

| File | Cosa fa |
|---|---|
| `vulkan.*` | device, formati importabili (texture, destinazioni, wl_shm), import dei dmabuf |
| `renderer.*` | invio alla GPU con semaforo timeline, distruzione differita, memoria di appoggio per i caricamenti, destinazioni dmabuf, pipeline; timeline syncobj per i rilasci delle app e timestamp della GPU (S3); il `wlr_renderer` |
| `frame_clock.hpp` | il tempo di uno schermo: griglia dei vblank, latenza imparata, costo e margine del late latching, piano di ogni frame (§4) |
| `texture.*` | texture da dmabuf (una importazione per buffer) e da memoria (caricamento solo delle zone cambiate), lettura dei pixel |
| `pass.*` | il disegno: quad con texture o a tinta unita, ritaglio per rettangoli, barriere, sincronizzazione implicita ed esplicita, misura dei tempi; il `wlr_render_pass` |

La scena e il frame sono in `compositor/src/scene/` (§5, §6).

### 7.3 Buffer dei client e sincronizzazione

- dmabuf: import con il modifier del client, cache per buffer; transizione
  di proprietà dalla coda "foreign" a ogni uso.
- Sincronizzazione implicita: sync_file estratto dal dmabuf
  (`DMA_BUF_IOCTL_EXPORT_SYNC_FILE`) e atteso come semaforo; al termine,
  sync_file reinserito nel dmabuf.
- Sincronizzazione esplicita (`linux-drm-syncobj-v1`, già in wlroots 0.20):
  punti di attesa e rilascio dei timeline del client.
- Verso lo schermo: fence di fine rendering passata al commit.

**Come è fatto (S3).** Il renderer ha una timeline syncobj sua (dal render
node): ogni disegno ne fa scattare un punto importandovi il sync_file di
fine lavoro. Per ogni app con `linux-drm-syncobj-v1` letta da un disegno
(schermo o cattura), quel punto diventa un suo punto di rilascio
(`wlr_linux_drm_syncobj_v1_state_add_release_point`): wlroots rilascia il
buffer all'app quando tutti gli schermi che l'hanno letto hanno finito e
l'app ne ha mandato un altro. L'attesa è il punto di acquisizione esportato
come sync_file, al posto della fence implicita del dmabuf. Il renderer
dichiara `features.timeline`, così anche wlroots (catture) può chiedere
punti di attesa e di fine lavoro. Verso lo schermo resta la
sincronizzazione implicita, che basta. `WLR_RENDER_NO_EXPLICIT_SYNC=1` spegne
il protocollo. Provato con vkcube e mpv (Vulkan, Mesa 26): le app girano
senza fermarsi, quindi i rilasci arrivano.

### 7.4 Shader e pipeline

- GLSL compilato in SPIR-V **a compile time** (`glslc`) e incluso
  nell'eseguibile. Nessun compilatore di shader a runtime.
- Poche pipeline, generiche: quad con texture (ritaglio arrotondato,
  opacità, filtro scelto), quad a tinta unita, ombra analitica,
  downsample/upsample della sfocatura, composizione "acrylic".
- Niente librerie pesanti: API C di Vulkan con piccoli wrapper RAII come
  `Listener`. Allocazione della memoria nostra (poche risorse, a lunga
  vita; i buffer dei client sono memoria esterna). Si valuterà VMA solo se
  servirà davvero.

### 7.5 Colore

- Fusione in **spazio lineare** (viste `_SRGB` o intermedi fp16), non sui
  valori sRGB come fanno quasi tutti i compositor: sfumature e trasparenze
  corrette.
- Schermi a 10 bit dove disponibili.
- Più avanti HDR e `color-management-v1` (già in wlroots 0.20): l'impianto
  lineare fin dall'inizio lo rende un'estensione, non una riscrittura.

### 7.6 Luce notturna e filtri colore

Entrambi sono una matrice 3x3 in spazio lineare (la Luce notturna è una
diagonale: quanto resta di rosso, verde e blu alla temperatura scelta).

- **Filtri colore** (scala di grigi, correzioni per i daltonismi): nel
  disegno. La matrice va nelle costanti di ogni quad (`QuadPush.filter`, bit 2
  dei flag) e la applicano gli shader di texture, rettangoli e ombre. Dato
  che è lineare, e che le fusioni sono combinazioni lineari in spazio
  lineare (§7.5), applicarla a ogni disegno equivale ad applicarla
  all'immagine finita, senza un passaggio in più. La sfocatura legge uno
  sfondo che ha già il filtro: nella composizione acrylic lo prende solo
  la tinta. Col filtro acceso niente scanout diretto, e il cursore lo
  disegna il renderer (lo prende anche lui).
- **Luce notturna**: sugli schermi veri nella **gamma del monitor**
  (`wlr_output_state_set_color_transform` con una tabella 3x1D: ogni
  valore codificato torna in luce lineare, prende il guadagno e si
  ricodifica), provata prima con un commit di prova e poi mandata col
  frame. Così non finisce negli screenshot né nella condivisione dello
  schermo, prende anche il cursore hardware e lo scanout diretto dei giochi
  resta. Dove la gamma non c'è (annidato, headless) o il monitor non la
  accetta, si moltiplica nella matrice del disegno come i filtri.
- Il passaggio (un secondo, come Windows) è un'animazione del §4.2: a ogni
  frame un nuovo valore, e tutto lo schermo ridisegnato (o solo una nuova
  gamma).

## 8. Effetti

### 8.1 Angoli arrotondati

Ritaglio con la distanza da un rettangolo arrotondato (SDF) calcolata nello
shader, in **pixel fisici**, con antialiasing di esattamente un pixel
fisico. Nitido a ogni scala. Si applica al riquadro della finestra (non ai
margini d'ombra disegnati dall'app).

Com'è fatto: un albero della scena può avere una **forma** (`scene::Shape`:
rettangolo, raggio, ombra); gli elementi dei suoi figli ereditano il
ritaglio (i popup no: `Node::unclipped`). La finestra calcola la sua a ogni
frame (`Toplevel::updateShape`): raggio 8, niente da massimizzata, a
schermo intero o agganciata, né per le app con margini d'ombra propri
(GTK). Gli angoli non contano come opachi; con gli angoli niente scanout
diretto. Le istantanee delle animazioni portano con sé la forma. Il test
di nitidezza resta bit per bit fuori dai quadrati degli angoli.

### 8.2 Ombre

Ombra analitica di un rettangolo arrotondato (formula chiusa
dell'integrale di una gaussiana, tecnica di Evan Wallace): un quad, nessuna
texture, nessuna sfocatura. Due strati come Windows 11 (ombra ampia e
morbida + ombra di contatto stretta), più marcate per la finestra attiva.
Le app con decorazioni proprie disegnano già la loro ombra: se la geometria
xdg indica margini d'ombra, la nostra non si disegna.

Com'è fatto: due elementi per finestra (ampia: sigma 14, scostata di 8 in
basso; di contatto: sigma 2), più scuri per quella attiva, sotto il
contenuto. Lo shader non disegna sotto la finestra, e il suo ritaglio
esclude l'interno (tranne gli angoli): costa una cornice, non tutta l'area.

### 8.3 Sfocatura dal vivo

- **Chi viene sfocato**: le zone che un client chiede con il protocollo
  standard `ext-background-effect-v1` (la shell per taskbar, menu Start,
  Alt+Tab, notifiche; le app che lo supportano) e le nostre decorazioni
  lato server. Non "tutto ciò che è trasparente": i margini d'ombra delle
  app sono trasparenti e non vanno sfocati.
- **Algoritmo**: dual Kawase (una catena di riduzioni e ingrandimenti a
  metà risoluzione), poi la ricetta acrylic: saturazione, tinta, luminosità
  e un velo di rumore per non avere bande. Il raggio è in unità logiche,
  quindi identico a ogni scala.
- **Costo sotto controllo**:
  - si sfoca solo la parte **danneggiata** dietro ciascuna zona, allargata
    del raggio;
  - lo sfondo sfocato di ogni zona viene **tenuto in cache**: se dietro la
    taskbar non è cambiato niente (il caso normale), la sfocatura non si
    ricalcola affatto;
  - un cambiamento dietro una zona sfocata danneggia anche la zona stessa
    (è così che la sfocatura "segue" ciò che si muove dietro).
- Sfocature una sopra l'altra (menu Start sopra la taskbar) si compongono
  nell'ordine giusto perché si disegna dal fondo verso l'alto.

Com'è fatto: il protocollo lo implementa Vela (`scene/effects.cpp`, wlroots
non lo ha). Nel disegno, arrivati a un pannello con una regione da
sfocare, si chiude il render pass, si legge ciò che è già disegnato sotto
(il buffer dello schermo, importato anche come texture se il formato lo
permette), si fanno quattro riduzioni e tre ingrandimenti dual Kawase in
immagini a 16 bit lineari che crescono quando serve e si riusano, poi si
compone la ricetta acrylic e si riprende. La forma la dà l'alfa del
pannello: la shell chiede rettangoli, gli angoli arrotondati restano. Il
danno che tocca una zona (col raggio) la fa ridisegnare tutta, raggio
compreso. Costo misurato: 0,05–0,13 ms di GPU per frame con il menu Start
che si apre sopra la taskbar. La cache dello sfondo sfocato (tenerlo tra
un frame e l'altro se dietro non cambia nulla) non è ancora servita: a
riposo non si ridisegna niente comunque.
- **Anche a batteria, tutto al massimo**: nessuna riduzione automatica
  degli effetti. Il risparmio viene dal non disegnare ciò che non cambia,
  non dal togliere gli effetti.

## 9. La barra del titolo di Vela

Oggi le app Qt/KDE disegnano da sé la barra "di ripiego" di Qt, perché
Vela non offre decorazioni lato server. La barra di Vela è il primo
elemento "nostro" che unisce tutto ciò che il renderer sa fare: testo,
sfocatura, angoli, ombre, animazioni.

**Seconda versione (ottobre 2026)**: anche per le app Wayland, con
`xdg-decoration` (Vela chiede sempre la barra lato server; Qt e KDE la
accettano, GTK4 no). La barra fa parte della geometria anche per loro, e
alle app va la parte sotto. A sinistra l'icona dell'app (dal `.desktop`
via `app_id` o `StartupWMClass`, dal tema di icone di KDE, SVG disegnato
con librsvg alla dimensione fisica; senza librsvg, facoltativa, niente
icona): un clic apre il menu della finestra, un doppio clic la chiude. Lo
sfondo è la tinta Mica: la shell manda al compositor il colore medio
dello sfondo del desktop (`wallpaper-tint`), reso sicuro per il testo e
mescolato al grigio di Windows 11 (di più da inattiva). La sfocatura dal
vivo dietro la barra (§9.4, ultimo punto) è rinviata: la tinta piena è già
vicina a Mica, che su Windows non mostra le finestre dietro.

**Prima versione (settembre 2026)**, per le sole app X11 che lasciano la
barra al gestore di finestre (`compositor/src/decoration.*`): misure di
§9.3, colori fissi del tema scuro di Windows 11 (niente sfocatura né tinta
ancora), titolo con FreeType + HarfBuzz nel font di KDE rasterizzato alla
scala fisica dello schermo, simboli dei pulsanti disegnati come segmenti
antialiasati alla dimensione fisica. La barra fa parte della geometria
della finestra (32 unità sopra la superficie), così snap, massimizzazione
e posizionamento la includono; all'app X11 va solo la parte sotto. Il
trascinamento dal titolo parte dopo 4 unità di movimento (un doppio clic
non ripristina una finestra massimizzata). Mancano i bordi invisibili per
ridimensionare e l'icona dell'app.

### 9.1 Chi la usa

- Il protocollo `xdg-decoration` permette al compositor di dire "la
  disegno io". Vela lo preferisce sempre.
- Le app Qt/KDE la accettano: guadagno immediato e visibile.
- Firefox e Chromium lasciano scegliere all'utente; le app GTK4/libadwaita
  disegnano sempre la propria barra e non si possono convincere. Per loro
  valgono comunque angoli arrotondati e ombra del compositor.

### 9.2 Chi la disegna: il compositor

Due strade:

- **La shell (Qt) disegna le barre** come superfici separate. Si riusa il
  QML, ma a ogni ridimensionamento due processi devono presentare nello
  stesso frame: se non ci riescono, barra e finestra si "staccano" per un
  frame. È esattamente il tipo di difetto che Vela non deve avere.
- **Il compositor disegna la barra** nello stesso frame e con la stessa
  geometria della finestra. Nessuna sincronizzazione tra processi, testo
  rasterizzato direttamente alla scala fisica dello schermo. Serve un
  piccolo motore di testo nel compositor (FreeType + HarfBuzz +
  fontconfig, già presenti su ogni sistema: li usa anche Qt).

**Proposta: il compositor.** Il motore di testo servirà comunque anche per
l'overlay di debug (§10) e per altra interfaccia del compositor.

### 9.3 Com'è fatta

- **Misure** come Windows 11: barra alta 32 logici, pulsanti 46×32, angoli
  con raggio 8, bordi di ridimensionamento invisibili di 8 logici fuori
  dalla finestra.
- **Sfondo**: sfocatura dal vivo di ciò che sta dietro, con una **tinta
  derivata dallo sfondo del desktop** (come Mica su Windows): vedi §9.4.
- **Pulsanti** riduci/massimizza/chiudi: glifi disegnati come SDF (linee),
  nitidi a ogni scala; passaggio del mouse animato; "chiudi" rosso.
- **Titolo**: testo con antialiasing in scala di grigi (niente subpixel
  RGB: non va d'accordo con trasparenze e animazioni), glifi rasterizzati
  per ogni scala fisica e mai ingranditi, in un atlante di texture. Il
  font è **quello di KDE** (§9.5).
- **Icona dell'app**: **sempre SVG** quando il tema la offre (§9.6).
- **Forma unica**: barra e contenuto dell'app sono ritagliati insieme come
  un solo rettangolo arrotondato, con l'ombra attorno all'insieme. Da
  massimizzata o agganciata, niente angoli sui lati che toccano i bordi.
- **Interazioni**: trascina per spostare (con lo snap), doppio clic per
  massimizzare, clic destro per il menu della finestra (§14.8); più avanti, col
  mouse sul pulsante "massimizza", i layout di snap di Windows 11.

### 9.4 La tinta, dallo sfondo del desktop

Come Mica su Windows, la barra "prende" i colori dello sfondo che le sta
dietro, così ogni finestra si intona al desktop senza impostazioni:

- quando lo sfondo cambia, il compositor ne ricava una versione minuscola
  (una griglia di pochi colori, per esempio 32×18, dalla catena di mipmap
  della sua texture: lo sfondo è una superficie della shell che il
  compositor sta già disegnando);
- la tinta di una barra è quella griglia letta nella posizione della
  barra: si aggiorna mentre la finestra si sposta, al costo di un valore
  passato allo shader;
- il colore viene poi **reso sicuro per il testo**: saturazione ridotta e
  luminosità portata nella fascia del tema (scuro o chiaro), in modo che il
  contrasto con il titolo resti almeno 4,5:1 (la soglia WCAG);
- risultato finale = sfocatura dal vivo di ciò che sta dietro, mescolata
  con la tinta, più un velo di rumore; da inattiva la tinta pesa di più e
  la barra si "spegne", come su Windows.

### 9.5 Il font, da KDE

Per ora il titolo usa il font scelto in KDE, così le barre sono coerenti con
le app Qt: `activeFont` nella sezione `[WM]` di `kdeglobals`, altrimenti
`font` in `[General]`, altrimenti il predefinito di KDE (Noto Sans 10 pt),
trovato con fontconfig. Le dimensioni in punti diventano pixel logici a
96 DPI (10 pt = 13,33 px) e poi pixel fisici con la scala dello schermo.
Il font si rilegge quando `kdeglobals` cambia.

### 9.6 Le icone, sempre SVG

Per la nitidezza a ogni scala le icone si disegnano **dal vettoriale**,
alla dimensione fisica esatta: un'icona da 16 logici è di 20 pixel al 125%
e di 24 al 150%, disegnata direttamente a quella misura.

- Si cerca l'icona nel tema (specifica freedesktop), preferendo sempre la
  versione SVG; le PNG si usano solo se l'app non ha altro, scegliendo la
  più grande e riducendola con il filtro di qualità.
- Per disegnare gli SVG propongo **resvg**: è il più fedele alla specifica,
  ha un'API C, non trascina cairo e glib nel compositor ed è nei
  repository ufficiali di Arch. Alternativa se in qualche distribuzione
  mancasse: lunasvg (C++, piccola) inclusa nel sorgente.
- Le icone disegnate si tengono in cache per (icona, dimensione fisica).

## 10. Misure e prove

- **Tempi**: timestamp GPU per ogni frame e per ogni effetto; tempo CPU;
  frame persi. `VELA_DEBUG_OVERLAY=1` li mostra sullo schermo.
- **Validazione**: `VELA_VULKAN_VALIDATION=1` attiva i validation layer.
- **Test d'immagine** (headless): scene di riferimento confrontate con
  immagini attese; per la nitidezza, confronto bit per bit (§3.10); per gli
  effetti, tolleranza piccola.
- **Matrice di prova**: scale 100/125/150/175/200%, frequenze simulate
  60/75/144/165/240/360 Hz, più schermi con scale diverse.
- **Vblank virtuale**: il backend headless di wlroots non simula uno
  schermo fedele (timer al millisecondo, 1000000/refresh troncato: a 144 Hz
  dà 166 fps; e "presenta" all'istante del commit). Per gli schermi
  headless Vela usa un proprio vblank virtuale al nanosecondo (`timerfd`
  con scadenze assolute su una griglia esatta): è su quello che si provano
  frequenze, previsione e vblank persi. `VELA_OUTPUT_SIZE=1920x1080@144`.

## 11. Tappe

Il compositor attuale (con `wlr_scene`) resta funzionante finché la nuova
scena non lo eguaglia; poi `wlr_scene` sparisce. Ogni tappa si chiude solo
con i suoi test.

| Tappa | Contenuto | Fatto quando |
|---|---|---|
| **S0** Fondamenta ✔ | device Vulkan nostro, allocatore GBM, import dei buffer degli schermi, sincronizzazione implicita (sync_file), shader compilati, scena di prova; ciclo di frame per schermo con tempo di presentazione previsto e latenza imparata; vblank virtuale per l'headless | fatto: 60–360 Hz simulati esatti (0 vblank persi, errore < 10 µs); annidato in KWin a 75 e 180 Hz reali, errore ~1 µs dopo l'apprendimento; validation layer (anche della sincronizzazione): nessun messaggio |
| **S1** Parità ✔ | scena propria con finestre, layer, popup, sottosuperfici; shm e dmabuf; damage; frame callback, presentation, enter/leave; istantanee, snap, catture portate sul nuovo renderer; Vulkan 1.4; il nostro renderer anche come `wlr_renderer` | fatto: `wlr_scene` e il renderer di wlroots rimossi. Provati headless e annidato in KWin: Konsole (shm), shell Qt Quick e Firefox (dmabuf), popup anche con sottosuperfici, menu Start, trascinamento, snap con anteprima, riduzione a icona e ripristino, chiusura, Alt+Tab con anteprime, screencopy, due schermi (enter/leave), scala 150%. Validation layer (anche della sincronizzazione): nessun messaggio. A riposo 0 CPU; trascinando una finestra a 144 Hz 17–29 ms di CPU su 3,4 s, contro 34–36 ms di `wlr_scene` |
| **S2** Nitidezza ✔ | fractional scale, aggancio ai pixel, filtri di qualità, più schermi con scale diverse, cursore per scala, scala predefinita dai DPI | fatto: `scripts/test-sharpness.sh` bit per bit a 100, 125, 150, 175, 200 e 225% (aperta, agganciata a sinistra e a destra, massimizzata, ripristinata); schermi misti 150% + 100%, bit per bit su quello al 150%. Prima delle correzioni: fino a 134 mila pixel diversi a 150% per una finestra centrata. Filtro bicubico Catmull-Rom per gli ingrandimenti; riduzioni ancora bilineari (mipmap da fare con le animazioni di scala, S4) |
| **S3** Tempo e latenza ✔ | late latching, scanout diretto, sincronizzazione esplicita, dmabuf feedback | fatto: headless a 60, 75, 144, 165, 240 e 360 Hz con vkcube: 0 vblank persi, errore di previsione 0, dal disegno alla luce 1,1–1,9 ms (senza late latching un periodo intero). Shell, Konsole e una finestra trascinata a 360 Hz: un solo frame in ritardo (wlroots che alloca un buffer nuovo della swapchain), assorbito dal margine. Scanout diretto con mpv a schermo intero: headless (OpenGL, sincronizzazione implicita, costo del frame 0,02 ms) e annidato in KWin (Vulkan, sincronizzazione esplicita, con il feedback dmabuf di scanout); le catture durante lo scanout funzionano. Validation layer (anche della sincronizzazione): nessun messaggio. A riposo 0 CPU |
| **S4** Forma ✔ | angoli arrotondati, ombre | fatto: SDF in pixel fisici e ombra analitica a due strati; `scripts/test-sharpness.sh` bit per bit a 100–225% fuori dagli angoli; validation layer: nessun messaggio |
| **S5** Sfocatura ✔ | `ext-background-effect`, dual Kawase, acrylic per la shell | fatto: taskbar, menu Start, menu, Alt+Tab e notifiche sfocati; 0,05–0,13 ms di GPU per frame mentre il menu Start si apre; a riposo niente; validation layer: nessun messaggio. Rinviata la cache dello sfondo sfocato |
| **S6** Barra del titolo ✔ | `xdg-decoration`, motore di testo con il font di KDE, pulsanti, icone SVG, tinta dallo sfondo, interazioni | fatto: le app che accettano xdg-decoration (Qt/KDE) hanno la barra di Vela, con l'icona dell'app (SVG del tema di KDE con librsvg, alla dimensione fisica) e la tinta Mica dello sfondo; bordi invisibili per ridimensionare. Rinviati: sfocatura dal vivo dietro la barra (per ora tinta piena), passaggio del mouse animato, resvg al posto di librsvg |
| **S7** Colore | 10 bit, HDR, `color-management-v1` | |
| (poi) VRR | politica di frequenza durante le animazioni | §4.4 |

## 12. Rischi

- **Quantità di lavoro di S1**: rifare ciò che `wlr_scene` dà gratis è la
  parte più lunga. Mitigazione: parità verificata con i test d'immagine
  contro il renderer attuale, finché esiste.
- **wlroots cambia API a ogni versione minore**: usiamo meno di wlroots
  rispetto a oggi (niente `wlr_scene`, niente renderer), quindi meno
  superficie esposta.
- **Driver**: dmabuf con modifier e sync_file sono ben supportati su
  AMD/Intel; NVIDIA va provato presto.
- ~~**Cursore hardware**: `wlr_output_cursor` usa il renderer di wlroots
  per preparare il buffer del piano cursore.~~ Risolto in S1: il renderer di
  wlroots, per wlroots, è il nostro (§7.2).
- ~~**Previsione dei frame isolati** (emerso in S1).~~ Risolto in S3: con il
  late latching ogni frame, isolato o no, parte alla stessa distanza dal
  vblank (annidati in KWin: errore 0,001 ms).
- **Late latching e scanout su DRM vero** (S3): late latching provato da
  TTY (RX 9070 XT, 2560×1440 a 180 Hz al 125% + 1920×1080 a 75 Hz):
  trascinando una finestra 0 vblank persi, CPU 0,03 ms per frame, lavoro
  della GPU 0,01–0,03 ms. Ancora da verificare: scanout accettato dal piano
  primario, feedback dmabuf che fa cambiare modifier alle app, margine
  minimo sotto 1 ms.
- **Le app lente frenano il compositor** (emerso in S3): un frame aspetta
  (nella GPU, o nel kernel con lo scanout) che le app abbiano finito di
  disegnare i buffer che mostra. Un'app in ritardo può quindi far perdere
  un vblank a tutto lo schermo, cursore e animazioni compresi. KWin e
  Mutter applicano il commit di un'app solo quando il suo buffer è pronto,
  e intanto mostrano il precedente. Per noi vuol dire tenere, per ogni
  superficie, l'ultimo stato pronto (oggi le superfici si leggono dal vivo,
  §5.2), e per la sincronizzazione esplicita conoscere il punto di
  acquisizione di un commit ancora in sospeso, che wlroots 0.20 non espone.
  **Deciso (S4):** rinviato a una tappa a sé dopo S6. wlroots 0.20 ha il
  modo di trattenere un commit (`wlr_surface_lock_pending`), ma non dice il
  punto di acquisizione di un commit in sospeso con la sincronizzazione
  esplicita, che è proprio quella delle app moderne (Mesa, Firefox): farlo
  solo per l'implicita coprirebbe le app sbagliate. Nelle prove da TTY
  nessun vblank perso per colpa di un'app.
- **Chiusura da annidati** (emerso a settembre 2026): circa una volta su
  dieci, chiudendo Vela annidato in KDE con un'app X11 aperta, il driver
  amdgpu andava in crash liberando la memoria della GPU nel distruttore
  del renderer (stato interno già rovinato; RADV e il GBM di Mesa lo
  condividono nel processo). Causa non trovata; mitigato non smontando
  renderer e device alla chiusura normale (il kernel recupera tutto). Con
  `VELA_VULKAN_VALIDATION=1` si smonta tutto, per trovare risorse
  dimenticate.
- **Rotazione degli schermi**: il frame la gestisce (elementi nello spazio
  ruotato, danno e disegno riportati al buffer), ma non è ancora stata
  provata su uno schermo vero.
- **Testo e icone nel compositor** (§9.2, §9.6): dipendenze nuove
  (FreeType, HarfBuzz, fontconfig, resvg) e codice delicato. Le prime tre
  sono su ogni sistema; resvg va verificato sulle altre distribuzioni.
- **Multi-GPU** (portatili ibridi, schermi collegati alla GPU secondaria):
  fuori da S0–S2, ma il device "per GPU dello schermo" (§7.2) non deve
  impedirlo.

## 13. Domande risolte

- Scala predefinita dai DPI → §3.8
- App X11 ingrandite → §3.9
- VRR rinviato → §4.4
- Nessuna riduzione a batteria → §8.3
- Barra del titolo in questo documento → §9
- Tinta derivata dallo sfondo → §9.4
- Font di KDE, per ora → §9.5
- Icone sempre SVG → §9.6
- Menu del tasto destro copiati da Windows 11, disegnati dalla shell → §14

Nuove domande emergeranno scrivendo il codice: si aggiungono qui.

## 14. I menu del tasto destro

Vela copia **in toto** i menu contestuali di Windows 11: stesse voci, stesso
ordine, stessi gruppi, stesso comportamento. Dove una voce non ha senso su
Linux la si traduce nell'equivalente più vicino (tabella in §14.11); la si
toglie solo se un equivalente non esiste.

> **Stato (settembre 2026):** fatti il componente (`Menus`, `ContextMenu`,
> `MenuPanel` nella shell), la jump list, lo spazio vuoto della taskbar,
> l'orologio, l'area di notifica, Win+X, i menu del menu Start (con la
> sezione "Aggiunte", che prima non c'era), il menu della finestra con
> Sposta e Ridimensiona da tastiera, il desktop, i campi di testo, Esegui
> (Win+R) e Win+D; poi le icone del desktop (la cartella Scrivania) con
> il menu del desktop e dei file, riga di icone compresa, e "Mostra altre
> opzioni" con i service menu di KDE, e la finestra Proprietà (Generale,
> Autorizzazioni, Dettagli). Le voci delle impostazioni (Personalizza,
> Impostazioni schermo, della taskbar e di notifica, Sistema, Win+I) aprono
> la pagina giusta dell'app Impostazioni (`vela-settings --page …`). Esplora
> (`vela-files`) usa lo stesso componente (`MenuPanel.qml`) dentro la sua
> finestra, con i menu dei file e dello spazio vuoto della cartella. Ci sono
> ma spente le voci che aspettano la loro funzione: Collegamento, Aggiungi a
> Preferiti, "Scegli un'altra app". Mancano i menu di ciò che non esiste ancora: la sezione
> Consigliati della Start, il menu "…" delle notifiche. Tolte
> perché senza equivalente: "Esegui come amministratore" (le app grafiche
> come root sotto Wayland di norma non partono), "Impostazioni app",
> "Condividi".

### 14.1 Chi li disegna: la shell

Tutti i menu li disegna **la shell**, con un solo componente Qt Quick
(`VelaMenu`: menu, voce, separatore, sottomenu) usato ovunque, così che
ogni menu di Vela sia identico agli altri. Il compositor non disegna menu:
quando serve il menu di una finestra (§14.8) lo chiede alla shell. Il menu
attuale dell'area di notifica (`TrayMenu.qml`) passerà a questo
componente.

La superficie: per ora **una finestra trasparente a tutto schermo** nello
strato overlay (quella che aveva il menu dell'area di notifica), su cui i
menu e i sottomenu sono pannelli; un clic fuori li chiude. Funziona uguale
da qualunque parte nasca il menu (taskbar, Start, sfondo, compositor) e su
qualunque schermo. Il popup xdg con *grab* resta l'alternativa se servirà
(per esempio per non coprire lo schermo col trasparente): da rivalutare
con la sfocatura (S5).

Due dettagli che Wayland rende necessari:

- **La tastiera torna al pannello.** Il menu prende la tastiera; quando si
  chiude, il compositor la ridà alla superficie della shell che l'aveva
  prima (es. il menu Start), non alla finestra attiva.
- **Maiusc lo sa il compositor.** Cliccando la taskbar la tastiera è di
  un'altra app, e la shell non vede i modificatori: per Maiusc+clic destro
  li chiede al compositor (`modifiers` sul suo socket, con risposta).

### 14.2 Aspetto

Come i menu di Windows 11 (WinUI), in tema scuro:

- angoli arrotondati (8), bordo sottile, ombra; sfondo acrilico quando ci
  sarà la sfocatura (S5), fino ad allora il colore `popup` del tema;
- ogni riga: icona 16×16 a sinistra (colonna vuota se nessuna voce del
  gruppo ha icona), testo nel font di KDE, scorciatoia a destra in grigio,
  freccia `›` per i sottomenu; spunte e pallini per le voci a scelta;
- evidenziazione: rettangolo arrotondato (4) staccato dai bordi del menu;
  voci disattivate in grigio;
- separatori sottili a tutta larghezza tra i gruppi;
- nei menu dei file (§14.9) una **riga di icone** in cima o in fondo (vicino
  al punto del clic): Taglia, Copia, Rinomina, Condividi, Elimina;
- nitidi a ogni scala come tutto il resto (§3).

### 14.3 Comportamento

- **Apertura**: al punto del clic, con l'angolo in alto a sinistra sotto il
  cursore; se non c'è spazio si ribalta a sinistra o verso l'alto. Dalla
  taskbar sale verso l'alto, sopra il pulsante. Col tasto Menu o
  Maiusc+F10 si apre sull'elemento che ha il fuoco.
- **Animazione**: entra con una dissolvenza e un breve scorrimento dal lato
  da cui si apre, ai frame reali dello schermo (§4.2); esce subito.
- **Sottomenu**: si aprono al passaggio del mouse dopo un breve ritardo
  (400 ms, come Windows), al clic o con la freccia destra; restano aperti
  se il mouse ci va in diagonale attraversando altre voci.
- **Tastiera**: frecce, Home/Fine, Invio o Spazio per eseguire, Esc chiude
  un livello, freccia sinistra torna al menu padre; lettera sottolineata
  (se aperto da tastiera) o iniziale per saltare alla voce.
- **Mouse**: il tasto destro su una voce la esegue come il sinistro; un
  clic fuori chiude il menu.
- **Tocco**: una pressione lunga vale come tasto destro.

### 14.4 Taskbar: pulsante di un'app (jump list)

Il menu che sale dal pulsante, dall'alto in basso:

- **Aggiunti**: file fissati per quell'app (con la puntina al passaggio del
  mouse per toglierli);
- **Recenti**: gli ultimi file aperti con quell'app (puntina per fissarli;
  tasto destro sul file: Apri, Aggiungi a questo elenco / Rimuovi da questo
  elenco, Rimuovi dall'elenco);
- **Attività**: le azioni dichiarate dall'app (es. Firefox: "Nuova
  finestra", "Nuova finestra anonima");
- ---
- nome dell'app con la sua icona: apre un'altra istanza;
- "Aggiungi alla barra delle applicazioni" / "Rimuovi dalla barra delle
  applicazioni";
- "Chiudi finestra", o "Chiudi tutte le finestre" se sono più d'una;
- "Termina attività": chiude il processo senza chiedere. In Windows 11
  24H2 va attivata nelle impostazioni; in Vela è accesa di default
  (scelta dell'utente), e si spegne con `endTask=false`.

Maiusc+clic destro sul pulsante apre invece il menu della finestra (§14.8).

### 14.5 Taskbar: spazio vuoto, orologio, icone

- **Spazio vuoto**: "Gestione attività", "Impostazioni della barra delle
  applicazioni".
- **Data e ora**: "Regola data e ora", "Impostazioni di notifica".
- **Icone di sistema** (fatte con le impostazioni rapide): volume →
  "Apri mixer volume", "Impostazioni audio" ("Risolvi i problemi audio"
  non ha un equivalente); rete → "Diagnostica problemi di rete" (spenta),
  "Impostazioni di rete e Internet"; batteria → "Opzioni risparmio energia
  e sospensione" (da fare insieme alla batteria nella taskbar dei
  portatili).
- **Icone delle app nell'area di notifica**: il menu lo decide l'app
  (dbusmenu, già fatto), disegnato con `VelaMenu`.

### 14.6 Pulsante Start (Win+X)

Clic destro sul pulsante Start o Win+X, con i gruppi di Windows 11:

- "App installate", "Centro PC portatile" (solo portatili), "Opzioni
  risparmio energia", "Visualizzatore eventi", "Sistema", "Gestione
  dispositivi", "Connessioni di rete", "Gestione disco", "Gestione
  computer"
- ---
- "Terminale", "Terminale (Admin)"
- ---
- "Gestione attività", "Impostazioni", "Esplora file", "Cerca", "Esegui"
- ---
- "Arresta il sistema o disconnetti" › "Disconnetti", "Sospendi",
  "Arresta il sistema", "Riavvia il sistema"
- "Desktop"

Win+X seguito dalla lettera sottolineata apre direttamente la voce.

### 14.7 Menu Start

- **App aggiunte**: le voci della jump list (§14.4, recenti e attività),
  poi "Rimuovi da Start", "Sposta all'inizio", "Aggiungi alla barra delle
  applicazioni" / "Rimuovi dalla barra delle applicazioni", "Esegui come
  amministratore", "Apri percorso file", "Disinstalla". Sulle cartelle di
  app aggiunte: "Rinomina", "Rimuovi da Start".
- **Tutte le app** e **risultati della ricerca**: "Aggiungi a Start",
  "Altro" › ("Aggiungi alla barra delle applicazioni", "Esegui come
  amministratore", "Apri percorso file", "Impostazioni app"),
  "Disinstalla".
- **Consigliati**: "Apri percorso file", "Rimuovi dall'elenco".
- **Casella di ricerca** (e ogni campo di testo della shell): "Annulla",
  "Taglia", "Copia", "Incolla", "Seleziona tutto".

### 14.8 Il menu della finestra

Clic destro sulla barra del titolo (o sulla sua icona), Alt+Spazio, o
Maiusc+clic destro sul pulsante della taskbar:

- "Ripristina", "Sposta", "Ridimensiona", "Riduci a icona", "Ingrandisci"
- ---
- "Chiudi" (Alt+F4)

Le voci che non valgono sono disattivate (es. "Ripristina" su una finestra
non ingrandita). "Sposta" e "Ridimensiona" funzionano con le frecce e
Invio, come in Windows. Il doppio clic sull'icona della barra chiude la
finestra.

Lo chiede il compositor: per la barra di Vela (§9), per Alt+Spazio, e per
le app con la barra propria che mandano `xdg_toplevel.show_window_menu`
(GTK/libadwaita lo fanno col clic destro sulla loro barra). Il compositor
passa alla shell la finestra e il punto (socket della shell), la shell
disegna il menu; ciò che non sa fare da sé attraverso foreign-toplevel
(Sposta e Ridimensiona da tastiera) lo rimanda al compositor sul socket dei
comandi.

### 14.9 Desktop

Le icone sono i file della cartella Desktop (`XDG_DESKTOP_DIR`) più il
Cestino, sullo schermo principale (`DesktopModel` e `Wallpaper.qml`).
Trascinarle è un trascinamento vero tra app (wl_data_device, gestito dal
compositor con la sua icona): finisce in un'altra app, su una cartella,
sul Cestino o di nuovo sul desktop, dove le icone si spostano. Il
compositor tiene anche la **presa implicita** di Wayland: finché un tasto
è premuto, il puntatore resta alla superficie su cui è stato premuto,
anche su un altro schermo (senza, un riquadro di selezione che esce dallo
schermo non riceveva mai il rilascio).

- **Sfondo**: "Visualizza" › ("Icone grandi", "Icone medie", "Icone
  piccole", "Disponi icone automaticamente", "Allinea icone alla griglia",
  "Mostra icone del desktop"); "Ordina per" › ("Nome", "Dimensione", "Tipo
  elemento", "Data ultima modifica"); "Aggiorna"; "Annulla …" (l'ultima
  azione, Ctrl+Z); "Nuovo" › ("Cartella", "Collegamento", poi i tipi di
  documento); "Impostazioni schermo"; "Personalizza"; "Apri in Terminale";
  "Mostra altre opzioni".
- **Icone (file e cartelle)**: riga di icone (Taglia, Copia, Rinomina,
  Condividi, Elimina), poi "Apri", "Apri con" ›, "Aggiungi a Start",
  "Aggiungi a Preferiti", "Comprimi in" › ("File ZIP", "File 7z", "File
  TAR"), "Copia come percorso", "Apri in Terminale" (cartelle),
  "Proprietà", "Mostra altre opzioni".
- **"Mostra altre opzioni"** (anche Maiusc+F10 o Maiusc+clic destro): il
  menu completo, con tutte le voci aggiunte dalle app. Su Linux sono i
  *service menu* di KDE (e le azioni che le app dichiarano per i tipi di
  file); nel menu moderno compaiono solo quelli che lo chiedono, come le
  app registrate in Windows 11.

Lo stesso menu dei file lo userà Esplora (milestone 3).

### 14.10 Visualizzazione attività e notifiche

Con i desktop virtuali (milestone 3, fatti: `shell/qml/TaskView.qml`,
`compositor/src/workspaces.cpp`):

- **Anteprima di una finestra**: "Aggancia a sinistra", "Aggancia a
  destra", "Sposta in" › (i desktop, "Nuovo desktop"), "Mostra questa
  finestra su tutti i desktop", "Mostra le finestre di questa app su tutti
  i desktop", "Chiudi".
- **Anteprima di un desktop**: "Rinomina", "Scegli sfondo", "Sposta a
  sinistra", "Sposta a destra", "Chiudi".

Notifiche (menu "…" del popup e del centro notifiche): "Disattiva tutte le
notifiche per <app>", "Vai alle impostazioni di notifica".

### 14.11 Da Windows a Linux

| Voce di Windows | In Vela |
|---|---|
| Impostazioni e le sue pagine | l'app Impostazioni di Vela (`vela-settings`), aperta sulla pagina giusta |
| App installate, Disinstalla | Impostazioni > App installate; la disinstallazione via Flatpak, Discover o il gestore dei pacchetti |
| Centro PC portatile, Opzioni risparmio energia | Impostazioni > Alimentazione |
| Visualizzatore eventi | il visualizzatore del journal di systemd |
| Sistema | Impostazioni > Informazioni |
| Gestione dispositivi | le informazioni sul sistema (kinfocenter) |
| Connessioni di rete | Impostazioni > Rete e Internet (NetworkManager) |
| Gestione disco, Gestione computer | il gestore delle partizioni; "Gestione computer" apre le informazioni sul sistema |
| Terminale / Terminale (Admin) | il terminale predefinito / lo stesso con una shell di root chiesta a polkit |
| Esegui come amministratore | solo per le app che lo supportano (polkit); le app grafiche come root sotto Wayland di norma non partono, quindi altrove la voce non c'è |
| Gestione attività | il monitor di sistema (poi quello di Vela) |
| Esegui | una casella di comando piccola, come Win+R |
| Aggiunti, Recenti (jump list) | file fissati salvati da Vela; recenti da `recently-used.xbel`, che registra quale app ha aperto ogni file |
| Attività (jump list) | le azioni `[Desktop Action …]` del file `.desktop` dell'app |
| Condividi | il portale di condivisione, quando ci sarà; fino ad allora la voce non c'è |
| Mostra altre opzioni | il menu completo con i service menu di KDE |

# Il renderer di Vela

Progetto della scena e del renderer propri di Vela, nativi Vulkan. È il
fondamento della milestone 2 (sfocatura, angoli arrotondati, ombre) e di
tutto ciò che verrà dopo. Questo documento viene prima del codice: le
decisioni si prendono qui, il codice le segue.

> **Stato:** progetto discusso e approvato nelle scelte di fondo (§2,
> riassunte anche in §13). Prossimo passo: la tappa S0 (§11).

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
| API grafica | **solo Vulkan** | nessun ripiego GLES/pixman; requisito minimo esplicito (§7.1) |
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

La posizione **fisica** di ogni finestra (origine della superficie
principale) è intera. Le animazioni calcolano posizioni logiche frazionarie,
ma a ogni frame l'origine viene agganciata al pixel fisico più vicino:
niente testo che "trema" di mezzo pixel durante un movimento. Solo le
trasformazioni di scala (apertura, riduzione a icona) ricampionano, e per
la loro breve durata.

### 3.5 Dimensioni logiche scelte bene

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

### 4.3 Disegnare tardi (late latching)

Il renderer misura quanto tempo CPU+GPU serve per un frame (percentile
alto delle misure recenti) e comincia a disegnare **quel tempo più un
margine prima del vblank**, non subito dopo il vblank precedente. Input e
animazioni campionati più tardi = latenza minore. Se la stima sbaglia e si
perde un vblank, il margine cresce da solo.

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

| Compito | Come |
|---|---|
| Import dei buffer | shm → upload in una `VkImage`; dmabuf → import diretto (§7.3); cache per `wlr_buffer`, invalidata al rilascio |
| Damage | danni della superficie (in coordinate buffer) → logico → fisico per schermo; + danni dei nostri nodi; storia per l'età dei buffer della swapchain |
| Frame callback, presentation | §4.5 |
| Output enter/leave | ricalcolati quando una finestra si sposta o cambia dimensione |
| Scala preferita | §3.3, §3.6 |
| dmabuf feedback | per superficie: tranche di scanout quando la finestra è a schermo intero |
| Direct scanout | una sola superficie opaca che copre lo schermo, 1:1, senza effetti sopra → il suo buffer va direttamente sul piano primario |
| Catture | sorgenti `ext-image-capture` e screencopy disegnate **dal nostro renderer**, effetti compresi |
| Istantanee | già nostre (snapshot.cpp): buffer bloccati disegnati come nodi |

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

**Vulkan 1.3** (dynamic rendering, synchronization2, timeline semaphore
nel core) più le estensioni per scambiare buffer con kernel e client:

- `VK_EXT_image_drm_format_modifier`, `VK_EXT_external_memory_dma_buf`,
  `VK_KHR_external_memory_fd`, `VK_EXT_queue_family_foreign`
- `VK_KHR_external_semaphore_fd` (sync_file, per la sincronizzazione
  implicita ed esplicita)
- `VK_EXT_physical_device_drm` (scegliere la GPU che pilota lo schermo)

In pratica: AMD GCN e successive (RADV), Intel Gen9/Skylake e successive
(ANV), NVIDIA con driver proprietario recente o NVK. Chi non ha Vulkan 1.3
non può usare Vela: va scritto chiaramente nel README e detto all'avvio con
un messaggio comprensibile, non con un crash.

### 7.2 Un device nostro

Vela crea il proprio `VkInstance`/`VkDevice` sulla GPU dello schermo
(`VK_EXT_physical_device_drm` confrontato con il device DRM del backend).
Non usiamo il renderer di wlroots per disegnare. Per allocare i buffer degli
schermi scriviamo un nostro `wlr_allocator` (`wlr_allocator_init` è
pubblica): buffer esportati dal nostro device come dmabuf con un modifier
accettato dal piano primario (`wlr_output_get_primary_formats`).

### 7.3 Buffer dei client e sincronizzazione

- dmabuf: import con il modifier del client, cache per buffer; transizione
  di proprietà dalla coda "foreign" a ogni uso.
- Sincronizzazione implicita: sync_file estratto dal dmabuf
  (`DMA_BUF_IOCTL_EXPORT_SYNC_FILE`) e atteso come semaforo; al termine,
  sync_file reinserito nel dmabuf.
- Sincronizzazione esplicita (`linux-drm-syncobj-v1`, già in wlroots 0.20):
  punti di attesa e rilascio dei timeline del client.
- Verso lo schermo: fence di fine rendering passata al commit.

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

## 8. Effetti

### 8.1 Angoli arrotondati

Ritaglio con la distanza da un rettangolo arrotondato (SDF) calcolata nello
shader, in **pixel fisici**, con antialiasing di esattamente un pixel
fisico. Nitido a ogni scala. Si applica al riquadro della finestra (non ai
margini d'ombra disegnati dall'app).

### 8.2 Ombre

Ombra analitica di un rettangolo arrotondato (formula chiusa
dell'integrale di una gaussiana, tecnica di Evan Wallace): un quad, nessuna
texture, nessuna sfocatura. Due strati come Windows 11 (ombra ampia e
morbida + ombra di contatto stretta), più marcate per la finestra attiva.
Le app con decorazioni proprie disegnano già la loro ombra: se la geometria
xdg indica margini d'ombra, la nostra non si disegna.

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
- **Anche a batteria, tutto al massimo**: nessuna riduzione automatica
  degli effetti. Il risparmio viene dal non disegnare ciò che non cambia,
  non dal togliere gli effetti.

## 9. La barra del titolo di Vela

Oggi le app Qt/KDE disegnano da sé la barra "di ripiego" di Qt, perché
Vela non offre decorazioni lato server. La barra di Vela è il primo
elemento "nostro" che unisce tutto ciò che il renderer sa fare: testo,
sfocatura, angoli, ombre, animazioni.

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
  massimizzare, clic destro per il menu della finestra; più avanti, col
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
  60/144/240/360 Hz (il backend headless accetta una modalità con
  frequenza: da verificare in S0 che il ritmo dei frame la rispetti), più
  schermi con scale diverse.

## 11. Tappe

Il compositor attuale (con `wlr_scene`) resta funzionante finché la nuova
scena non lo eguaglia; poi `wlr_scene` sparisce. Ogni tappa si chiude solo
con i suoi test.

| Tappa | Contenuto | Fatto quando |
|---|---|---|
| **S0** Fondamenta | device Vulkan nostro, allocatore, import dei buffer degli schermi, shader compilati, un colore a tutto schermo; ciclo di frame per schermo con tempo di presentazione previsto | schermo colorato a 60/144/240 Hz simulati, validation layer senza errori |
| **S1** Parità | scena propria con finestre, layer, popup, sottosuperfici; shm e dmabuf; damage; frame callback, presentation, enter/leave; istantanee, snap, catture portate sul nuovo renderer | tutto ciò che c'è oggi funziona, `wlr_scene` rimosso |
| **S2** Nitidezza | fractional scale, aggancio ai pixel, filtri di qualità, più schermi con scale diverse, cursore per scala, scala predefinita dai DPI | test bit per bit verdi a ogni scala |
| **S3** Tempo e latenza | late latching, scanout diretto, sincronizzazione esplicita, dmabuf feedback | latenza misurata, nessun frame perso a 360 Hz simulati |
| **S4** Forma | angoli arrotondati, ombre | nitidi a ogni scala |
| **S5** Sfocatura | `ext-background-effect`, dual Kawase, cache, acrylic per la shell | taskbar e menu sfocati; costo zero quando dietro non cambia nulla |
| **S6** Barra del titolo | `xdg-decoration`, motore di testo con il font di KDE, pulsanti, icone SVG, tinta dallo sfondo, interazioni | le app Qt/KDE con la barra di Vela, nitida a ogni scala |
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
- **Cursore hardware**: `wlr_output_cursor` usa il renderer di wlroots per
  preparare il buffer del piano cursore. Da verificare in S0 se accetta un
  buffer già pronto; altrimenti gestiamo noi il piano cursore.
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

Nuove domande emergeranno scrivendo il codice: si aggiungono qui.

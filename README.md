# Vela

Un desktop environment per Linux moderno, fluido e leggero, con l'aspetto
e la comodità di Windows 11. L'obiettivo è sostituire KDE Plasma un pezzo
alla volta.

> **Stato: milestone 3 completata.** Vela si sceglie all'accesso come una
> sessione vera e copre il desktop di tutti i giorni: compositor con il suo
> renderer Vulkan, shell, Esplora e Impostazioni. Ogni funzione nuova è
> provata in automatico in una sessione senza schermo (vedi «Provarlo senza
> guardarlo»); ciò che dipende dall'hardware (gamma del monitor, tearing,
> touchpad) va provato su una macchina vera.

## Com'è fatto

```
┌──────────────────────── vela-shell (Qt Quick, C++/QML) ────────────────────────┐
│  taskbar · menu Start · sfondo · notifiche · impostazioni rapide · menu       │
└────────────────────────────── protocollo layer-shell ──────────────────────────┘
     vela-settings, vela-files (Qt Quick): Impostazioni ed Esplora, finestre normali
┌──────────────────────── vela-compositor (C++20, wlroots) ──────────────────────┐
│  schermi · input · finestre · animazioni legate ai frame · focus               │
└──────────────────────────────── DRM/KMS · libinput ────────────────────────────┘
```

- **Compositor** (`compositor/`): C++20 su **wlroots 0.20**. Disegna tutto con
  una scena e un renderer **Vulkan 1.4** tutti suoi
  ([docs/renderer.md](docs/renderer.md)); gestisce schermi, input e finestre.
  Le animazioni avanzano a ogni frame reale dello schermo, quindi a 180 Hz
  fanno 180 passi al secondo.
- **Shell** (`shell/`): **Qt Quick** (scena grafica su GPU) con
  **LayerShellQt**. Ogni pezzo della shell è un'app separata dal compositor:
  se la shell va in crash, le tue finestre restano aperte.
- I due parlano con i protocolli Wayland standard, più un piccolo socket per
  i comandi (es. il tasto Super apre il menu Start).
- Motion design in un posto solo: `compositor/src/motion.hpp` e
  `shell/qml/Theme.qml` usano la stessa curva, cubic-bezier(0, 0, 0.2, 1).

### Cosa c'è già

**Compositor**
- Sceglie da solo la **frequenza più alta** del monitor alla risoluzione
  nativa. Molti monitor a 180 Hz dichiarano 60 Hz come "preferita".
- Finestre che si aprono **sfumando e salendo di qualche pixel** (250 ms)
  e si chiudono sfumando e ritraendosi un poco (180 ms).
- Riduzione a icona animata: la finestra vola verso il suo pulsante nella
  taskbar rimpicciolendo, e da lì ritorna quando la ripristini.
- Spostamento e ridimensionamento. Trascinando una finestra massimizzata
  questa si ripristina sotto il cursore, come su Windows.
- Massimizza, riduci a icona e schermo intero. Le finestre a schermo intero
  stanno sopra la taskbar e beneficiano del direct scanout. Alt+Tab
  ripristina anche le finestre ridotte a icona, come su Windows.
- Pubblica l'elenco delle finestre aperte (protocollo foreign-toplevel):
  lo usa la taskbar, ma funziona anche con strumenti esterni.
- Snap a metà e quarti con Super+frecce o trascinando a bordi e angoli,
  con anteprima animata; layout di snap e Snap Assist (vedi sotto). Le
  finestre sistemate insieme con Snap Assist fanno un **gruppo di snap**:
  la taskbar le mostra insieme e le riporta davanti insieme; ci si stacca
  spostando, massimizzando o mandando la finestra su un altro desktop.
- Menu e popup tenuti dentro lo schermo.
- Alt+Tab come su Windows: tenendo premuto Alt compare il pannello con le
  anteprime delle finestre dalla più recente (anche quelle ridotte a icona
  o coperte) del desktop in uso; un Alt+Tab veloce cambia finestra senza
  mostrarlo. Un clic su un'anteprima ci porta subito.
- Cattura di schermi e finestre con i protocolli standard ext-image-copy-capture
  (le anteprime di Alt+Tab e della condivisione dello schermo).
- Bordi invisibili per ridimensionare le finestre con la barra di Vela, come
  in Windows 11: 8 pixel attorno alla finestra e il bordo alto della barra.
- **Angoli arrotondati e ombre** come Windows 11 (raggio 8, ombra ampia più
  ombra di contatto, più marcata per la finestra attiva), nitidi a ogni
  scala; niente angoli da massimizzata, agganciata o a schermo intero.
- **Sfocatura acrylic** dietro i pannelli della shell (taskbar, menu Start,
  menu, Alt+Tab, notifiche) con il protocollo standard
  `ext-background-effect-v1`, implementato da Vela.
- **Barra del titolo di Vela** anche per le app Wayland che la accettano
  (`xdg-decoration`: Qt e KDE), con l'icona dell'app e la tinta Mica presa
  dallo sfondo del desktop; un clic sull'icona apre il menu della finestra.
- Buffer GPU condivisi con le app (linux-dmabuf) e cursori per nome
  (cursor-shape), oltre ai protocolli che le app moderne si aspettano:
  scala frazionaria, viewporter, appunti, screencopy, presentation time.
- Il tasto Super premuto da solo apre il menu Start.
- **Luce notturna** come Windows: colori più caldi con l'intensità scelta,
  accesa a mano o pianificata (dalle-alle, o dal tramonto all'alba con le
  ore del sole calcolate dalle coordinate del fuso orario, senza rete),
  con un passaggio graduale di un secondo. Sui monitor veri va nella gamma
  del monitor: niente tinta negli screenshot e nella condivisione dello
  schermo, e lo scanout diretto dei giochi resta; dove il monitor non la
  accetta (annidato, headless) la applica il renderer.
- **Accessibilità**: filtri colore (scala di grigi e correzioni per i
  daltonismi, anche con Win+Ctrl+C), **lente di ingrandimento** (Win+più
  ingrandisce lo schermo del cursore attorno a lui, Win+meno riduce,
  Win+Esc chiude; la zona ingrandita segue il cursore ai bordi),
  **tasti permanenti** (Maiusc, Ctrl, Alt e Win premuti e lasciati valgono
  per il tasto dopo; premuti due volte restano bloccati).
- **Tearing per i giochi** (wp-tearing-control): un gioco a schermo intero
  che lo chiede mostra ogni fotogramma appena è pronto, senza aspettare il
  vblank (si spegne nelle Impostazioni o con `VELA_TEARING=0`).
- **Frequenza di aggiornamento variabile** (VRR, FreeSync, G-Sync): di
  solito solo con un'app a schermo intero, come l'"Automatico" di KWin
  (con alcuni monitor il desktop sfarfalla), oppure sempre o mai. Con VRR e
  un gioco in scanout diretto, ogni suo fotogramma va sullo schermo appena
  arriva. `VELA_DEBUG_SCANOUT=1` scrive nel log perché un'app a schermo
  intero non va in scanout diretto (pannelli sopra, alfa, scala...).
- **Più schermi**, ognuno con la sua scala: collegandone e scollegandone
  uno a caldo, le finestre dello schermo che sparisce passano su quello
  più vicino (massimizzate, agganciate e a schermo intero si risistemano) e
  tornano al loro posto quando lo schermo torna. I desktop virtuali
  valgono per tutti gli schermi insieme, come in Windows.
- Barre del titolo chiare o scure secondo la modalità delle app, e tinta
  acrylic chiara o scura secondo quella della shell.
- Mouse e touchpad come su Windows, regolati dalle Impostazioni: tocco per
  cliccare (due dita: tasto destro), trascinamento col tocco, niente tocchi
  accidentali mentre scrivi, scorrimento naturale, touchpad spento con un
  mouse collegato se lo si vuole; gesti a tre e quattro dita.
- Se la shell va in crash il compositor la rilancia (ma si arrende se
  continua a chiudersi appena avviata).
- **App X11** con Xwayland (Steam, molti giochi, app vecchie): parte al
  primo client X11; le finestre X11 hanno animazioni, snap, taskbar e
  Alt+Tab come le altre, i loro menu compaiono dove li mette l'app.
- **Giochi**: movimenti relativi del mouse (girare la visuale), puntatore
  bloccato o confinato nella finestra, app che tengono per sé le
  scorciatoie (macchine virtuali, desktop remoto).
- **Barra del titolo di Vela** per le app X11 che non ne hanno una propria
  (prima versione della barra di [docs/renderer.md](docs/renderer.md) §9):
  titolo nel font di KDE, riduci/massimizza/chiudi, trascinamento e doppio
  clic, nitida alla scala dello schermo. Le finestre non sono mai più grandi
  dello schermo e restano dentro di lui anche quando si spostano da sole.
- **Super + trascinamento** sposta qualunque finestra, **Super + tasto
  destro** la ridimensiona.
- **Blocco dello schermo** sicuro (ext-session-lock-v1) con `vela-lock`,
  in stile Windows 11: ora e data, poi utente e password (PAM). Se il
  programma di blocco va in crash lo schermo resta nero e bloccato, e
  viene rilanciato. Si blocca con Win+L, prima di sospendere e dopo 10
  minuti di inattività; pochi secondi dopo gli schermi si spengono.
  Un'app che mostra un video può impedirlo (idle-inhibit).
- Tastiera presa dal sistema (impostazioni di KDE o `localectl`),
  disposizione degli schermi ricordata e modificabile da programmi esterni
  (wlr-output-management, per esempio `wlr-randr`), cambio di console con
  Ctrl+Alt+F1…F12.

**Shell**
- Sfondo del desktop (`images/vela_splash_169.svg`, incluso nell'eseguibile)
  su ogni schermo, disegnato già alla dimensione esatta dello schermo: in
  memoria c'è solo ciò che si vede. Un altro sfondo con `VELA_WALLPAPER`.
- **Icone del desktop** sullo schermo principale, come in Windows 11: i file
  della cartella Desktop (`XDG_DESKTOP_DIR`, per esempio `~/Scrivania`) e il
  Cestino, in colonne dall'alto a sinistra. Si aggiornano da sole quando la
  cartella cambia. Le immagini hanno la miniatura, presa dalla cache
  condivisa di freedesktop (`~/.cache/thumbnails`, la stessa di Dolphin:
  anche video e PDF, se un'altra app l'ha già fatta) o fatta e salvata lì.
  - **Proprietà** (Alt+Invio): nome, tipo, "Apri con" con Cambia,
    percorso, dimensioni (anche su disco e delle cartelle), date, sola
    lettura; le autorizzazioni (lettura, scrittura, esecuzione per
    proprietario, gruppo e altri); i dettagli (per le immagini i pixel).
  - Selezione con clic, Ctrl+clic, Maiusc+clic, riquadro, Ctrl+A, frecce;
    doppio clic o Invio apre; trascinate restano dove le lasci (sulla
    griglia, se "Allinea icone alla griglia").
  - Rinomina sul posto (F2), Canc sposta nel Cestino, Ctrl+C/X/V, Ctrl+Z
    annulla (rinomina, eliminazione, nuovo, incolla), F5 aggiorna,
    Ctrl+Maiusc+2/3/4 per la dimensione.
  - Il menu del desktop: Visualizza (grandi, medie, piccole, disposizione
    automatica, griglia, mostra icone), Ordina per, Aggiorna, Annulla,
    Incolla, Nuovo (cartella, documento di testo e i modelli di
    `XDG_TEMPLATES_DIR`); quello dei file: la riga di icone (Taglia, Copia,
    Rinomina, Elimina), Apri, Apri con, Apri in Terminale, Aggiungi a
    Start, Comprimi in ZIP, 7z o TAR, Copia come percorso; il Cestino si
    apre e si svuota (con conferma).
  - **Mostra altre opzioni** (anche Maiusc+clic destro e Maiusc+F10): il
    menu completo, con le voci che le app aggiungono ai file (i service
    menu di KDE, per esempio Filelight o "Esegui in Konsole").
  - **Trascinare file** dal desktop alle app (un documento in Kate, un
    allegato in un'email) e dalle app al desktop, su una cartella o sul
    Cestino: spostati se sono sullo stesso disco, altrimenti copiati, come
    in Windows.
- Taskbar in stile Windows 11: pulsante Start, app fissate e app aperte al
  centro, orologio a destra, effetti di hover e pressione animati.
  - Le finestre della stessa app stanno sotto un solo pulsante; sotto
    l'icona un trattino grigio se l'app è aperta, blu se è quella attiva.
  - Clic: avvia l'app, oppure porta davanti la sua finestra, oppure (se è
    già attiva) la riduce a icona. Con più finestre, il clic passa alla
    successiva. Clic centrale: una nuova finestra.
  - I pulsanti delle app che si aprono entrano con un'animazione e la fila
    scivola al nuovo centro.
- Menu Start con ricerca istantanea tra le app installate (legge i normali
  file `.desktop`, con i nomi in italiano), navigazione da tastiera e
  animazioni di apertura e chiusura: il pannello sale da dietro la taskbar.
  In cima le app **aggiunte** alla Start, poi tutte le app.
- Si chiude cliccando fuori o con Esc.
- Pulsante di accensione nel menu Start: Sospendi, Esci (chiude Vela),
  Riavvia, Arresta (tramite systemd-logind).
- **Notifiche** delle app (org.freedesktop.Notifications) in basso a destra,
  come su Windows 11: icona, testo con grassetto e link, pulsanti delle
  azioni; spariscono dopo 6 secondi (non con il mouse sopra), quelle
  critiche restano.
- **Area di notifica** accanto all'orologio (StatusNotifierItem): le icone
  di Telegram, Discord, Steam e simili, con clic, clic centrale, rotellina
  e i loro menu (anche sottomenu e voci con la spunta).
- **Menu del tasto destro** copiati da Windows 11, tutti con lo stesso
  componente (stile, sottomenu, tastiera: [docs/renderer.md §14](docs/renderer.md)):
  - pulsanti della taskbar: la **jump list**, con i file recenti dell'app
    (dal registro condiviso `recently-used.xbel`), i file fissati con la
    puntina, le azioni dell'app (es. Firefox: "Nuova finestra anonima"),
    aggiungi o togli dalla taskbar, "Termina attività" (chiude subito il
    processo; si spegne con `endTask=false` nel gruppo `[taskbar]` di
    `~/.config/Vela/vela-shell.conf`), chiudi le finestre; Maiusc+clic
    destro: il menu della finestra;
  - pulsante Start e **Win+X**: App installate, Sistema, Terminale (Admin),
    Gestione attività, Esegui, Arresta il sistema o disconnetti…, ognuna
    affidata al programma Linux che fa la stessa cosa (le voci senza
    programma installato sono spente);
  - spazio vuoto della taskbar, orologio, area di notifica;
  - app del menu Start: file recenti e azioni, Aggiungi/Rimuovi da Start,
    Sposta all'inizio, Aggiungi alla taskbar, Apri percorso file,
    Disinstalla (Flatpak, oppure il gestore dei pacchetti in un terminale);
  - **menu della finestra** (clic destro sulla barra del titolo, Alt+Spazio,
    app GTK con la barra propria): Ripristina, Sposta e Ridimensiona anche da
    tastiera come in Windows, Riduci a icona, Ingrandisci, Chiudi;
  - desktop e file (vedi le icone del desktop sopra) e campi di testo.
- **Impostazioni rapide** (Win+A, o clic sulle icone di sistema accanto
  all'orologio), come Windows 11: Wi-Fi (NetworkManager, con la freccia
  che elenca le reti per connettersi, con la password, o disconnettersi),
  Bluetooth (BlueZ), modalità aereo, risparmio energia
  (power-profiles-daemon), Luce notturna, Accessibilità (lente, filtri
  colore, tasti permanenti), luminosità dei portatili (logind), volume
  (PipeWire), batteria (UPower) e Impostazioni. Le icone di sistema seguono
  lo stato; la rotellina sul volume lo cambia, il tasto destro apre i loro
  menu.
- **Cronologia degli Appunti** (Win+V), come Windows 11: testo e immagini
  copiati, i fissati che restano dopo il riavvio; un clic (o Invio)
  incolla nell'app a fuoco, "…" fissa o elimina. Le password dei gestori di
  password non si ricordano. Si accende dal pannello stesso o da
  Impostazioni > Sistema > Appunti.
- **Strumento di cattura** (Win+Maiusc+S o Stamp): lo schermo si ferma e si
  sceglie un rettangolo, una finestra o uno schermo intero; il ritaglio va
  negli Appunti e in Immagini > Screenshot, e una notifica lo apre.
- **Gesti del touchpad** come Windows: tre o quattro dita verso l'alto la
  Visualizzazione attività, verso il basso il desktop, di lato si cambia
  app (Alt+Tab che segue le dita) o desktop virtuale; il pizzico e gli
  altri gesti vanno alle app.
- **Una taskbar per schermo**, come Windows 11: Start, app e orologio su
  ognuno, area di notifica e icone di sistema sul principale; Start, menu,
  impostazioni rapide, centro notifiche e anteprime si aprono sullo schermo
  della taskbar da cui si cliccano (da tastiera su quello principale). Si
  può tenere solo sul principale (Impostazioni > Barra delle applicazioni).
- I pulsanti della taskbar si **riordinano trascinandoli** (le app fissate
  restano davanti a quelle solo aperte, e l'ordine si ricorda).
- **Anteprime della taskbar**: col mouse fermo sul pulsante di un'app
  aperta compaiono le sue finestre in miniatura (clic per andarci, clic
  centrale o ✕ per chiuderle), e i suoi gruppi di snap con le finestre nei
  loro posti.
- **Tema chiaro e scuro** come Windows 11 ("Scegli la modalità": Chiaro,
  Scuro o Personalizzato, una modalità per Vela e una per le app): la shell,
  le sue icone, Esplora, le Impostazioni e le barre del titolo cambiano
  subito; alle app KDE e GTK passano colori e icone.
- **Condividi** (desktop ed Esplora): ai telefoni e computer associati con
  KDE Connect, per posta, via Bluetooth. **Scegli un'altra app** con "Solo
  una volta" e "Sempre". **Collegamenti**: Nuovo > Collegamento (a file,
  cartelle o indirizzi web), Crea collegamento, Incolla collegamento.
  **Aggiungi a Preferiti** (la sezione Preferiti della Home di Esplora).
- **Centro notifiche e calendario** (Win+N, o clic sull'orologio): le
  notifiche passate raggruppate per app (quelle scadute dal popup ci
  restano, ancora vive: un clic esegue l'azione), "Non disturbare",
  "Cancella tutto", e il calendario del mese; accanto all'orologio il
  numero delle notifiche da leggere.
- **Esegui** (Win+R), in basso a sinistra come in Windows, con i comandi
  già usati; **Win+D** riduce tutto a icona e la volta dopo rimette com'era.
- **Desktop virtuali e Visualizzazione attività** (Win+Tab, o il pulsante
  accanto a Start), come Windows 11: le anteprime delle finestre del desktop
  in uso, i desktop in basso (passandoci sopra se ne vedono le finestre) e
  "Nuovo desktop"; una finestra trascinata su un desktop ci si sposta. Tasto
  destro su una finestra: Aggancia a sinistra/destra, Sposta in, Mostra su
  tutti i desktop (la finestra o tutte quelle dell'app), Chiudi; su un
  desktop: Rinomina, Scegli sfondo, Sposta a sinistra/destra, Chiudi.
  Win+Ctrl+←/→ passa da un desktop all'altro (scorrendo, col nome al centro
  dello schermo), Win+Ctrl+D ne crea uno, Win+Ctrl+F4 chiude quello in uso
  (le finestre passano al desktop accanto). La taskbar e Alt+Tab mostrano
  le finestre del desktop in uso; desktop e nomi restano tra una sessione e
  l'altra (`~/.config/vela/desktop.conf`).
- **Snap come Windows 11**: trascinando una finestra contro un lato si
  aggancia a metà, contro un angolo a un quarto, in alto si massimizza;
  Win+frecce passa da metà a quarti (da una metà, Win+↑/↓ al quarto in
  alto o in basso). Fermando il mouse sul pulsante Ingrandisci (o con
  Win+Z) compaiono i **layout di snap** (metà, 2/3 e 1/3, terzi, metà e due
  quarti, quarti, 1/4-1/2-1/4): un clic su una zona aggancia lì la finestra.
  Poi **Snap Assist** propone le altre finestre, in anteprima, per gli
  spazi rimasti liberi.
- **Esplora file** (Win+E, `vela-files`), come quello di Windows 11, in
  circa 100 ms dal lancio: schede (Ctrl+T, Ctrl+W, clic centrale su una
  cartella), barra degli indirizzi a pezzi (con le sottocartelle dietro le
  frecce, o da scrivere con Ctrl+L), ricerca nelle sottocartelle, barra dei
  comandi (Nuovo, Taglia, Copia, Incolla, Rinomina, Elimina, Ordina,
  Visualizza), riquadro di navigazione con Home (Accesso rapido e file
  recenti), Questo PC (unità e spazio libero) e Cestino (con Ripristina).
  Viste Dettagli (colonne ordinabili e allargabili) e icone grandi, medie e
  piccole con le miniature, ricordate per cartella; selezione col
  rettangolo, Ctrl e Maiusc, rinomina sul posto (F2), scrittura del nome
  per saltare a un file, Ctrl+Z. Copie e spostamenti in sottofondo con
  l'avanzamento e "Sostituisci o ignora file"; trascinamento da e verso le
  app, le cartelle, il riquadro di navigazione e il percorso. Gli stessi
  menu del tasto destro del desktop, con Apri con, Comprimi, Estrai tutto,
  Mostra altre opzioni e la finestra Proprietà della shell. **Riquadro di
  anteprima** (Alt+P: immagini, testo, miniature di video e PDF) e
  **riquadro dettagli** (Alt+Maiusc+P, o "Dettagli" a destra della barra
  dei comandi). Le **unità non ancora montate** (udisks, per esempio la
  partizione di Windows) stanno in Questo PC e si aprono con un doppio clic
  (con la password, se serve); le chiavette si espellono dal menu. Home con
  Accesso rapido, **Preferiti** e Recenti; i collegamenti hanno la freccia.
- **Impostazioni** (Win+I, `vela-settings`), come quelle di Windows 11, con
  la ricerca e il fondo Mica che continua la barra del titolo:
  - Sistema: **Schermo** (disposizione dei monitor trascinabile, scala,
    risoluzione, orientamento, frequenza, e "Mantenere queste impostazioni?"
    che torna indietro da solo dopo 15 secondi; **Luce notturna** con
    intensità e pianificazione; frequenza variabile e tearing nei giochi),
    **Audio** (uscite e
    ingressi con il loro volume), **Notifiche** (Non disturbare),
    **Alimentazione** (schermo spento dopo, blocco, modalità di
    alimentazione), **Appunti** (cronologia, cancella), **Informazioni** (specifiche, Rinomina questo PC);
  - **Bluetooth e dispositivi** (Aggiungi dispositivo, connetti,
    disconnetti, rimuovi, batteria; **Mouse**: pulsante principale,
    velocità, precisione, righe per scatto della rotellina; **Touchpad**:
    acceso o spento anche solo con un mouse, velocità, tocco per cliccare,
    direzione dello scorrimento, gesti a tre e quattro dita) e **Rete e
    Internet** (reti Wi-Fi con la password, proprietà dei collegamenti);
  - Personalizzazione: **Sfondo** (immagini recenti, Sfoglia foto, sfondi
    del sistema), **Colori** (la tavolozza di Windows 11, modalità Chiara,
    Scura o Personalizzata), **Barra delle applicazioni** (allineamento al
    centro o a sinistra, su tutti gli schermi, Termina attività);
  - App: **App installate** (cerca, disinstalla) e **App predefinite**;
  - Ora e lingua: **Data e ora** (fuso orario, sincronizzazione) e
    **Tastiera** (layout, con Win+Spazio per passare dall'uno all'altro, e
    ripetizione dei tasti);
  - **Accessibilità**: lente di ingrandimento (e di quanto ingrandisce),
    filtri colore (quale, e la scorciatoia Win+Ctrl+C), tasti permanenti.

  Ogni cambiamento vale subito: la shell rilegge
  `~/.config/Vela/vela-shell.conf` appena cambia, il compositor rilegge
  `~/.config/vela/vela.conf` quando le Impostazioni glielo chiedono (e lo
  aggiorna lui stesso quando Luce notturna, filtri o tasti permanenti si
  accendono dalle impostazioni rapide o dalla pianificazione). Le voci
  dei menu (Impostazioni schermo, Personalizza, Win+X...) aprono la pagina
  giusta.

## Compilare

Servono CMake ≥ 3.22, un compilatore C++20, **wlroots 0.20**
(la 0.19 dovrebbe andare, vedi sotto), Qt ≥ 6.5, LayerShellQt e, per il
renderer, **Vulkan 1.4** con gli header, `glslc`, GBM e libdrm.

Vela disegna solo con Vulkan 1.4, senza ripieghi: serve una GPU con un driver
recente (AMD e Intel con Mesa ≥ 25.0, NVIDIA con il driver proprietario ≥ 570
o NVK). Se manca, Vela non parte e il log dice perché.

**Arch / CachyOS / EndeavourOS**
```sh
sudo pacman -S --needed base-devel cmake pkgconf wlroots0.20 wayland-protocols \
    libxkbcommon pixman libinput qt6-declarative qt6-svg layer-shell-qt \
    vulkan-headers vulkan-icd-loader shaderc mesa libdrm
```

**Fedora**
```sh
sudo dnf install cmake gcc-c++ wlroots-devel wayland-devel wayland-protocols-devel \
    libxkbcommon-devel pixman-devel libinput-devel qt6-qtdeclarative-devel \
    qt6-qtsvg-devel qt6-qtwayland-devel layer-shell-qt-devel \
    vulkan-headers vulkan-loader-devel glslc mesa-libgbm-devel libdrm-devel
```

**openSUSE Tumbleweed**
```sh
sudo zypper install cmake gcc-c++ wlroots-devel wayland-protocols-devel \
    libxkbcommon-devel pixman-devel libinput-devel qt6-declarative-devel \
    qt6-svg-devel qt6-waylandclient-devel layer-shell-qt6-devel \
    vulkan-headers vulkan-devel shaderc libgbm-devel libdrm-devel
```

I nomi dei pacchetti cambiano ogni tanto: se uno non esiste, cerca
`wlroots` / `layer-shell-qt` nel gestore pacchetti. Per vedere quale versione
di wlroots hai:

```sh
pkg-config --list-all | grep wlroots
```

Poi, dalla cartella del progetto:

```sh
cmake -B build -G Ninja            # oppure senza -G Ninja
cmake --build build
```

Se hai wlroots 0.19 invece della 0.20:

```sh
cmake -B build -DVELA_WLROOTS=wlroots-0.19
```

## Usarlo come sessione

Installato, Vela compare nella schermata di accesso (SDDM o Plasma Login)
accanto a Plasma.

**Arch / CachyOS: come pacchetto** (`vela-git`, in `/usr`). Si installa e si
aggiorna come gli altri pacchetti: ogni volta che c'è un commit nuovo si
rifà lo stesso comando, e `pacman -R vela-git` lo toglie del tutto.

```sh
cd packaging/arch
makepkg -si                                            # dal repository su GitHub (chiave SSH)
VELA_GIT_URL=file://$PWD/../.. makepkg -si             # oppure da questa copia (i commit, non le modifiche in sospeso)
```

Chi aveva installato prima con `cmake --install` toglie quella copia, che in
`/usr/local` verrebbe prima del pacchetto (e i file in `/etc` farebbero
rifiutare l'installazione a pacman):

```sh
sudo xargs rm -v < build/install_manifest.txt
```

**Altre distribuzioni, o senza pacchetto:**

```sh
sudo cmake --install build        # in /usr/local (più /etc/pam.d e /etc/xdg)
```

Si installano il compositor, la shell e `vela-lock` (schermata di blocco,
con il suo servizio PAM in `/etc/pam.d/vela-lock`), `vela-session` (avvio
della sessione), la voce per la schermata di accesso, la configurazione
dei portali e l'unità `vela-session.target` di systemd. Se la voce "Vela"
non compare, il gestore di accesso non guarda in `/usr/local`:
`sudo ln -s /usr/local/share/wayland-sessions/vela.desktop /usr/share/wayland-sessions/`.
La sessione:

- dice alle app che il desktop è Vela, basato sui servizi di KDE
  (`XDG_CURRENT_DESKTOP=Vela:KDE`): le app Qt/KDE usano il tema di KDE
  (stile, colori, font, icone), e Brave, Chrome e le app Electron il
  portachiavi di KDE, come dentro Plasma (accessi ai siti e password
  restano gli stessi nelle due sessioni);
- avvia i portali (selettore file di KDE; catture e condivisione dello
  schermo con `xdg-desktop-portal-wlr`: quando un'app chiede di condividere,
  la shell mostra cosa scegliere, uno schermo intero o una finestra, con le
  anteprime), l'agente di KDE per le password di
  amministratore e le app che hai messo all'avvio;
- usa la tastiera scelta nelle Impostazioni di Vela, altrimenti quella di
  KDE o del sistema (`localectl`);
- ricorda la disposizione degli schermi in `~/.config/vela/schermi.conf`.
  Si cambia da Impostazioni > Sistema > Schermo (che usa `wlr-randr`), o a
  mano: `wlr-randr --output HDMI-A-1 --pos -1920,0`;
- tiene il log dell'ultima sessione (e della precedente) in
  `~/.local/state/vela/`.

Pacchetti consigliati su Arch: `xdg-desktop-portal-wlr` (condivisione dello
schermo, screenshot dalle app) e `wlr-randr`.

Anche avviato da una console (Ctrl+Alt+F3) Vela si collega alla sessione,
ma solo se non c'è già un'altra sessione grafica attiva: se Plasma è
aperto su un'altra console, Vela non tocca l'ambiente dei suoi servizi.

## Provarlo dentro KDE

Non serve uscire da Plasma: lanciato da una sessione Wayland, Vela si apre
in una finestra, come un "monitor virtuale".

```sh
sh scripts/run-nested.sh
```

Le app che avvii da lì (menu Start, `Alt+Invio`) si aprono dentro Vela.

### Scorciatoie

Quando Vela gira in una finestra, KDE si tiene il tasto Super: per questo
ogni comando ha anche una versione con Alt. Le varianti Alt+lettera sono
attive **solo** dentro un'altra sessione: nella sessione vera Alt+lettera
resta alle app, che lo usano per aprire i propri menu.

| Azione                           | Sessione vera       | Dentro KDE    |
|----------------------------------|---------------------|---------------|
| Menu Start                       | Super               | Alt+S         |
| Terminale                        | Super+Invio         | Alt+Invio     |
| Chiudi finestra                  | Alt+F4              | Alt+Q         |
| Cambia finestra (con anteprime)  | Alt+Tab, Alt+Maiusc+Tab | Alt+J, Alt+Maiusc+J |
| Massimizza                       | Super+↑             | Alt+M         |
| Ripristina, poi riduci a icona   | Super+↓             |               |
| Aggancia a metà sinistra/destra  | Super+← / Super+→   | Alt+← / Alt+→ |
| Da una metà al quarto sopra/sotto | Super+↑ / Super+↓  |               |
| Layout di snap                   | Super+Z             | Alt+Z         |
| Blocca lo schermo                | Super+L             | Alt+L         |
| Menu Win+X                       | Super+X             | Alt+X         |
| Esegui                           | Super+R             | Alt+R         |
| Mostra il desktop                | Super+D             | Alt+D         |
| Menu della finestra              | Alt+Spazio          | Alt+Spazio    |
| Impostazioni rapide              | Super+A             | Alt+A         |
| Centro notifiche e calendario    | Super+N             | Alt+N         |
| Impostazioni                     | Super+I             | Alt+I         |
| Esplora file                     | Super+E             | Alt+E         |
| Visualizzazione attività         | Super+Tab           | Alt+W         |
| Desktop precedente / successivo  | Super+Ctrl+← / →    | Alt+Ctrl+← / → |
| Nuovo desktop / chiudi desktop   | Super+Ctrl+D / Super+Ctrl+F4 | Alt+Ctrl+D |
| Layout di tastiera successivo    | Super+Spazio        |               |
| Lente: ingrandisci / riduci / chiudi | Super+più / Super+meno / Super+Esc | |
| Filtri colore (se attivata la scorciatoia) | Super+Ctrl+C |       |
| Cronologia degli Appunti         | Super+V             |               |
| Strumento di cattura             | Super+Maiusc+S, Stamp | Stamp       |
| Sposta / ridimensiona una finestra | Super+trascina / Super+tasto destro |  |
| Cambia console                   | Ctrl+Alt+F1…F12     |               |
| Esci da Vela                     | Alt+Shift+Esc       | Alt+Shift+Esc |

### Variabili utili

| Variabile            | Effetto                                              |
|----------------------|------------------------------------------------------|
| `XKB_DEFAULT_LAYOUT` | layout tastiera (lo script usa `it` di default)      |
| `VELA_TERMINAL`      | terminale da usare (predefinito: konsole)            |
| `VELA_SCALE`         | scala degli schermi, es. `1.25`, o per schermo: `DP-1=1.5,HDMI-A-1=1`. Senza, Vela la sceglie dai DPI di ogni schermo (come Windows) |
| `VELA_VRR=1` / `=0`  | refresh variabile sempre acceso / mai (vince sulle Impostazioni, che di solito lo accendono solo per le app a schermo intero) |
| `VELA_LATCH=0`       | disegna appena arriva il vblank, invece che il più tardi possibile prima del successivo (late latching, per confronto) |
| `VELA_LATCH_MARGIN`  | margine minimo del late latching in ms (predefinito 1; cresce da solo se un frame arriva tardi) |
| `VELA_SCANOUT=0`     | niente scanout diretto delle app a schermo intero (per confronto e diagnosi) |
| `VELA_DEBUG_SCANOUT=1` | nel log, perché un'app a schermo intero non va in scanout diretto |
| `VELA_TEARING=0`     | niente tearing anche per i giochi che lo chiedono (vince sulle Impostazioni) |
| `VELA_SCREEN_OFF`    | minuti di inattività prima di bloccare e spegnere gli schermi (predefinito 10; 0: mai). Vince sulla scelta fatta nelle Impostazioni |
| `VELA_LOCK_ON_IDLE=0` | con l'inattività spegne gli schermi senza bloccare (vince sulle Impostazioni) |
| `VELA_LOCK`          | programma di blocco da usare al posto di `vela-lock` (es. `swaylock`) |
| `VELA_REALTIME=0`    | niente scheduling realtime per il thread principale del compositor (attivo se `ulimit -r` > 0 o con `CAP_SYS_NICE`) |
| `VELA_NATURAL_SCROLL=0` | scorrimento classico sul touchpad (vince sulle Impostazioni) |
| `VELA_ICON_THEME`    | tema di icone se Qt non lo trova (predef. breeze-dark) |
| `VELA_WALLPAPER`     | immagine di sfondo (SVG, PNG, JPEG...), riempie lo schermo tagliando i bordi; vince su quella scelta nelle Impostazioni |
| `VELA_STATS=1`       | ogni 2 s, per schermo: fps, costo dei frame, tempo dal disegno alla luce, vblank persi |
| `VELA_DEBUG=1`       | log dettagliato di wlroots                           |
| `VELA_OUTPUT_SIZE`   | risoluzione quando lo schermo non ne ha (finestra annidata, headless), es. `1920x1080`, con frequenza facoltativa: `1920x1080@144` |
| `VELA_VULKAN_VALIDATION=1` | validation layer di Vulkan (pacchetto `vulkan-validation-layers`) |
| `VELA_DEBUG_INPUT=1` | mouse e tastiera virtuali per `vela-input` (spento di default: permettono a qualunque programma di simulare input) |

Lo snap funziona anche col mouse: trascina una finestra contro il bordo
sinistro o destro (o in alto, per massimizzarla) e un'anteprima mostra dove
finirà. Trascinando una finestra agganciata o massimizzata, torna alla sua
dimensione sotto il cursore.

## Provarlo senza guardarlo

`scripts/run-headless.sh` avvia Vela senza finestra (backend headless di
wlroots, 1920x1080): nulla compare sullo schermo, ma le app si aprono. Con
gli strumenti in `build/tools/` lo si guarda e lo si comanda come un utente:

```sh
sh scripts/run-headless.sh &          # il log dice il WAYLAND_DISPLAY
export WAYLAND_DISPLAY=wayland-1
kcalc &
build/tools/vela-input key super+Left                  # aggancia a sinistra
build/tools/vela-input move 1100 1055 click            # clic sulla taskbar
build/tools/vela-windows list                          # finestre e stato
build/tools/vela-shot cattura.png                      # screenshot PNG
```

- `vela-shot FILE.png [X Y L A]`: cattura lo schermo o una zona
  (`VELA_SHOT_OUTPUT=N` per l'N-esimo schermo).
- `vela-windows [list] | AZIONE APP_ID`: activate, minimize, restore,
  maximize, unmaximize, close.
- `vela-input AZIONE...`: `move X Y`, `down`/`up`/`click [left|right|middle]`,
  `key super+Left`, `keydown alt`/`keyup alt`, `type testo`, `sleep MS`,
  eseguite in ordine. Un
  trascinamento va fatto in un solo comando: quando `vela-input` termina,
  i tasti rimasti premuti vengono rilasciati.

- `vela-pattern [--scala1] [L A]`: finestra di prova della nitidezza; ogni
  pixel del suo buffer codifica le proprie coordinate.

Funzionano anche con Vela annidato in KDE (per `vela-input` serve
`VELA_DEBUG_INPUT=1`).

**Più schermi senza schermi:** nella sessione headless si collegano e
scollegano schermi a caldo col socket dei comandi
(`$XDG_RUNTIME_DIR/vela-$WAYLAND_DISPLAY.sock`, una riga per comando):
`test-output add 2560x1440` ne collega uno (`HEADLESS-2`, `HEADLESS-3`...),
`test-output remove HEADLESS-2` lo stacca; la scala per nome con
`VELA_SCALE=HEADLESS-1=1,HEADLESS-2=1.5`. Spegnere e riaccendere con
`wlr-randr --output HEADLESS-2 --off` / `--on` fa tornare le finestre al loro
schermo.

**Test della nitidezza:** `sh scripts/test-sharpness.sh` prova le scale
100–200% (o quelle passate come argomenti): la finestra di `vela-pattern`,
appena aperta, agganciata, massimizzata e ripristinata, deve arrivare sullo
schermo **bit per bit**. Serve Python con numpy e Pillow.

## Struttura

```
compositor/src/
  wlr.hpp          unico punto in cui si includono gli header C di wlroots
  listener.hpp     wl_listener RAII con lambda
  motion.hpp       curve e durate delle animazioni
  server.*         avvio, focus, puntatore, scorciatoie, lancio processi
  output.cpp       schermi, scelta della frequenza, frame, spazio dei pannelli
  toplevel.cpp     finestre, popup, animazione di apertura, massimizza,
                   riduci a icona, maniglie per la taskbar
  snapshot.cpp     istantanee delle finestre e animazioni di chiusura/riduzione
  snap.cpp         snap a metà, terzi e quarti (in dodicesimi dell'area utile),
                   anteprima durante il trascinamento, layout e Snap Assist
  workspaces.cpp   desktop virtuali e il passaggio animato dall'uno all'altro
  layer.cpp        superfici della shell (layer-shell)
  keyboard.cpp     tastiera e tasto Super
  accessibility.cpp  Luce notturna (anche pianificata), filtri colore, lente
                   di ingrandimento, tasti permanenti
  input.cpp        mouse, touchpad e gesti; l'incolla di Win+V
  settings.*       ~/.config/vela/vela.conf (inattività, tastiera, Luce
                   notturna, accessibilità, tearing)
shell/
  src/             app installate, finestre aperte, taskbar, icone, socket
  qml/             Theme, Taskbar, StartMenu e componenti
explorer/
  src/             Esplora: cartella (letta in un thread), operazioni sui file, luoghi
  qml/             finestra, schede, viste Dettagli e Icone, menu (MenuPanel della shell)
settings/
  src/             le Impostazioni: schermi, audio, rete, Bluetooth, preferenze
  qml/             finestra, componenti di Windows 11, pages/ (una per pagina)
tools/             vela-shot, vela-windows, vela-input (prove automatiche)
images/            sfondo predefinito
protocols/         protocolli wlroots non inclusi in wayland-protocols
scripts/           avvio annidato e headless
```

## Roadmap

**Milestone 1: un desktop usabile tutti i giorni**
- ~~Finestre aperte nella taskbar, riduzione a icona animata verso il
  pulsante.~~ Fatto.
- ~~Animazione di chiusura (istantanea del contenuto che si dissolve).~~
  Fatto.
- ~~Alt+Tab grafico con anteprime.~~ Fatto (anteprime fotografate
  all'apertura, clic per scegliere).
- ~~Snap delle finestre: Super+frecce e trascinamento ai bordi, con
  anteprima.~~ Fatto (metà schermo; i quarti e i layout di Windows 11 con
  la milestone 3).

**Sessione vera** (fatto, settembre 2026): Vela come desktop da scegliere
all'accesso.
- ~~Sessione da SDDM/Plasma Login, collegata a systemd e D-Bus; portali;
  agente polkit; tastiera e schermi del sistema.~~ Fatto.
- ~~Xwayland e i protocolli per i giochi.~~ Fatto, tearing-control
  compreso (ottobre 2026).
- ~~Area di notifica, notifiche, menu di accensione.~~ Fatto.
- ~~Schermata di blocco e inattività.~~ Fatto.
- ~~Selettore dello schermo o della finestra da condividere; bordi per
  ridimensionare le finestre X11 decorate; centro notifiche.~~ Fatto.

**Milestone 2: l'aspetto**
- ~~Sfocatura acrilica, angoli arrotondati e ombre disegnati dal compositor,
  con una scena e un renderer Vulkan tutti nostri.~~ Fatto (ottobre 2026,
  tappe S4–S5 di [docs/renderer.md](docs/renderer.md)).
- ~~Barra del titolo lato server in stile Vela, uguale per tutte le app.~~
  Fatto per le app che la accettano (Qt/KDE, X11), con icona e tinta Mica
  (tappa S6).
- ~~Centro notifiche, impostazioni rapide (volume, rete, luminosità).~~
  Fatto (ottobre 2026), con la scelta della rete Wi-Fi dal pannello e i
  riquadri Luce notturna e Accessibilità.
- ~~Menu del tasto destro copiati in toto da Windows 11, tutti con lo stesso
  componente della shell ([docs/renderer.md §14](docs/renderer.md)).~~ Fatto
  (settembre 2026).

**Milestone 3: sostituire Plasma**
- ~~App Impostazioni (schermi, tastiera, tema) e configurazione su file.~~
  Fatto (ottobre 2026), con la modalità chiara anche per la shell
  (Chiaro, Scuro, Personalizzato).
- ~~Desktop virtuali e Visualizzazione attività, con i loro menu del tasto
  destro.~~ Fatto (ottobre 2026).
- ~~Snap a quarti e layout di Windows 11.~~ Fatto (ottobre 2026), con
  Snap Assist e i gruppi di snap sulla taskbar.
- ~~Icone sul desktop con il menu del desktop e dei file di Windows 11.~~
  Fatto (settembre 2026), con "Mostra altre opzioni" (service menu di KDE)
  e il trascinamento di file da e verso le app, le miniature delle immagini
  e la finestra Proprietà, Collegamento, Preferiti e Condividi; lo stesso
  menu dei file lo usa Esplora.
- ~~File manager veloce ("Esplora"), avvio a freddo sotto i 150 ms come
  obiettivo.~~ Fatto (ottobre 2026): primo fotogramma a circa 100 ms
  dall'avvio del processo; con il riquadro di anteprima e dei dettagli,
  Condividi, i collegamenti, i Preferiti e le unità non montate.

## Obiettivi misurabili

Per non perdere di vista "più leggero di Windows":

- Menu Start: primo frame visibile entro un frame dopo il tasto.
- Menu del tasto destro: visibile al frame successivo al clic.
- Esplora file: primo fotogramma entro 150 ms dall'avvio del processo
  (`VELA_FILES_TIMING=1 vela-files` lo stampa; oggi circa 100 ms, anche
  in una cartella con migliaia di file).
- Nessuna animazione legata a timer: sempre ai frame reali dello schermo.
- Memoria della shell tenuta sotto controllo a ogni milestone.
- A riposo (nessuno tocca niente) compositor e shell non consumano CPU e
  si svegliano poche volte al secondo.

Le misure si ripetono con `scripts/measure.sh` dentro una sessione di Vela
(30 secondi senza toccare nulla; `--esplora` aggiunge l'avvio di Esplora,
`--json` una riga da conservare per confrontare nel tempo): memoria (PSS)
di compositor e shell, CPU a riposo, risvegli al secondo, e a batteria il
consumo medio del computer. In una sessione di prova senza schermo, ottobre
2026:

| | memoria | CPU a riposo | risvegli/s |
|---|---|---|---|
| vela-compositor | 17 MiB | 0,1% | 3–7 |
| vela-shell | 143 MiB | 0,1% | 1–4 |

Esplora: primo fotogramma a 104 ms dall'avvio del processo (mediana di 5).

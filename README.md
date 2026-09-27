# Vela

Un desktop environment per Linux moderno, fluido e leggero, con l'aspetto
e la comodità di Windows 11. L'obiettivo è sostituire KDE Plasma un pezzo
alla volta.

> **Stato: milestone 0, prototipo.** Il codice è scritto e verificato sulle
> API di wlroots 0.20 (confrontandolo con sway 1.12, cage, labwc e Wayfire),
> ma questa è la sua prima compilazione su una macchina vera. Se qualcosa non
> compila, l'errore esatto basta a sistemarlo.

## Com'è fatto

```
┌──────────────────────── vela-shell (Qt Quick, C++/QML) ────────────────────────┐
│  taskbar · menu Start · (poi: notifiche, sfondo, impostazioni rapide)          │
└────────────────────────────── protocollo layer-shell ──────────────────────────┘
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
- Snap a metà schermo con Super+frecce o trascinando ai bordi, con
  anteprima animata.
- Menu e popup tenuti dentro lo schermo.
- Alt+Tab come su Windows: tenendo premuto Alt compare il pannello con le
  anteprime delle finestre dalla più recente (anche quelle ridotte a icona
  o coperte); un Alt+Tab veloce cambia finestra senza mostrarlo.
- Cattura di schermi e finestre con i protocolli standard ext-image-copy-capture
  (le anteprime di Alt+Tab; in futuro la condivisione dello schermo).
- Buffer GPU condivisi con le app (linux-dmabuf) e cursori per nome
  (cursor-shape), oltre ai protocolli che le app moderne si aspettano:
  scala frazionaria, viewporter, appunti, screencopy, presentation time.
- Il tasto Super premuto da solo apre il menu Start.
- Touchpad come su Windows: tocco per cliccare, trascinamento col tocco,
  niente tocchi accidentali mentre scrivi, scorrimento naturale.
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
- Sfondo del desktop (`images/vela_splash_169.svg`, incluso nell'eseguibile),
  disegnato già alla dimensione esatta dello schermo: in memoria c'è solo
  ciò che si vede. Un altro sfondo con `VELA_WALLPAPER`.
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
accanto a Plasma:

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

- dice alle app che il desktop è Vela (`XDG_CURRENT_DESKTOP=Vela`) e che le
  app Qt/KDE devono usare il tema di KDE (stile, colori, font, icone);
- avvia i portali (selettore file di KDE; catture e condivisione dello
  schermo con `xdg-desktop-portal-wlr`), l'agente di KDE per le password di
  amministratore e le app che hai messo all'avvio;
- usa la tastiera scelta in KDE, o quella del sistema (`localectl`);
- ricorda la disposizione degli schermi in `~/.config/vela/schermi.conf`.
  Per cambiarla, finché non ci sono le Impostazioni di Vela, `wlr-randr`:
  `wlr-randr --output HDMI-A-1 --pos -1920,0`;
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
| Blocca lo schermo                | Super+L             | Alt+L         |
| Sposta / ridimensiona una finestra | Super+trascina / Super+tasto destro |  |
| Cambia console                   | Ctrl+Alt+F1…F12     |               |
| Esci da Vela                     | Alt+Shift+Esc       | Alt+Shift+Esc |

### Variabili utili

| Variabile            | Effetto                                              |
|----------------------|------------------------------------------------------|
| `XKB_DEFAULT_LAYOUT` | layout tastiera (lo script usa `it` di default)      |
| `VELA_TERMINAL`      | terminale da usare (predefinito: konsole)            |
| `VELA_SCALE`         | scala degli schermi, es. `1.25`, o per schermo: `DP-1=1.5,HDMI-A-1=1`. Senza, Vela la sceglie dai DPI di ogni schermo (come Windows) |
| `VELA_VRR=1`         | attiva il refresh variabile (spento di default)      |
| `VELA_LATCH=0`       | disegna appena arriva il vblank, invece che il più tardi possibile prima del successivo (late latching, per confronto) |
| `VELA_LATCH_MARGIN`  | margine minimo del late latching in ms (predefinito 1; cresce da solo se un frame arriva tardi) |
| `VELA_SCANOUT=0`     | niente scanout diretto delle app a schermo intero (per confronto e diagnosi) |
| `VELA_SCREEN_OFF`    | minuti di inattività prima di bloccare e spegnere gli schermi (predefinito 10; 0: mai) |
| `VELA_LOCK_ON_IDLE=0` | con l'inattività spegne gli schermi senza bloccare |
| `VELA_LOCK`          | programma di blocco da usare al posto di `vela-lock` (es. `swaylock`) |
| `VELA_REALTIME=0`    | niente scheduling realtime per il thread principale del compositor (attivo se `ulimit -r` > 0 o con `CAP_SYS_NICE`) |
| `VELA_NATURAL_SCROLL=0` | scorrimento classico sul touchpad (predef. naturale, come Windows) |
| `VELA_ICON_THEME`    | tema di icone se Qt non lo trova (predef. breeze-dark) |
| `VELA_WALLPAPER`     | immagine di sfondo (SVG, PNG, JPEG...), riempie lo schermo tagliando i bordi |
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
  snap.cpp         snap a metà schermo, con anteprima durante il trascinamento
  layer.cpp        superfici della shell (layer-shell)
  keyboard.cpp     tastiera e tasto Super
shell/
  src/             app installate, finestre aperte, taskbar, icone, socket
  qml/             Theme, Taskbar, StartMenu e componenti
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
  all'apertura; il clic sulle anteprime più avanti).
- ~~Snap delle finestre: Super+frecce e trascinamento ai bordi, con
  anteprima.~~ Fatto (metà schermo; i quarti e i layout di Windows 11 più
  avanti).

**Sessione vera** (fatto, settembre 2026): Vela come desktop da scegliere
all'accesso.
- ~~Sessione da SDDM/Plasma Login, collegata a systemd e D-Bus; portali;
  agente polkit; tastiera e schermi del sistema.~~ Fatto.
- ~~Xwayland e i protocolli per i giochi.~~ Fatto (manca tearing-control).
- ~~Area di notifica, notifiche, menu di accensione.~~ Fatto.
- ~~Schermata di blocco e inattività.~~ Fatto.
- Da fare: selettore dello schermo da condividere; centro notifiche (le
  notifiche passate); bordi per ridimensionare le finestre X11 decorate.

**Milestone 2: l'aspetto**
- Sfocatura acrilica, angoli arrotondati e ombre disegnati dal compositor,
  con una scena e un renderer Vulkan tutti nostri: vedi
  [docs/renderer.md](docs/renderer.md).
- Barra del titolo lato server in stile Vela, uguale per tutte le app.
- Centro notifiche, impostazioni rapide (volume, rete, luminosità).

**Milestone 3: sostituire Plasma**
- App Impostazioni (schermi, tastiera, tema) e configurazione su file.
- Desktop virtuali e Visualizzazione attività, snap a quarti e layout di
  Windows 11.
- File manager veloce ("Esplora"), avvio a freddo sotto i 150 ms come
  obiettivo.

## Obiettivi misurabili

Per non perdere di vista "più leggero di Windows":

- Menu Start: primo frame visibile entro un frame dopo il tasto.
- Nessuna animazione legata a timer: sempre ai frame reali dello schermo.
- Memoria della shell tenuta sotto controllo a ogni milestone.

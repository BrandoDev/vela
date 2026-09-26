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
  la GPU usando la scena di wlroots e il renderer **Vulkan** (se non c'è,
  OpenGL ES), gestisce schermi, input e finestre.
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

**Shell**
- Sfondo del desktop (`images/vela_splash.svg`, incluso nell'eseguibile),
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

## Compilare

Servono CMake ≥ 3.22, un compilatore C++20, **wlroots 0.20**
(la 0.19 dovrebbe andare, vedi sotto), Qt ≥ 6.5 e LayerShellQt.

**Arch / CachyOS / EndeavourOS**
```sh
sudo pacman -S --needed base-devel cmake pkgconf wlroots0.20 wayland-protocols \
    libxkbcommon pixman libinput qt6-declarative qt6-svg layer-shell-qt
```

**Fedora**
```sh
sudo dnf install cmake gcc-c++ wlroots-devel wayland-devel wayland-protocols-devel \
    libxkbcommon-devel pixman-devel libinput-devel qt6-qtdeclarative-devel \
    qt6-qtsvg-devel qt6-qtwayland-devel layer-shell-qt-devel
```

**openSUSE Tumbleweed**
```sh
sudo zypper install cmake gcc-c++ wlroots-devel wayland-protocols-devel \
    libxkbcommon-devel pixman-devel libinput-devel qt6-declarative-devel \
    qt6-svg-devel qt6-waylandclient-devel layer-shell-qt6-devel
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
| Esci da Vela                     | Alt+Shift+Esc       | Alt+Shift+Esc |

### Variabili utili

| Variabile            | Effetto                                              |
|----------------------|------------------------------------------------------|
| `XKB_DEFAULT_LAYOUT` | layout tastiera (lo script usa `it` di default)      |
| `VELA_TERMINAL`      | terminale da usare (predefinito: konsole)            |
| `VELA_SCALE`         | scala dello schermo, es. `1.25`                      |
| `VELA_VRR=1`         | attiva il refresh variabile (spento di default)      |
| `VELA_NATURAL_SCROLL=0` | scorrimento classico sul touchpad (predef. naturale, come Windows) |
| `VELA_ICON_THEME`    | tema di icone se Qt non lo trova (predef. breeze-dark) |
| `VELA_WALLPAPER`     | immagine di sfondo (SVG, PNG, JPEG...), riempie lo schermo tagliando i bordi |
| `WLR_RENDERER`       | renderer: di default `vulkan` (con ripiego su OpenGL ES); `gles2` o `pixman` per forzarne un altro |
| `VELA_DEBUG=1`       | log dettagliato di wlroots                           |
| `VELA_OUTPUT_SIZE`   | risoluzione quando lo schermo non ne ha (finestra annidata, headless), es. `1920x1080` |
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

- `vela-shot FILE.png [X Y L A]`: cattura lo schermo o una zona.
- `vela-windows [list] | AZIONE APP_ID`: activate, minimize, restore,
  maximize, unmaximize, close.
- `vela-input AZIONE...`: `move X Y`, `down`/`up`/`click [left|right|middle]`,
  `key super+Left`, `keydown alt`/`keyup alt`, `type testo`, `sleep MS`,
  eseguite in ordine. Un
  trascinamento va fatto in un solo comando: quando `vela-input` termina,
  i tasti rimasti premuti vengono rilasciati.

Funzionano anche con Vela annidato in KDE (per `vela-input` serve
`VELA_DEBUG_INPUT=1`).

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

**Milestone 2: l'aspetto**
- Sfocatura acrilica, angoli arrotondati e ombre disegnati dal compositor.
  Due strade: SceneFX (sostituto di wlr_scene che li ha già) oppure un
  renderer proprio.
- Barra del titolo lato server in stile Vela, uguale per tutte le app.
- Sfondo, centro notifiche, impostazioni rapide (volume, rete, luminosità).

**Milestone 3: sostituire Plasma**
- Xwayland per le app solo-X11.
- App Impostazioni (schermi, tastiera, tema) e configurazione su file.
- Schermata di blocco, sessione selezionabile da SDDM, portali
  (condivisione schermo, selettore file).
- File manager veloce ("Esplora"), avvio a freddo sotto i 150 ms come
  obiettivo.

## Obiettivi misurabili

Per non perdere di vista "più leggero di Windows":

- Menu Start: primo frame visibile entro un frame dopo il tasto.
- Nessuna animazione legata a timer: sempre ai frame reali dello schermo.
- Memoria della shell tenuta sotto controllo a ogni milestone.

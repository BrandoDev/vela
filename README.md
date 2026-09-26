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
  la GPU usando la scena di wlroots, gestisce schermi, input e finestre.
  Le animazioni avanzano a ogni frame reale dello schermo, quindi a 180 Hz
  fanno 180 passi al secondo.
- **Shell** (`shell/`): **Qt Quick** (scena grafica su GPU) con
  **LayerShellQt**. Ogni pezzo della shell è un'app separata dal compositor:
  se la shell va in crash, le tue finestre restano aperte.
- I due parlano con i protocolli Wayland standard, più un piccolo socket per
  i comandi (es. il tasto Super apre il menu Start).
- Motion design in un posto solo: `compositor/src/motion.hpp` e
  `shell/qml/Theme.qml` usano la stessa curva, cubic-bezier(0.1, 0.9, 0.2, 1).

### Cosa c'è già

**Compositor**
- Sceglie da solo la **frequenza più alta** del monitor alla risoluzione
  nativa. Molti monitor a 180 Hz dichiarano 60 Hz come "preferita".
- Finestre che si aprono **sfumando e salendo di qualche pixel** (220 ms).
- Spostamento e ridimensionamento. Trascinando una finestra massimizzata
  questa si ripristina sotto il cursore, come su Windows.
- Massimizza e schermo intero. Le finestre a schermo intero stanno sopra la
  taskbar e beneficiano del direct scanout.
- Menu e popup tenuti dentro lo schermo.
- Buffer GPU condivisi con le app (linux-dmabuf) e cursori per nome
  (cursor-shape), oltre ai protocolli che le app moderne si aspettano:
  scala frazionaria, viewporter, appunti, screencopy, presentation time.
- Il tasto Super premuto da solo apre il menu Start.

**Shell**
- Taskbar in stile Windows 11: pulsante Start e app fissate al centro,
  orologio a destra, effetti di hover e pressione animati.
- Menu Start con ricerca istantanea tra le app installate (legge i normali
  file `.desktop`, con i nomi in italiano), navigazione da tastiera e
  animazioni di apertura e chiusura.
- Si chiude cliccando fuori o con Esc.

## Compilare

Servono CMake ≥ 3.22, un compilatore C++20, **wlroots 0.20**
(la 0.19 dovrebbe andare, vedi sotto), Qt ≥ 6.5 e LayerShellQt.

**Arch / CachyOS / EndeavourOS**
```sh
sudo pacman -S --needed base-devel cmake pkgconf wlroots0.20 wayland-protocols \
    libxkbcommon pixman qt6-declarative layer-shell-qt
```

**Fedora**
```sh
sudo dnf install cmake gcc-c++ wlroots-devel wayland-devel wayland-protocols-devel \
    libxkbcommon-devel pixman-devel qt6-qtdeclarative-devel layer-shell-qt-devel
```

**openSUSE Tumbleweed**
```sh
sudo zypper install cmake gcc-c++ wlroots-devel wayland-protocols-devel \
    libxkbcommon-devel pixman-devel qt6-declarative-devel layer-shell-qt6-devel
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
ogni comando ha anche una versione con Alt.

| Azione                           | Sessione vera       | Dentro KDE    |
|----------------------------------|---------------------|---------------|
| Menu Start                       | Super               | Alt+S         |
| Terminale                        | Super+Invio         | Alt+Invio     |
| Chiudi finestra                  | Alt+F4              | Alt+Q         |
| Finestra precedente              | Alt+Tab             | Alt+J         |
| Massimizza / ripristina          | Super+↑ / Super+↓   | Alt+M         |
| Esci da Vela                     | Alt+Shift+Esc       | Alt+Shift+Esc |

### Variabili utili

| Variabile            | Effetto                                              |
|----------------------|------------------------------------------------------|
| `XKB_DEFAULT_LAYOUT` | layout tastiera (lo script usa `it` di default)      |
| `VELA_TERMINAL`      | terminale da usare (predefinito: konsole)            |
| `VELA_SCALE`         | scala dello schermo, es. `1.25`                      |
| `VELA_VRR=1`         | attiva il refresh variabile (spento di default)      |
| `VELA_ICON_THEME`    | tema di icone se Qt non lo trova (predef. breeze-dark) |
| `VELA_DEBUG=1`       | log dettagliato di wlroots                           |

## Struttura

```
compositor/src/
  wlr.hpp          unico punto in cui si includono gli header C di wlroots
  listener.hpp     wl_listener RAII con lambda
  motion.hpp       curve e durate delle animazioni
  server.*         avvio, focus, puntatore, scorciatoie, lancio processi
  output.cpp       schermi, scelta della frequenza, frame, spazio dei pannelli
  toplevel.cpp     finestre, popup, animazione di apertura, massimizza
  layer.cpp        superfici della shell (layer-shell)
  keyboard.cpp     tastiera e tasto Super
shell/
  src/             modello delle app, icone, socket dei comandi
  qml/             Theme, Taskbar, StartMenu e componenti
protocols/         wlr-layer-shell (non incluso in wayland-protocols)
scripts/           avvio annidato
```

## Roadmap

**Milestone 1: un desktop usabile tutti i giorni**
- Finestre aperte nella taskbar (protocollo foreign-toplevel), con
  riduzione a icona e animazione verso la taskbar.
- Animazione di chiusura (istantanea del contenuto che si dissolve).
- Alt+Tab grafico con anteprime.
- Snap delle finestre: Super+frecce e trascinamento ai bordi, con anteprima.

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

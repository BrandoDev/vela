// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_MOTION_H
#define VELA_MOTION_H

// Motion design di Vela: curve e durate in un solo posto, così tutto il
// desktop si muove nello stesso modo. La shell QML usa gli stessi valori
// (vedi shell/qml/Theme.qml).

#include <stdbool.h>

// Curva di Bézier cubica come quelle di CSS/QML: cubic-bezier(x1, y1, x2, y2).
struct vela_curve {
    double x1, y1, x2, y2;
};

// x = tempo normalizzato [0, 1] -> progresso [0, 1].
double vela_curve_eval(const struct vela_curve *curve, double x);

// Un'interpolazione a durata fissa. Il tempo di partenza viene fissato al
// primo frame in cui viene letta: così il primo frame disegnato è sempre lo
// stato iniziale esatto, anche se tra la richiesta e il frame passa del tempo.
struct vela_tween {
    double start_ms; // < 0: non ancora letta
    double duration_ms;
    const struct vela_curve *curve; // NULL: lineare
};

void vela_tween_start(struct vela_tween *tween, double duration_ms, const struct vela_curve *curve);
double vela_tween_progress(struct vela_tween *tween, double now_ms);
bool vela_tween_finished(const struct vela_tween *tween, double now_ms);

// Decelerazione: parte decisa e si posa dolcemente. È la curva che dà la
// sensazione "moderna"; conta più della durata.
// Attenzione alle curve più estreme come (0.1, 0.9, 0.2, 1): a 180 Hz fanno
// metà del movimento nei primi 3 frame e l'animazione non si vede più.
extern const struct vela_curve vela_decelerate;

// Apertura finestra: dissolvenza + risalita. Stessa durata di Theme.slow.
#define VELA_WINDOW_OPEN_MS 250.0
#define VELA_WINDOW_OPEN_RISE 36

// Chiusura: la finestra si ritrae un poco e sparisce. Anche qui la curva di
// decelerazione: con una che parte piano la finestra sembrerebbe ignorare il
// clic per metà del tempo.
#define VELA_WINDOW_CLOSE_MS 180.0
#define VELA_WINDOW_CLOSE_SCALE 0.94

// Riduzione a icona e ripristino: la finestra vola verso il suo pulsante
// nella taskbar (e ritorno), rimpicciolendo e sfumando.
#define VELA_WINDOW_MINIMIZE_MS 250.0
#define VELA_WINDOW_MINIMIZE_SCALE 0.3

// Massimizzare e ripristinare: il contenuto di prima si deforma fino al
// riquadro nuovo e sfuma nella finestra vera.
#define VELA_WINDOW_MAXIMIZE_MS 250.0

// Anteprima dello snap: cresce dal centro dell'area e si accende.
#define VELA_SNAP_PREVIEW_MS 150.0

// Cambio di desktop: il desktop che si lascia scivola via sfumando, il nuovo
// arriva dall'altra parte. Lo scorrimento è una frazione dello schermo più
// stretto: con più schermi le finestre non invadono quello accanto.
#define VELA_WORKSPACE_SWITCH_MS 300.0
#define VELA_WORKSPACE_SLIDE_FRACTION 0.25

// Lente di ingrandimento: da un ingrandimento all'altro.
#define VELA_MAGNIFIER_MS 150.0

#endif

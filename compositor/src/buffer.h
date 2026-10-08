// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_BUFFER_H
#define VELA_BUFFER_H

// Un testo che cresce (messaggi alla shell, JSON dello stato): la memoria
// raddoppia quando serve. Si parte da { 0 } e si finisce con
// vela_buffer_finish.

#include <stdbool.h>
#include <stddef.h>

struct vela_buffer {
    char *data; // sempre terminato da '\0' dopo il primo append
    size_t length;
    size_t capacity;
};

void vela_buffer_finish(struct vela_buffer *buffer);
void vela_buffer_append(struct vela_buffer *buffer, const char *text);
void vela_buffer_appendf(struct vela_buffer *buffer, const char *format, ...) __attribute__((format(printf, 2, 3)));
// Una stringa JSON tra virgolette, con gli escape (NULL: "").
void vela_buffer_append_json(struct vela_buffer *buffer, const char *text);
// Il testo, anche se vuoto ("" e mai NULL).
const char *vela_buffer_text(const struct vela_buffer *buffer);

#endif

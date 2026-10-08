// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_BUFFER_H
#define VELA_BUFFER_H

// Growing text (messages to the shell, the state JSON): the memory doubles
// when needed. Start from { 0 } and end with vela_buffer_finish.

#include <stdbool.h>
#include <stddef.h>

struct vela_buffer {
    char *data; // always '\0'-terminated after the first append
    size_t length;
    size_t capacity;
};

void vela_buffer_finish(struct vela_buffer *buffer);
void vela_buffer_append(struct vela_buffer *buffer, const char *text);
void vela_buffer_appendf(struct vela_buffer *buffer, const char *format, ...) __attribute__((format(printf, 2, 3)));
// A quoted JSON string, escaped (NULL: "").
void vela_buffer_append_json(struct vela_buffer *buffer, const char *text);
// The text, even when empty ("" and never NULL).
const char *vela_buffer_text(const struct vela_buffer *buffer);

#endif

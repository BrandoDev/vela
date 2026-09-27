#pragma once

// wlroots è una libreria C e i suoi header non sono pensati per il C++:
// usano `static` dentro le dimensioni degli array nei parametri
// (`const float color[static 4]`) e il protocollo layer-shell ha un campo
// chiamato `namespace`. Qui li includiamo tutti in un unico posto, con gli
// stessi accorgimenti usati da Wayfire (compositor C++ su wlroots 0.20).
//
// Regola: nel resto del progetto si include SOLO questo file, mai
// direttamente <wlr/...>.

// Header di sistema e C++ inclusi PRIMA delle macro, così le #define
// qui sotto non li toccano (le include guard impediscono una seconda
// espansione).
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libinput.h>
#include <pixman.h>
#include <wayland-client-protocol.h> // usato da <wlr/backend/wayland.h>
#include <wayland-server.h>
#include <xkbcommon/xkbcommon.h>

extern "C" {
// Valgono per TUTTI gli header wlroots: se uno di loro includesse
// indirettamente wlr_layer_shell_v1.h, il campo `namespace` sarebbe già
// coperto.
#define static
#define namespace namespace_
#include <wlr/config.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/wayland.h>
#if WLR_HAS_X11_BACKEND
#include <wlr/backend/x11.h>
#endif
#if WLR_HAS_LIBINPUT_BACKEND
#include <wlr/backend/libinput.h>
#endif
#include <wlr/render/allocator.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/render/dmabuf.h>
#include <wlr/render/drm_syncobj.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/swapchain.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/interfaces/wlr_ext_image_capture_source_v1.h>
#include <wlr/util/addon.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/util/transform.h>
#include <wlr/types/wlr_layer_shell_v1.h>

#undef namespace
#undef static
}

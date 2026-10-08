// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VELA_RENDER_RENDER_H
#define VELA_RENDER_RENDER_H

// L'interno del renderer: le strutture e le funzioni che vulkan.c,
// renderer.c, texture.c e pass.c si scambiano. Il resto del compositor
// usa solo render/renderer.h.

#include "render/renderer.h"

#include <vulkan/vulkan.h>
#include <wayland-server-core.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/interface.h>
#include <wlr/render/dmabuf.h>
#include <wlr/util/addon.h>

// --------------------------------------------------------------- formati --

// I formati dei pixel che il renderer conosce: il codice DRM (quello dei
// buffer di app e schermi) e il formato Vulkan corrispondente.
struct vela_pixel_format {
    uint32_t drm;
    // Vista "grezza": gli shader leggono i valori così come sono (codificati
    // sRGB e premoltiplicati) e li portano in spazio lineare da sé (§7.5).
    VkFormat unorm;
    // Vista con conversione sRGB in scrittura, per disegnarci sopra: la GPU
    // fonde in lineare e codifica. VK_FORMAT_UNDEFINED se non esiste.
    VkFormat srgb;
    bool alpha; // i formati X... ignorano il quarto canale
    uint32_t bytes_per_pixel;
};

extern const struct vela_pixel_format vela_pixel_formats[];
extern const int vela_pixel_format_count;
const struct vela_pixel_format *vela_pixel_format_from_drm(uint32_t drm);

// ---------------------------------------------------------------- device --

#define VELA_MAX_SHM_FORMATS 16

struct vela_vulkan {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family;
    char name[256];

    int render_fd; // il render node della stessa GPU, nostro
    // (formato, modifier) importabili come dmabuf e usabili come
    // destinazione di rendering.
    struct wlr_drm_format_set render_formats;
    // (formato, modifier) dei dmabuf che sappiamo leggere come texture:
    // quelli che offriamo alle app (linux-dmabuf).
    struct wlr_drm_format_set texture_formats;
    // Formati dei buffer in memoria condivisa (wl_shm) che sappiamo caricare.
    uint32_t shm_formats[VELA_MAX_SHM_FORMATS];
    int shm_format_count;
    // La GPU sa esportare e importare semafori come sync_file
    // (sincronizzazione implicita con il kernel, §7.3).
    bool sync_file;

    PFN_vkGetMemoryFdPropertiesKHR get_memory_fd_properties;
    PFN_vkGetSemaphoreFdKHR get_semaphore_fd;
    PFN_vkImportSemaphoreFdKHR import_semaphore_fd;

    // Timestamp della GPU (§4.3: quanto costa un frame): ns per tick; 0 se
    // la coda non li sa scrivere. Con i timestamp calibrati si sa anche
    // *quando* la GPU ha finito, su CLOCK_MONOTONIC.
    float timestamp_period;
    uint64_t timestamp_mask;
    PFN_vkGetCalibratedTimestampsKHR get_calibrated_timestamps;

    VkDebugUtilsMessengerEXT messenger;
    VkPhysicalDeviceDrmPropertiesEXT drm;
};

uint32_t vela_vulkan_memory_type(const struct vela_vulkan *vk, uint32_t type_bits, VkMemoryPropertyFlags flags);
// Un dmabuf di quel formato e modifier si può importare per quell'uso.
bool vela_vulkan_supports_dmabuf(const struct vela_vulkan *vk, VkFormat format, uint64_t modifier,
    VkImageUsageFlags usage);
// Un dmabuf come VkImage, senza copie (tutti i piani nello stesso dmabuf).
bool vela_vulkan_import_dmabuf(const struct vela_vulkan *vk, const struct wlr_dmabuf_attributes *dmabuf,
    VkFormat format, VkImageUsageFlags usage, VkImage *image, VkDeviceMemory *memory);
VkShaderModule vela_vulkan_shader(const struct vela_vulkan *vk, const uint32_t *code, size_t bytes);

// -------------------------------------------------------------- renderer --

// Le costanti di ogni disegno (vedi shaders/push.glsl): 160 byte, sotto i
// 256 che Vulkan 1.4 garantisce.
struct vela_quad_push {
    float dst[4]; // x, y, larghezza, altezza in pixel della destinazione
    float target[2]; // dimensioni della destinazione
    float alpha;
    // bit 0: ingrandimento bicubico; bit 1: ritaglio arrotondato; bit 2: filtro colore
    uint32_t flags;
    float uv_origin[2]; // coordinate texture dell'angolo in alto a sinistra
    float uv_x[2]; // spostamento lungo il bordo superiore
    float uv_y[2]; // spostamento lungo il bordo sinistro
    float pad[2];
    float color[4]; // rettangoli e ombre: colore lineare premoltiplicato
    float shape_rect[4]; // rettangolo arrotondato (ritaglio, o chi proietta l'ombra)
    float shape[4]; // raggio degli angoli, sigma dell'ombra
    // Il filtro colore dello schermo (Luce notturna, filtri colore): una
    // matrice 3x3 in spazio lineare, una riga per vec4.
    float filter[12];
};
_Static_assert(sizeof(struct vela_quad_push) == 160, "push constants must match push.glsl");

enum vela_pipeline_kind {
    VELA_PIPELINE_TEXTURE,
    VELA_PIPELINE_RECT,
    VELA_PIPELINE_SHADOW,
    VELA_PIPELINE_BLUR_DOWN,
    VELA_PIPELINE_BLUR_UP,
    VELA_PIPELINE_BLUR_MIX,
};

// Le immagini di lavoro della sfocatura: una catena di livelli, ognuno a
// metà risoluzione del precedente (il primo a metà dello schermo), in
// virgola mobile a 16 bit: lineare, senza bande. Crescono quando serve e si
// riusano da un disegno all'altro.
#define VELA_BLUR_LEVELS 4
#define VELA_BLUR_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT

struct vela_blur_image {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    uint32_t width;
    uint32_t height;
};

// Un buffer dmabuf importato come destinazione di disegno (schermo, cursore,
// cattura). Vive quanto il buffer (wlr_addon) e sta in renderer->targets.
struct vela_target {
    struct vela_renderer *renderer;
    struct wlr_addon addon;
    struct wl_list link;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFormat format; // della vista: _SRGB, la GPU codifica scrivendo
    int dmabuf_fd; // piano 0, per la sincronizzazione implicita (non nostro)
    uint32_t width;
    uint32_t height;
    bool initialized; // il contenuto va conservato tra un disegno e l'altro
    bool sampleable; // si può anche leggere come texture (la sfocatura legge ciò che c'è dietro)
};

// Una texture: il contenuto di un buffer pronto da leggere negli shader.
// - dmabuf: importato così com'è, senza copie; una sola importazione per
//   buffer (wlr_addon), finché wlroots usa la texture.
// - wl_shm e simili: copiati in un'immagine nostra; quando l'app aggiorna,
//   si ricopia solo la parte cambiata.
// wlr_texture come primo membro: da wlr_texture* si risale qui.
struct vela_texture {
    struct wlr_texture base;
    struct vela_renderer *renderer; // NULL: il renderer è già chiuso
    struct wl_list link; // renderer->textures
    const struct vela_pixel_format *format;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;

    // dmabuf importato: il buffer a cui è legata (bloccato finché la
    // texture esiste) e il suo descrittore del piano 0, per la
    // sincronizzazione implicita.
    struct wlr_buffer *buffer;
    struct wlr_addon addon;
    int dmabuf_fd;
    bool foreign; // a ogni uso: presa in carico dalla coda "foreign"

    bool uploaded; // immagine nostra: il contenuto c'è
    int refs; // riferimenti di wlroots (texture_from_buffer / destroy)
};

struct vela_texture *vela_texture_from_wlr_texture(struct wlr_texture *texture);
struct wlr_texture *vela_texture_create(struct vela_renderer *renderer, struct wlr_buffer *buffer);
// Alla chiusura del renderer: libera subito le risorse Vulkan.
void vela_texture_release(struct vela_texture *texture);

// Un blocco di memoria visibile alla CPU per i caricamenti e le letture.
struct vela_staging {
    VkBuffer buffer;
    VkDeviceSize offset;
    uint8_t *data;
};

struct vela_target *vela_renderer_target(struct vela_renderer *renderer, struct wlr_buffer *buffer);
// Un command buffer libero, già in registrazione.
VkCommandBuffer vela_renderer_begin_commands(struct vela_renderer *renderer);
// I caricamenti dei buffer wl_shm si registrano qui e partono verso la GPU
// insieme al prossimo invio, prima di esso.
VkCommandBuffer vela_renderer_upload_commands(struct vela_renderer *renderer);
bool vela_renderer_stage(struct vela_renderer *renderer, VkDeviceSize size, struct vela_staging *out);
// Invia `cmd` (ancora in registrazione). wait_fds: sync_file da aspettare
// prima, che il renderer chiude. release_fd, se non NULL, riceve un
// sync_file che scatta a fine lavoro (-1 se non disponibile: allora la
// CPU ha già aspettato). Restituisce il punto della timeline, 0 se l'invio
// è fallito.
uint64_t vela_renderer_submit(struct vela_renderer *renderer, VkCommandBuffer cmd, const int *wait_fds,
    int wait_count, int *release_fd);
void vela_renderer_wait(struct vela_renderer *renderer, uint64_t point);
// Libera ciò che la GPU ha finito di usare (immagini ritirate, semafori,
// blocchi di appoggio in più).
void vela_renderer_collect(struct vela_renderer *renderer);
// Un'immagine da distruggere quando la GPU avrà finito tutto il lavoro
// inviato fin qui e quello in registrazione. view e memory possono essere
// VK_NULL_HANDLE.
void vela_renderer_retire_image(struct vela_renderer *renderer, VkImage image, VkImageView view,
    VkDeviceMemory memory);
// Fa scattare il prossimo punto della timeline syncobj con `sync_file` (non
// lo chiude; -1: il lavoro è già finito). Restituisce il punto, 0 se non
// c'è la timeline.
uint64_t vela_renderer_signal_sync_point(struct vela_renderer *renderer, int sync_file);

// Pipeline: una per (formato di destinazione, tipo, fusione).
VkPipeline vela_renderer_pipeline(struct vela_renderer *renderer, VkFormat target, enum vela_pipeline_kind kind,
    bool blend);
VkPipelineLayout vela_renderer_layout(const struct vela_renderer *renderer);
VkSampler vela_renderer_sampler(const struct vela_renderer *renderer, bool linear);

// Livelli abbastanza grandi per sfocare una zona di width x height pixel.
bool vela_renderer_prepare_blur(struct vela_renderer *renderer, uint32_t width, uint32_t height);
const struct vela_blur_image *vela_renderer_blur_image(const struct vela_renderer *renderer, int level);

// Uno slot di misura per il prossimo command buffer; -1 se non ci sono i
// timestamp o gli slot sono tutti in uso.
int vela_renderer_timing_slot(struct vela_renderer *renderer);
void vela_renderer_write_timestamp(struct vela_renderer *renderer, VkCommandBuffer cmd, int slot, bool end);
void vela_renderer_timing_submitted(struct vela_renderer *renderer, int slot, uint64_t point);

struct vela_vulkan *vela_renderer_vulkan(struct vela_renderer *renderer);
void vela_renderer_track_texture(struct vela_renderer *renderer, struct vela_texture *texture);

#endif

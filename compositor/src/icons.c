// SPDX-FileCopyrightText: 2026 Brando Giuffrida
// SPDX-License-Identifier: GPL-3.0-or-later

#include "icons.h"

#include "util.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wlr/util/log.h>

#if VELA_HAVE_RSVG
#include <cairo.h>
#include <librsvg/rsvg.h>
#endif

// Un elenco di stringhe allocate.
struct strings {
    char **items;
    int count;
    int capacity;
};

static void strings_push(struct strings *list, const char *text)
{
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 16;
        list->items = realloc(list->items, (size_t)list->capacity * sizeof(*list->items));
    }
    list->items[list->count++] = strdup(text);
}

static bool strings_contain(const struct strings *list, const char *text)
{
    for (int i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i], text) == 0) {
            return true;
        }
    }
    return false;
}

static void strings_free(struct strings *list)
{
    for (int i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

// Le voci non vuote di "a,b,c" (o con un altro separatore).
static void strings_split(struct strings *list, const char *text, char separator)
{
    while (*text) {
        const char *end = strchr(text, separator);
        size_t length = end ? (size_t)(end - text) : strlen(text);
        if (length > 0) {
            char *item = strndup(text, length);
            strings_push(list, item);
            free(item);
        }
        text += length + (end ? 1 : 0);
    }
}

// Un'associazione nome -> icona, dai file .desktop.
struct icon_name {
    char *key; // minuscolo
    char *icon;
};

struct icon_names {
    struct icon_name *items;
    int count;
    int capacity;
};

static const char *icon_names_find(const struct icon_names *names, const char *key)
{
    for (int i = 0; i < names->count; ++i) {
        if (strcmp(names->items[i].key, key) == 0) {
            return names->items[i].icon;
        }
    }
    return NULL;
}

// Il primo che si trova vince (le cartelle vanno dalla più importante).
static void icon_names_add(struct icon_names *names, const char *key, const char *icon)
{
    if (icon_names_find(names, key)) {
        return;
    }
    if (names->count == names->capacity) {
        names->capacity = names->capacity ? names->capacity * 2 : 128;
        names->items = realloc(names->items, (size_t)names->capacity * sizeof(*names->items));
    }
    names->items[names->count].key = strdup(key);
    names->items[names->count].icon = strdup(icon);
    ++names->count;
}

static void icon_names_free(struct icon_names *names)
{
    for (int i = 0; i < names->count; ++i) {
        free(names->items[i].key);
        free(names->items[i].icon);
    }
    free(names->items);
}

// Un'icona già disegnata (o cercata senza successo), per app e misura.
struct cached_icon {
    char *app_id;
    int size;
    struct vela_image image;
    struct cached_icon *next;
};

struct vela_icons {
    struct strings theme_chain; // il tema, quelli che eredita, hicolor
    struct strings icon_dirs; // .../icons
    bool desktop_loaded;
    struct icon_names by_id; // id del .desktop (senza estensione) -> Icon
    struct icon_names by_class; // StartupWMClass -> Icon
    struct cached_icon *cache;
};

static void lowercase(char *text)
{
    for (; *text; ++text) {
        *text = (char)tolower((unsigned char)*text);
    }
}

static bool exists(const char *path)
{
    return access(path, F_OK) == 0;
}

// Le cartelle dati XDG, dalla più importante.
static void data_dirs(struct strings *dirs)
{
    const char *home = getenv("HOME");
    const char *data_home = getenv("XDG_DATA_HOME");
    char path[PATH_MAX];
    if (data_home && *data_home) {
        strings_push(dirs, data_home);
    } else if (home) {
        snprintf(path, sizeof(path), "%s/.local/share", home);
        strings_push(dirs, path);
    }
    const char *system = getenv("XDG_DATA_DIRS");
    strings_split(dirs, system && *system ? system : "/usr/local/share:/usr/share", ':');
}

// I valori di alcune chiavi in una sezione [section] di un file .ini o
// .desktop (a parità di chiave vince l'ultima riga). values[i] resta vuoto
// se la chiave manca.
static void read_section(const char *path, const char *section, const char *const *keys, char (*values)[256],
    int count)
{
    for (int i = 0; i < count; ++i) {
        values[i][0] = '\0';
    }
    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }
    size_t section_length = strlen(section);
    bool inside = false;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t n;
    while ((n = getline(&line, &capacity, file)) >= 0) {
        if (n > 0 && line[n - 1] == '\n') {
            line[--n] = '\0';
        }
        if (line[0] == '[') {
            inside = (size_t)n == section_length + 2 && strncmp(line + 1, section, section_length) == 0
                && line[n - 1] == ']';
            continue;
        }
        char *eq = inside ? strchr(line, '=') : NULL;
        if (!eq) {
            continue;
        }
        *eq = '\0';
        for (int i = 0; i < count; ++i) {
            if (strcmp(line, keys[i]) == 0) {
                snprintf(values[i], sizeof(values[i]), "%s", eq + 1);
            }
        }
    }
    free(line);
    fclose(file);
}

static void kdeglobals_path(char *out, size_t size)
{
    const char *config = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (config && *config) {
        snprintf(out, size, "%s/kdeglobals", config);
    } else if (home) {
        snprintf(out, size, "%s/.config/kdeglobals", home);
    } else {
        out[0] = '\0';
    }
}

struct vela_icons *vela_icons_create(void)
{
    struct vela_icons *icons = calloc(1, sizeof(*icons));
    struct strings dirs = { 0 };
    data_dirs(&dirs);
    char path[PATH_MAX];
    for (int i = 0; i < dirs.count; ++i) {
        snprintf(path, sizeof(path), "%s/icons", dirs.items[i]);
        strings_push(&icons->icon_dirs, path);
    }
    strings_free(&dirs);

    // Il tema scelto in KDE ([Icons] Theme in kdeglobals), altrimenti Breeze.
    static const char *const theme_key[] = { "Theme" };
    char theme[1][256];
    kdeglobals_path(path, sizeof(path));
    read_section(path, "Icons", theme_key, theme, 1);
    // La catena: il tema, quelli che eredita (Inherits), infine hicolor.
    struct strings pending = { 0 };
    strings_push(&pending, theme[0][0] ? theme[0] : "breeze");
    for (int next = 0; next < pending.count; ++next) {
        const char *name = pending.items[next];
        if (strings_contain(&icons->theme_chain, name)) {
            continue;
        }
        strings_push(&icons->theme_chain, name);
        for (int i = 0; i < icons->icon_dirs.count; ++i) {
            snprintf(path, sizeof(path), "%s/%s/index.theme", icons->icon_dirs.items[i], name);
            if (exists(path)) {
                static const char *const inherits_key[] = { "Inherits" };
                char inherits[1][256];
                read_section(path, "Icon Theme", inherits_key, inherits, 1);
                strings_split(&pending, inherits[0], ',');
                break;
            }
        }
    }
    strings_free(&pending);
    if (!strings_contain(&icons->theme_chain, "hicolor")) {
        strings_push(&icons->theme_chain, "hicolor");
    }
    return icons;
}

void vela_icons_destroy(struct vela_icons *icons)
{
    if (!icons) {
        return;
    }
    strings_free(&icons->theme_chain);
    strings_free(&icons->icon_dirs);
    icon_names_free(&icons->by_id);
    icon_names_free(&icons->by_class);
    struct cached_icon *entry = icons->cache;
    while (entry) {
        struct cached_icon *next = entry->next;
        free(entry->app_id);
        vela_image_finish(&entry->image);
        free(entry);
        entry = next;
    }
    free(icons);
}

// I .desktop sotto `dir` (relativa: il pezzo dell'id dopo `apps`), in
// profondità. Le cartelle collegate non si seguono.
static void scan_applications(struct vela_icons *icons, const char *apps, const char *relative)
{
    char dir_path[PATH_MAX];
    snprintf(dir_path, sizeof(dir_path), "%s%s%s", apps, *relative ? "/" : "", relative);
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char child[PATH_MAX];
        char path[PATH_MAX];
        struct stat link_info;
        if (!vela_format(child, sizeof(child), "%s%s%s", relative, *relative ? "/" : "", entry->d_name)
            || !vela_format(path, sizeof(path), "%s/%s", apps, child) || lstat(path, &link_info) != 0) {
            continue;
        }
        if (S_ISDIR(link_info.st_mode)) {
            scan_applications(icons, apps, child);
            continue;
        }
        struct stat info;
        size_t length = strlen(child);
        if (stat(path, &info) != 0 || !S_ISREG(info.st_mode) || length <= 8
            || strcmp(child + length - 8, ".desktop") != 0) {
            continue;
        }
        static const char *const keys[] = { "Icon", "StartupWMClass" };
        char values[2][256];
        read_section(path, "Desktop Entry", keys, values, 2);
        if (!values[0][0]) {
            continue;
        }
        // L'id con le sottocartelle unite da '-' (specifica Desktop Entry).
        child[length - 8] = '\0';
        for (char *c = child; *c; ++c) {
            if (*c == '/') {
                *c = '-';
            }
        }
        lowercase(child);
        icon_names_add(&icons->by_id, child, values[0]);
        if (values[1][0]) {
            lowercase(values[1]);
            icon_names_add(&icons->by_class, values[1], values[0]);
        }
    }
    closedir(dir);
}

static void load_desktop_entries(struct vela_icons *icons)
{
    icons->desktop_loaded = true;
    struct strings dirs = { 0 };
    data_dirs(&dirs);
    for (int i = 0; i < dirs.count; ++i) {
        char apps[PATH_MAX];
        snprintf(apps, sizeof(apps), "%s/applications", dirs.items[i]);
        scan_applications(icons, apps, "");
    }
    strings_free(&dirs);
}

// Il nome dell'icona dell'app. Dalla regola più sicura: il nome del
// .desktop è l'app_id, poi la classe X11, poi l'ultima parte di un id a
// dominio inverso (il primo in ordine alfabetico); infine l'app_id stesso,
// perché spesso il tema ha un'icona col nome dell'app.
static const char *icon_name(struct vela_icons *icons, const char *app_id)
{
    if (!icons->desktop_loaded) {
        load_desktop_entries(icons);
    }
    char id[256];
    snprintf(id, sizeof(id), "%s", app_id);
    lowercase(id);
    const char *icon = icon_names_find(&icons->by_id, id);
    if (!icon) {
        icon = icon_names_find(&icons->by_class, id);
    }
    if (icon) {
        return icon;
    }
    const char *best_id = NULL;
    for (int i = 0; i < icons->by_id.count; ++i) {
        const char *desktop_id = icons->by_id.items[i].key;
        const char *dot = strrchr(desktop_id, '.');
        if (dot && strcmp(dot + 1, id) == 0 && (!best_id || strcmp(desktop_id, best_id) < 0)) {
            best_id = desktop_id;
            icon = icons->by_id.items[i].icon;
        }
    }
    return icon ? icon : app_id;
}

// Le cartelle di un tema di icone (index.theme): nome, dimensione, se
// scalabile. Directories dice quali contano, ogni [cartella] le misure.
struct theme_dir {
    char name[256];
    int size;
    bool scalable;
};

static int theme_dirs(const char *index, struct theme_dir **out)
{
    *out = NULL;
    FILE *file = fopen(index, "r");
    if (!file) {
        return 0;
    }
    struct strings listed = { 0 };
    struct theme_dir *dirs = NULL; // tutte le sezioni, poi solo quelle elencate
    int count = 0;
    int capacity = 0;
    int current = -1; // la sezione in corso; -1: [Icon Theme] o nessuna
    bool in_theme = false;
    char *line = NULL;
    size_t line_capacity = 0;
    ssize_t n;
    while ((n = getline(&line, &line_capacity, file)) >= 0) {
        if (n > 0 && line[n - 1] == '\n') {
            line[--n] = '\0';
        }
        if (line[0] == '[') {
            in_theme = strcmp(line, "[Icon Theme]") == 0;
            current = -1;
            if (!in_theme && n >= 2 && line[n - 1] == ']') {
                line[n - 1] = '\0';
                for (int i = 0; i < count; ++i) {
                    if (strcmp(dirs[i].name, line + 1) == 0) {
                        current = i;
                    }
                }
                if (current < 0) {
                    if (count == capacity) {
                        capacity = capacity ? capacity * 2 : 64;
                        dirs = realloc(dirs, (size_t)capacity * sizeof(*dirs));
                    }
                    current = count++;
                    snprintf(dirs[current].name, sizeof(dirs[current].name), "%s", line + 1);
                    dirs[current].size = 0;
                    dirs[current].scalable = false;
                }
            }
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        const char *value = eq + 1;
        if (in_theme && strcmp(line, "Directories") == 0) {
            strings_free(&listed);
            strings_split(&listed, value, ',');
        } else if (current >= 0 && strcmp(line, "Size") == 0) {
            dirs[current].size = atoi(value);
        } else if (current >= 0 && strcmp(line, "Type") == 0) {
            dirs[current].scalable = strcmp(value, "Scalable") == 0;
        }
    }
    free(line);
    fclose(file);

    // Nell'ordine di Directories; una cartella senza sezione vale 0, Threshold.
    struct theme_dir *result = calloc((size_t)(listed.count ? listed.count : 1), sizeof(*result));
    for (int i = 0; i < listed.count; ++i) {
        snprintf(result[i].name, sizeof(result[i].name), "%s", listed.items[i]);
        for (int d = 0; d < count; ++d) {
            if (strcmp(dirs[d].name, listed.items[i]) == 0) {
                result[i].size = dirs[d].size;
                result[i].scalable = dirs[d].scalable;
            }
        }
    }
    int listed_count = listed.count;
    strings_free(&listed);
    free(dirs);
    *out = result;
    return listed_count;
}

// Il file dell'icona `name` per `size` pixel, in `out`; false se non c'è.
// Nel tema e in quelli che eredita: la cartella di dimensione più vicina;
// a parità, l'SVG (si disegna esatto a qualunque misura).
static bool find_file(struct vela_icons *icons, const char *name, int size, char *out, size_t out_size)
{
    if (!*name) {
        return false;
    }
    if (name[0] == '/') {
        snprintf(out, out_size, "%s", name);
        return exists(name);
    }
    char path[PATH_MAX];
    for (int t = 0; t < icons->theme_chain.count; ++t) {
        const char *theme = icons->theme_chain.items[t];
        bool found = false;
        int best_score = INT_MAX;
        for (int b = 0; b < icons->icon_dirs.count; ++b) {
            char root[PATH_MAX];
            if (!vela_format(root, sizeof(root), "%s/%s", icons->icon_dirs.items[b], theme)
                || !vela_format(path, sizeof(path), "%s/index.theme", root) || !exists(path)) {
                continue;
            }
            struct theme_dir *dirs = NULL;
            int count = theme_dirs(path, &dirs);
            for (int d = 0; d < count; ++d) {
                static const char *const extensions[] = { ".svg", ".png" };
                for (int e = 0; e < 2; ++e) {
                    if (!vela_format(path, sizeof(path), "%s/%s/%s%s", root, dirs[d].name, name, extensions[e])
                        || !exists(path)) {
                        continue;
                    }
                    int score = dirs[d].scalable ? 0 : abs(dirs[d].size - size) * 2;
                    if (dirs[d].size < size && !dirs[d].scalable) {
                        score += 1000; // ingrandire una PNG piccola: ultima scelta
                    }
                    score = e == 0 ? (score > 0 ? score - 1 : 0) : score + 1;
                    if (score < best_score) {
                        best_score = score;
                        snprintf(out, out_size, "%s", path);
                        found = true;
                    }
                }
            }
            free(dirs);
        }
        if (found) {
            return true;
        }
    }
    static const char *const pixmaps[] = { ".svg", ".png" };
    for (int e = 0; e < 2; ++e) {
        snprintf(path, sizeof(path), "/usr/share/pixmaps/%s%s", name, pixmaps[e]);
        if (exists(path)) {
            snprintf(out, out_size, "%s", path);
            return true;
        }
    }
    return false;
}

static void render(const char *path, int size, struct vela_image *image)
{
#if VELA_HAVE_RSVG
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    cairo_t *cr = cairo_create(surface);
    bool ok = false;
    size_t length = strlen(path);
    if (length > 4 && strcmp(path + length - 4, ".svg") == 0) {
        GError *error = NULL;
        RsvgHandle *handle = rsvg_handle_new_from_file(path, &error);
        if (handle) {
            RsvgRectangle viewport = { 0, 0, (double)size, (double)size };
            ok = rsvg_handle_render_document(handle, cr, &viewport, &error);
            g_object_unref(handle);
        }
        if (error) {
            g_error_free(error);
        }
    } else {
        cairo_surface_t *png = cairo_image_surface_create_from_png(path);
        if (cairo_surface_status(png) == CAIRO_STATUS_SUCCESS) {
            int w = cairo_image_surface_get_width(png);
            int h = cairo_image_surface_get_height(png);
            double scale = (double)size / (w > h ? w : h);
            cairo_translate(cr, (size - w * scale) / 2.0, (size - h * scale) / 2.0);
            cairo_scale(cr, scale, scale);
            cairo_set_source_surface(cr, png, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
            cairo_paint(cr);
            ok = true;
        }
        cairo_surface_destroy(png);
    }
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    if (ok) {
        // Cairo ARGB32 è già ARGB8888 premoltiplicato, riga per riga.
        int stride = cairo_image_surface_get_stride(surface);
        const unsigned char *data = cairo_image_surface_get_data(surface);
        image->width = size;
        image->height = size;
        image->pixels = malloc((size_t)size * (size_t)size * 4);
        for (int y = 0; y < size; ++y) {
            memcpy(&image->pixels[(size_t)y * (size_t)size], data + (size_t)y * (size_t)stride, (size_t)size * 4);
        }
    }
    cairo_surface_destroy(surface);
#else
    (void)path;
    (void)size;
    (void)image;
#endif
}

const struct vela_image *vela_icons_app(struct vela_icons *icons, const char *app_id, int size)
{
    for (struct cached_icon *entry = icons->cache; entry; entry = entry->next) {
        if (entry->size == size && strcmp(entry->app_id, app_id) == 0) {
            return &entry->image;
        }
    }
    struct cached_icon *entry = calloc(1, sizeof(*entry));
    entry->app_id = strdup(app_id);
    entry->size = size;
    char path[PATH_MAX];
    if (find_file(icons, icon_name(icons, app_id), size, path, sizeof(path))) {
        render(path, size, &entry->image);
    }
    if (!entry->image.pixels) {
        wlr_log(WLR_DEBUG, "Icons: no icon for %s", app_id);
    }
    entry->next = icons->cache;
    icons->cache = entry;
    return &entry->image;
}

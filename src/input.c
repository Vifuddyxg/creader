#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include "ui.h"
#include "input.h"
#include "pdf.h"
#include "library.h"
#include "filepick.h"
#include "storage.h"
#include "state.h"

#define SCROLL_STEP 48
#define ZOOM_STEP   0.05f
#define ZOOM_MIN    0.2f
#define ZOOM_MAX    2.0f

#define READER_BACK_W   68
#define READER_BACK_H   30
#define READER_BTN_X    3
#define READER_BTN_W    (READER_SIDEBAR_W - 6)
#define READER_BTN_H    34
#define READER_ZOOM_X   (READER_SIDEBAR_W / 2)
#define READER_ZOOM_TOP 80

typedef struct {
    int idx;
    int y_rel;
    int col;
} LibCardPos;

/* ---- helpers ------------------------------------------------------------- */
static void get_timestamp(char *buf, int len)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", tm);
}

/* Extract base filename without extension */
static void make_title(const char *path, char *title, int len)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    strncpy(title, base, len - 1);
    title[len - 1] = '\0';
    /* strip extension */
    char *dot = strrchr(title, '.');
    if (dot) *dot = '\0';
}

/* Sync current state back into the library item */
static void sync_progress(InputCtx *ctx)
{
    if (!ctx->state->has_document) return;

    LibraryItem item;
    memset(&item, 0, sizeof(item));
    snprintf(item.path,  sizeof(item.path),  "%s", ctx->state->path);
    snprintf(item.title, sizeof(item.title), "%s", ctx->state->title);
    item.last_page  = ctx->state->current_page;
    item.page_count = ctx->state->page_count;
    item.zoom       = ctx->state->zoom;
    item.rotation   = ctx->state->rotation;
    item.scroll_y   = ctx->state->scroll_y;
    item.view_mode  = ctx->state->view_mode;
    get_timestamp(item.last_opened, sizeof(item.last_opened));

    int idx = library_find(ctx->library, item.path);
    if (idx >= 0) {
        library_update(ctx->library, idx, &item);
    } else {
        library_add(ctx->library, &item);
    }
    storage_save(ctx->library);
}

static void clamp_zoom(float *zoom)
{
    if (*zoom < ZOOM_MIN) *zoom = ZOOM_MIN;
    if (*zoom > ZOOM_MAX) *zoom = ZOOM_MAX;
}

static float fit_width_zoom(PDFDoc *doc, const AppState *s)
{
    int pw = 0, ph = 0;
    pdf_get_page_size(doc, 0, 1.0f, s->rotation, &pw, &ph);
    (void)ph;
    if (pw <= 0) return 1.0f;
    float zoom = (float)(s->window_width - READER_SIDEBAR_W - STATUS_W - 80) / (float)pw;
    clamp_zoom(&zoom);
    return zoom;
}

static int reader_visible_h(const AppState *s)
{
    int h = s->window_height - STATUS_H;
    return h < 1 ? 1 : h;
}

static int reader_max_scroll(InputCtx *ctx)
{
    AppState *s = ctx->state;
    if (!*ctx->doc) return 0;

    if (s->view_mode == MODE_SINGLE_PAGE) {
        int total_h = READER_PAGE_MARGIN;
        for (int i = 0; i < s->page_count; i++) {
            int pw = 0, ph = 0;
            pdf_get_page_size(*ctx->doc, i, s->zoom, s->rotation, &pw, &ph);
            (void)pw;
            total_h += ph + READER_PAGE_GAP;
        }
        if (s->page_count > 0) total_h -= READER_PAGE_GAP;
        total_h += READER_PAGE_MARGIN;

        int max_scroll = total_h - reader_visible_h(s);
        return max_scroll > 0 ? max_scroll : 0;
    }

    int pw1 = 0, ph1 = 0, pw2 = 0, ph2 = 0;
    pdf_get_page_size(*ctx->doc, s->current_page, s->zoom, 0, &pw1, &ph1);
    if (s->current_page + 1 < s->page_count)
        pdf_get_page_size(*ctx->doc, s->current_page + 1, s->zoom, 0, &pw2, &ph2);
    (void)pw1;
    (void)pw2;

    int page_h = ph1 > ph2 ? ph1 : ph2;
    int max_scroll = page_h - reader_visible_h(s) + 40;
    return max_scroll > 0 ? max_scroll : 0;
}

static void reader_update_current_page(InputCtx *ctx)
{
    AppState *s = ctx->state;
    if (!*ctx->doc || s->view_mode != MODE_SINGLE_PAGE) return;

    int viewport_mid = s->scroll_y + reader_visible_h(s) / 2;
    int y = READER_PAGE_MARGIN;
    int best_page = s->current_page;
    int best_dist = 0x7fffffff;

    for (int i = 0; i < s->page_count; i++) {
        int pw = 0, ph = 0;
        pdf_get_page_size(*ctx->doc, i, s->zoom, s->rotation, &pw, &ph);
        (void)pw;
        int page_mid = y + ph / 2;
        int dist = abs(page_mid - viewport_mid);
        if (dist < best_dist) {
            best_dist = dist;
            best_page = i;
        }
        y += ph + READER_PAGE_GAP;
    }

    if (best_page != s->current_page) {
        s->current_page = best_page;
        ctx->page_dirty = true;
        sync_progress(ctx);
    }
}

static void set_zoom(InputCtx *ctx, float zoom)
{
    clamp_zoom(&zoom);
    if (ctx->state->zoom == zoom) return;
    ctx->state->zoom = zoom;
    ctx->state->scroll_y = 0;
    ctx->page_dirty = true;
    sync_progress(ctx);
}

static int reader_track_bottom(const AppState *s)
{
    int bot = s->window_height - STATUS_H - 156;
    if (bot < READER_ZOOM_TOP + 120) bot = READER_ZOOM_TOP + 120;
    return bot;
}

static int page_track_bottom(const AppState *s)
{
    int wh = s->window_height - STATUS_H;
    int h = wh / 2;
    if (h < 160) h = 160;
    if (h > 360) h = 360;
    return (wh - h) / 2 + h;
}

static int page_track_top(const AppState *s)
{
    int wh = s->window_height - STATUS_H;
    int h = wh / 2;
    if (h < 160) h = 160;
    if (h > 360) h = 360;
    return (wh - h) / 2;
}

static void set_page_from_mouse(InputCtx *ctx, int y)
{
    AppState *s = ctx->state;
    if (s->page_count <= 0) return;

    int top = page_track_top(s);
    int bot = page_track_bottom(s);
    float t = (float)(y - top) / (float)(bot - top);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    int page = (int)(t * (float)(s->page_count - 1) + 0.5f);
    if (page < 0) page = 0;
    if (page >= s->page_count) page = s->page_count - 1;
    if (page == s->current_page && s->view_mode != MODE_SINGLE_PAGE) return;

    s->current_page = page;
    s->scroll_y = 0;
    if (s->view_mode == MODE_SINGLE_PAGE && *ctx->doc) {
        for (int i = 0; i < page; i++) {
            int pw = 0, ph = 0;
            pdf_get_page_size(*ctx->doc, i, s->zoom, s->rotation, &pw, &ph);
            (void)pw;
            s->scroll_y += ph + READER_PAGE_GAP;
        }
    }
    ctx->page_dirty = true;
    sync_progress(ctx);
}

static void set_zoom_from_mouse(InputCtx *ctx, int y)
{
    int track_bot = reader_track_bottom(ctx->state);
    int track_h = track_bot - READER_ZOOM_TOP;
    float t = (float)(track_bot - y) / (float)track_h;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    set_zoom(ctx, ZOOM_MIN + t * (ZOOM_MAX - ZOOM_MIN));
}

static bool hit_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
}

static void reader_scroll_pages(InputCtx *ctx, int delta)
{
    AppState *s = ctx->state;
    if (s->view_mode != MODE_SINGLE_PAGE) {
        s->scroll_y += delta;
        if (s->scroll_y < 0) s->scroll_y = 0;
        int max_scroll = reader_max_scroll(ctx);
        if (s->scroll_y > max_scroll) s->scroll_y = max_scroll;
        return;
    }

    int max_scroll = reader_max_scroll(ctx);
    int next_scroll = s->scroll_y + delta;

    s->scroll_y = next_scroll;
    if (s->scroll_y < 0) s->scroll_y = 0;
    if (s->scroll_y > max_scroll) s->scroll_y = max_scroll;
    reader_update_current_page(ctx);
}

/* Open a PDF and load progress from library */
static void open_document(InputCtx *ctx, const char *path)
{
    /* Close existing doc */
    if (*ctx->doc) {
        pdf_close(*ctx->doc);
        *ctx->doc = NULL;
    }

    PDFDoc *d = pdf_open(path);
    if (!d) {
        state_set_message(ctx->state, "Failed to open file");
        return;
    }

    *ctx->doc = d;
    strncpy(ctx->state->path, path, MAX_PATH_LEN - 1);
    make_title(path, ctx->state->title, MAX_TITLE_LEN);
    ctx->state->page_count   = d->page_count;
    ctx->state->has_document = true;
    ctx->state->screen_mode  = SCREEN_READER;

    /* Restore progress if available */
    int idx = library_find(ctx->library, path);
    if (idx >= 0) {
        const LibraryItem *it = &ctx->library->items[idx];
        ctx->state->current_page = it->last_page;
        ctx->state->zoom         = it->zoom > 0.0f ? it->zoom : 1.0f;
        ctx->state->rotation     = it->rotation;
        ctx->state->scroll_y     = it->scroll_y;
        ctx->state->view_mode    = it->view_mode;
        char msg[64];
        snprintf(msg, sizeof(msg), "Resumed at page %d", it->last_page + 1);
        state_set_message(ctx->state, msg);
    } else {
        ctx->state->current_page = 0;
        ctx->state->rotation     = 0;
        ctx->state->zoom         = fit_width_zoom(d, ctx->state);
        ctx->state->scroll_y     = 0;
        ctx->state->view_mode    = MODE_SINGLE_PAGE;

        /* Add to library */
        LibraryItem item;
        memset(&item, 0, sizeof(item));
        snprintf(item.path,  sizeof(item.path),  "%s", path);
        snprintf(item.title, sizeof(item.title), "%s", ctx->state->title);
        item.page_count = d->page_count;
        get_timestamp(item.last_opened, sizeof(item.last_opened));
        library_add(ctx->library, &item);
        storage_save(ctx->library);
    }

    ctx->page_dirty = true;
}

/* ---- library keyboard/mouse handling ------------------------------------- */
static int lib_cols(int ww)
{
    int cols = (ww - GRID_LEFT * 2 + CARD_GAP) / (CARD_W + CARD_GAP);
    return cols < 1 ? 1 : cols;
}

static int lib_display_order(const Library *lib, int *indices)
{
    int count;
    if (lib->search_active && lib->search_len > 0) {
        count = library_filter(lib, lib->search_buf, indices, MAX_LIBRARY);
        library_sort_indices(lib, indices, count);
    } else {
        library_sorted_indices(lib, indices);
        count = lib->count;
    }
    return count;
}

static int lib_card_positions(const Library *lib, int cols,
                              LibCardPos *cards, int max_cards)
{
    int indices[MAX_LIBRARY];
    int count = lib_display_order(lib, indices);
    int n = 0;
    int y_rel = 0;
    int col = 0;
    const char *prev_cat = NULL;

    for (int s = 0; s < count && n < max_cards; s++) {
        int idx = indices[s];
        const char *cat = lib->items[idx].category;
        bool new_cat = prev_cat == NULL || strcmp(cat, prev_cat) != 0;

        if (new_cat) {
            if (col > 0) {
                y_rel += CARD_H + CARD_GAP;
                col = 0;
            }
            if (cat[0] != '\0') y_rel += CAT_HEADER_H + 6;
            prev_cat = cat;
        }

        cards[n].idx = idx;
        cards[n].y_rel = y_rel;
        cards[n].col = col;
        n++;

        col++;
        if (col >= cols) {
            y_rel += CARD_H + CARD_GAP;
            col = 0;
        }
    }

    return n;
}

static int lib_selected_pos(const LibCardPos *cards, int count, int selected)
{
    for (int i = 0; i < count; i++)
        if (cards[i].idx == selected) return i;
    return -1;
}

static int lib_nearest_vertical(const LibCardPos *cards, int count,
                                int current_pos, int dir)
{
    int best = current_pos;
    int best_score = 1000000;
    int cur_y = cards[current_pos].y_rel;
    int cur_col = cards[current_pos].col;

    for (int i = 0; i < count; i++) {
        int dy = cards[i].y_rel - cur_y;
        if ((dir > 0 && dy <= 0) || (dir < 0 && dy >= 0)) continue;
        int col_dist = abs(cards[i].col - cur_col);
        int score = abs(dy) * 16 + col_dist;
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }

    return best;
}

static bool readable_path_ext(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return false;
    return strcasecmp(ext, ".pdf") == 0 ||
           strcasecmp(ext, ".cbz") == 0 ||
           strcasecmp(ext, ".cbr") == 0 ||
           strcasecmp(ext, ".epub") == 0;
}

static int add_file_to_library(Library *lib, const char *path)
{
    if (!readable_path_ext(path)) return -1;

    LibraryItem item;
    memset(&item, 0, sizeof(item));
    snprintf(item.path, sizeof(item.path), "%s", path);
    make_title(path, item.title, sizeof(item.title));
    item.zoom = 1.0f;

    PDFDoc *tmp = pdf_open(path);
    if (tmp) {
        item.page_count = tmp->page_count;
        pdf_close(tmp);
    }

    return library_add(lib, &item);
}

static int add_path_to_library(InputCtx *ctx, const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    Library *lib = ctx->library;
    int added = 0;

    if (S_ISDIR(st.st_mode)) {
        added = library_add_directory(lib, path);
    } else if (S_ISREG(st.st_mode)) {
        int before = lib->count;
        int idx = add_file_to_library(lib, path);
        if (idx >= 0) {
            lib->selected = idx;
            added = lib->count > before ? 1 : 0;
        }
    }

    if (added > 0) {
        lib->thumbs_loaded = false;
        storage_save(lib);
    }

    return added;
}

static bool find_dir_named(const char *root, const char *needle,
                           int depth, int max_depth,
                           char *out, int out_len)
{
    if (depth > max_depth) return false;

    DIR *dir = opendir(root);
    if (!dir) return false;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char full[MAX_PATH_LEN];
        snprintf(full, sizeof(full), "%s/%s", root, entry->d_name);

        struct stat st;
        if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        if (strcasecmp(entry->d_name, needle) == 0) {
            snprintf(out, out_len, "%s", full);
            closedir(dir);
            return true;
        }

        if (find_dir_named(full, needle, depth + 1, max_depth, out, out_len)) {
            closedir(dir);
            return true;
        }
    }

    closedir(dir);
    return false;
}

static int add_directory_text(InputCtx *ctx, const char *text)
{
    char path[MAX_PATH_LEN];
    snprintf(path, sizeof(path), "%s", text);

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        const char *home = getenv("HOME");
        if (!home || !find_dir_named(home, text, 0, 4, path, sizeof(path)))
            return -1;
    }

    int added = library_add_directory(ctx->library, path);
    if (added > 0) {
        ctx->library->thumbs_loaded = false;
        storage_save(ctx->library);
    }
    return added;
}

static void lib_scroll_to_selected(Library *lib, int ww, int wh)
{
    if (lib->selected < 0) return;
    LibCardPos cards[MAX_LIBRARY];
    int count = lib_card_positions(lib, lib_cols(ww), cards, MAX_LIBRARY);
    int pos = lib_selected_pos(cards, count, lib->selected);
    if (pos < 0) return;

    int card_top    = GRID_TOP + cards[pos].y_rel - lib->scroll_y;
    int visible_bot = wh - STATUS_H;
    if (card_top < GRID_TOP)
        lib->scroll_y -= (GRID_TOP - card_top);
    if (card_top + CARD_H > visible_bot)
        lib->scroll_y += (card_top + CARD_H - visible_bot) + 4;
}

static void handle_library_key(InputCtx *ctx, SDL_Keycode key)
{
    AppState *s   = ctx->state;
    Library  *lib = ctx->library;
    int indices[MAX_LIBRARY];
    int display_count = lib_display_order(lib, indices);

    /* Text input for add-file dialog */
    if (lib->input_active) {
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (lib->input_len > 0) {
                int added = lib->input_dir_mode
                    ? add_directory_text(ctx, lib->input_buf)
                    : add_path_to_library(ctx, lib->input_buf);
                if (added > 0) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Added %d item%s",
                             added, added == 1 ? "" : "s");
                    state_set_message(s, msg);
                } else if (added == 0) {
                    state_set_message(s, "Added to library");
                } else {
                    state_set_message(s, "Path not found or unsupported");
                }
            }
            lib->input_active = false;
            lib->input_dir_mode = false;
            lib->input_len    = 0;
            lib->input_buf[0] = '\0';
            SDL_StopTextInput();
        } else if (key == SDLK_ESCAPE) {
            lib->input_active = false;
            lib->input_dir_mode = false;
            lib->input_len    = 0;
            lib->input_buf[0] = '\0';
            SDL_StopTextInput();
        } else if (key == SDLK_BACKSPACE && lib->input_len > 0) {
            lib->input_buf[--lib->input_len] = '\0';
        }
        return;
    }

    /* Text input for search */
    if (lib->search_active) {
        if (key == SDLK_RETURN || key == SDLK_ESCAPE || key == SDLK_SLASH) {
            lib->search_active = false;
            SDL_StopTextInput();
        } else if (key == SDLK_BACKSPACE && lib->search_len > 0) {
            lib->search_buf[--lib->search_len] = '\0';
        }
        return;
    }

    switch (key) {
    case SDLK_a:
        lib->input_active = true;
        lib->input_dir_mode = false;
        lib->input_len    = 0;
        lib->input_buf[0] = '\0';
        SDL_StartTextInput();
        break;

    case SDLK_d:
        lib->input_active = true;
        lib->input_dir_mode = true;
        lib->input_len = 0;
        lib->input_buf[0] = '\0';
        SDL_StartTextInput();
        break;

    case SDLK_SLASH:
        lib->search_active = true;
        lib->search_len    = 0;
        lib->search_buf[0] = '\0';
        SDL_StartTextInput();
        break;

    /* ---- file picker ---------------------------------------------------- */
    case SDLK_o: {
        char picked[MAX_PATH_LEN] = {0};
        if (filepick_file(picked, sizeof(picked))) {
            int added = add_path_to_library(ctx, picked);
            state_set_message(s, added >= 0 ? "Added to library"
                                             : "Unsupported file");
        } else if (!filepick_has_tool()) {
            state_set_message(s, "No dialog tool — install zenity or kdialog");
        }
        break;
    }

    /* ---- folder scanner ------------------------------------------------- */
    case SDLK_f: {
        char folder[MAX_PATH_LEN] = {0};
        if (filepick_folder(folder, sizeof(folder))) {
            int before = lib->count;
            int added  = library_add_directory(lib, folder);
            if (added > 0) {
                lib->thumbs_loaded = false;
                storage_save(lib);
                char msg[64];
                snprintf(msg, sizeof(msg), "Added %d file%s from folder",
                         added, added == 1 ? "" : "s");
                state_set_message(s, msg);
                /* select first newly added item */
                if (before < lib->count) lib->selected = before;
            } else {
                state_set_message(s, "No new PDFs found in folder");
            }
        } else if (!filepick_has_tool()) {
            state_set_message(s, "No dialog tool — install zenity or kdialog");
        }
        break;
    }

    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (lib->selected >= 0 && lib->selected < lib->count) {
            open_document(ctx, lib->items[lib->selected].path);
        }
        break;

    case SDLK_DELETE:
    case SDLK_x:
    case SDLK_BACKSPACE:
        if (lib->selected >= 0 && lib->selected < lib->count) {
            library_remove(lib, lib->selected);
            storage_save(lib);
            if (lib->selected >= lib->count) lib->selected = lib->count - 1;
            state_set_message(s, "Removed from library");
        }
        break;

    case SDLK_RIGHT:
        if (display_count > 0) {
            int pos = -1;
            for (int i = 0; i < display_count; i++)
                if (indices[i] == lib->selected) pos = i;
            if (pos < 0) lib->selected = indices[0];
            else if (pos < display_count - 1) lib->selected = indices[pos + 1];
        }
        lib_scroll_to_selected(lib, s->window_width, s->window_height);
        break;
    case SDLK_LEFT:
        if (display_count > 0) {
            int pos = -1;
            for (int i = 0; i < display_count; i++)
                if (indices[i] == lib->selected) pos = i;
            if (pos < 0) lib->selected = indices[0];
            else if (pos > 0) lib->selected = indices[pos - 1];
        }
        lib_scroll_to_selected(lib, s->window_width, s->window_height);
        break;
    case SDLK_DOWN: {
        LibCardPos cards[MAX_LIBRARY];
        int count = lib_card_positions(lib, lib_cols(s->window_width),
                                       cards, MAX_LIBRARY);
        int pos = lib_selected_pos(cards, count, lib->selected);
        if (pos < 0 && count > 0) lib->selected = cards[0].idx;
        else if (pos >= 0) lib->selected = cards[lib_nearest_vertical(cards, count, pos, 1)].idx;
        lib_scroll_to_selected(lib, s->window_width, s->window_height);
        break;
    }
    case SDLK_UP: {
        LibCardPos cards[MAX_LIBRARY];
        int count = lib_card_positions(lib, lib_cols(s->window_width),
                                       cards, MAX_LIBRARY);
        int pos = lib_selected_pos(cards, count, lib->selected);
        if (pos < 0 && count > 0) lib->selected = cards[0].idx;
        else if (pos >= 0) lib->selected = cards[lib_nearest_vertical(cards, count, pos, -1)].idx;
        lib_scroll_to_selected(lib, s->window_width, s->window_height);
        break;
    }

    case SDLK_b:
    case SDLK_ESCAPE:
        if (s->has_document) s->screen_mode = SCREEN_READER;
        break;

    case SDLK_q:
        s->running = false;
        break;
    default: break;
    }
}

/* ---- reader keyboard/mouse handling -------------------------------------- */
static void handle_reader_key(InputCtx *ctx, SDL_Keycode key)
{
    AppState *s   = ctx->state;
    PDFDoc  **doc = ctx->doc;

    switch (key) {
    /* Navigation */
    case SDLK_RIGHT:
    case SDLK_PAGEDOWN: {
        int step = (s->view_mode == MODE_TWO_PAGE) ? 2 : 1;
        int next = s->current_page + step;
        if (next < s->page_count) {
            s->current_page = next;
            s->scroll_y = 0;
            ctx->page_dirty = true;
            sync_progress(ctx);
        }
        break;
    }
    case SDLK_LEFT:
    case SDLK_PAGEUP: {
        int step = (s->view_mode == MODE_TWO_PAGE) ? 2 : 1;
        int prev = s->current_page - step;
        if (prev < 0) prev = 0;
        if (prev != s->current_page) {
            s->current_page = prev;
            s->scroll_y = 0;
            ctx->page_dirty = true;
            sync_progress(ctx);
        }
        break;
    }

    /* Scroll */
    case SDLK_DOWN:
        reader_scroll_pages(ctx, SCROLL_STEP);
        break;
    case SDLK_UP:
        reader_scroll_pages(ctx, -SCROLL_STEP);
        break;

    /* Zoom */
    case SDLK_EQUALS:
    case SDLK_PLUS:
    case SDLK_KP_PLUS: {
        float nz = s->zoom + ZOOM_STEP;
        set_zoom(ctx, nz);
        char msg[32]; snprintf(msg, sizeof(msg), "Zoom %d%%", (int)(nz*100));
        state_set_message(s, msg);
        break;
    }
    case SDLK_MINUS:
    case SDLK_KP_MINUS: {
        float nz = s->zoom - ZOOM_STEP;
        set_zoom(ctx, nz);
        char msg[32]; snprintf(msg, sizeof(msg), "Zoom %d%%", (int)(nz*100));
        state_set_message(s, msg);
        break;
    }
    case SDLK_0:
    case SDLK_KP_0:
        set_zoom(ctx, 1.0f);
        state_set_message(s, "Zoom reset");
        break;

    /* Rotation */
    case SDLK_r:
        s->rotation = (s->rotation + 90) % 360;
        s->scroll_y = 0;
        ctx->page_dirty = true;
        sync_progress(ctx);
        break;

    /* View mode */
    case SDLK_d:
        s->view_mode = (s->view_mode == MODE_SINGLE_PAGE) ? MODE_TWO_PAGE : MODE_SINGLE_PAGE;
        s->scroll_y  = 0;
        ctx->page_dirty = true;
        state_set_message(s, s->view_mode == MODE_TWO_PAGE ? "Two-Page Mode" : "Single Page Mode");
        sync_progress(ctx);
        break;

    /* Fullscreen */
    case SDLK_f:
        s->fullscreen = !s->fullscreen;
        SDL_SetWindowFullscreen(ctx->window,
                                s->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        break;

    /* Library */
    case SDLK_b:
        s->screen_mode = SCREEN_LIBRARY;
        ctx->library->thumbs_loaded = false; /* reload thumbs */
        break;

    case SDLK_q:
        sync_progress(ctx);
        s->running = false;
        break;

    default: break;
    }

    (void)doc;
}

/* ---- main event handler -------------------------------------------------- */
void input_handle(InputCtx *ctx, SDL_Event *e)
{
    AppState *s   = ctx->state;
    Library  *lib = ctx->library;

    switch (e->type) {
    case SDL_QUIT:
        sync_progress(ctx);
        s->running = false;
        break;

    case SDL_KEYDOWN:
        if (s->screen_mode == SCREEN_LIBRARY)
            handle_library_key(ctx, e->key.keysym.sym);
        else
            handle_reader_key(ctx, e->key.keysym.sym);
        break;

    case SDL_TEXTINPUT:
        if (s->screen_mode == SCREEN_LIBRARY) {
            if (lib->input_active) {
                int avail = MAX_PATH_LEN - lib->input_len - 1;
                if (avail > 0) {
                    snprintf(lib->input_buf + lib->input_len, avail + 1,
                             "%s", e->text.text);
                    lib->input_len = (int)strlen(lib->input_buf);
                }
            } else if (lib->search_active) {
                int avail = MAX_TITLE_LEN - lib->search_len - 1;
                if (avail > 0) {
                    snprintf(lib->search_buf + lib->search_len, avail + 1,
                             "%s", e->text.text);
                    lib->search_len = (int)strlen(lib->search_buf);
                }
            }
        }
        break;

    case SDL_MOUSEWHEEL:
        if (s->screen_mode == SCREEN_LIBRARY) {
            lib->scroll_y -= e->wheel.y * SCROLL_STEP;
            if (lib->scroll_y < 0) lib->scroll_y = 0;
        } else {
            reader_scroll_pages(ctx, e->wheel.y < 0 ? SCROLL_STEP : -SCROLL_STEP);
        }
        break;

    case SDL_MOUSEBUTTONDOWN:
        if (s->screen_mode == SCREEN_READER && e->button.button == SDL_BUTTON_LEFT) {
            int mx = e->button.x;
            int my = e->button.y;
            int wh = s->window_height - STATUS_H;
            int status_x = s->window_width - STATUS_W;
            int single_y = wh - 52;
            int book_y = wh - 94;
            int track_bot = reader_track_bottom(s);
            int page_bot = page_track_bottom(s);

            if (hit_rect(mx, my, status_x + 5, 10, STATUS_W - 10, READER_BACK_H)) {
                s->screen_mode = SCREEN_LIBRARY;
                ctx->library->thumbs_loaded = false;
            } else if (hit_rect(mx, my, status_x, page_track_top(s) - 16,
                                STATUS_W, page_bot - page_track_top(s) + 32)) {
                ctx->page_dragging = true;
                set_page_from_mouse(ctx, my);
            } else if (hit_rect(mx, my, READER_BTN_X, single_y,
                         READER_BTN_W, READER_BTN_H)) {
                s->view_mode = MODE_SINGLE_PAGE;
                s->scroll_y = 0;
                ctx->page_dirty = true;
                sync_progress(ctx);
            } else if (hit_rect(mx, my, READER_BTN_X, book_y,
                                READER_BTN_W, READER_BTN_H)) {
                s->view_mode = MODE_TWO_PAGE;
                s->scroll_y = 0;
                ctx->page_dirty = true;
                sync_progress(ctx);
            } else if (hit_rect(mx, my, READER_ZOOM_X - 18, READER_ZOOM_TOP - 12,
                                36, track_bot - READER_ZOOM_TOP + 24)) {
                ctx->zoom_dragging = true;
                set_zoom_from_mouse(ctx, my);
            }
        } else if (s->screen_mode == SCREEN_LIBRARY && e->button.button == SDL_BUTTON_LEFT) {
            int mx = e->button.x;
            int my = e->button.y;
            LibCardPos cards[MAX_LIBRARY];
            int count = lib_card_positions(lib, lib_cols(s->window_width),
                                           cards, MAX_LIBRARY);
            for (int i = 0; i < count; i++) {
                int idx = cards[i].idx;
                int cx = GRID_LEFT + cards[i].col * (CARD_W + CARD_GAP);
                int cy = GRID_TOP  + cards[i].y_rel - lib->scroll_y;
                if (mx >= cx && mx <= cx + CARD_W && my >= cy && my <= cy + CARD_H) {
                    if (lib->selected == idx && e->button.clicks == 2) {
                        /* double click = open */
                        open_document(ctx, lib->items[idx].path);
                    } else {
                        lib->selected = idx;
                    }
                    break;
                }
            }
        }
        break;

    case SDL_MOUSEBUTTONUP:
        if (e->button.button == SDL_BUTTON_LEFT) {
            ctx->zoom_dragging = false;
            ctx->page_dragging = false;
        }
        break;

    case SDL_MOUSEMOTION:
        if (ctx->zoom_dragging)
            set_zoom_from_mouse(ctx, e->motion.y);
        else if (ctx->page_dragging)
            set_page_from_mouse(ctx, e->motion.y);
        break;

    case SDL_DROPFILE:
        if (s->screen_mode == SCREEN_LIBRARY && e->drop.file) {
            int added = add_path_to_library(ctx, e->drop.file);
            if (added > 0) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Added %d item%s",
                         added, added == 1 ? "" : "s");
                state_set_message(s, msg);
            } else if (added == 0) {
                state_set_message(s, "Already in library");
            } else {
                state_set_message(s, "Drop a PDF, EPUB, CBZ, CBR, or folder");
            }
            SDL_free(e->drop.file);
        }
        break;

    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_RESIZED ||
            e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            s->window_width  = e->window.data1;
            s->window_height = e->window.data2;
            ctx->page_dirty  = true;
        }
        break;

    default: break;
    }
}

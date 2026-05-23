#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include "state.h"
#include "pdf.h"
#include "ui.h"
#include "input.h"
#include "library.h"
#include "storage.h"

#define WINDOW_TITLE "creader"
#define WIN_W        1280
#define WIN_H        800
#define FPS          60

static int clamp_page_index(int page, int page_count)
{
    if (page < 0) return 0;
    if (page >= page_count) return page_count > 0 ? page_count - 1 : 0;
    return page;
}

static int estimate_page_height(PDFDoc *doc, const AppState *s)
{
    int pw = 0, ph = 0;
    if (doc && s->page_count > 0)
        pdf_get_page_size(doc, clamp_page_index(s->current_page, s->page_count),
                          s->zoom, s->rotation, &pw, &ph);
    (void)pw;
    if (ph <= 0)
        ph = s->window_height - STATUS_H - READER_PAGE_MARGIN * 2;
    if (ph < 100) ph = 100;
    return ph;
}

static int continuous_max_scroll(const AppState *s, const int *page_h, int count)
{
    if (!page_h || count <= 0) return 0;
    long long total_h = (long long)READER_PAGE_MARGIN * 2;
    for (int i = 0; i < count; i++)
        total_h += page_h[i] + READER_PAGE_GAP;
    total_h -= READER_PAGE_GAP;
    long long max_scroll = total_h - (s->window_height - STATUS_H);
    if (max_scroll < 0) return 0;
    if (max_scroll > 0x7fffffffLL) return 0x7fffffff;
    return (int)max_scroll;
}

/* Compute fit-to-width zoom for the document viewport. */
static float fit_zoom(int page_w_pts, int page_h_pts,
                      int win_w, int win_h)
{
    (void)page_h_pts;
    (void)win_h;
    return (float)(win_w - READER_SIDEBAR_W - STATUS_W - 80) / (float)page_w_pts;
}

/* Apply fit-to-window zoom based on current page size at 1x */
static void auto_fit(AppState *s, PDFDoc *doc)
{
    if (!doc) return;
    int pw, ph;
    pdf_get_page_size(doc, s->current_page, 1.0f, s->rotation, &pw, &ph);
    if (pw <= 0 || ph <= 0) return;
    s->zoom = fit_zoom(pw, ph, s->window_width, s->window_height);
    if (s->zoom < 0.2f) s->zoom = 0.2f;
    if (s->zoom > 2.0f) s->zoom = 2.0f;
}

static void free_page_cache(SDL_Texture ***pages, int **page_w, int **page_h,
                            int *cached_pages)
{
    if (*pages) {
        for (int i = 0; i < *cached_pages; i++)
            if ((*pages)[i]) SDL_DestroyTexture((*pages)[i]);
        free(*pages);
    }
    free(*page_w);
    free(*page_h);
    *pages = NULL;
    *page_w = NULL;
    *page_h = NULL;
    *cached_pages = 0;
}

int main(int argc, char *argv[])
{
    /* ---- SDL init -------------------------------------------------------- */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        WINDOW_TITLE,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!renderer) {
        /* Fallback to software renderer */
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer) {
            fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    /* ---- UI ---- */
    UICtx *ui = ui_init();
    if (!ui) {
        fprintf(stderr, "ui_init failed (no TTF fonts found)\n");
        /* continue without text — not fatal */
    }

    /* ---- State & Library ------------------------------------------------ */
    AppState state;
    state_init(&state);
    SDL_GetWindowSize(window, &state.window_width, &state.window_height);

    Library library;
    library_init(&library);
    storage_ensure_dir();
    storage_load(&library);

    PDFDoc *doc = NULL;

    /* ---- Input context -------------------------------------------------- */
    InputCtx ictx = {
        .window   = window,
        .renderer = renderer,
        .state    = &state,
        .library  = &library,
        .doc      = &doc,
        .page_dirty = false
    };

    /* ---- Open file from command line ------------------------------------ */
    if (argc >= 2) {
        const char *path = argv[1];
        PDFDoc *d = pdf_open(path);
        if (d) {
            doc = d;
            strncpy(state.path,  path, MAX_PATH_LEN - 1);
            /* title = basename without extension */
            const char *base = strrchr(path, '/');
            base = base ? base + 1 : path;
            strncpy(state.title, base, MAX_TITLE_LEN - 1);
            char *dot = strrchr(state.title, '.');
            if (dot) *dot = '\0';

            state.page_count   = d->page_count;
            state.has_document = true;
            state.screen_mode  = SCREEN_READER;

            /* Restore progress */
            int idx = library_find(&library, path);
            if (idx >= 0) {
                const LibraryItem *it = &library.items[idx];
                state.current_page = it->last_page;
                state.zoom         = it->zoom > 0.0f ? it->zoom : 1.0f;
                state.rotation     = it->rotation;
                state.scroll_y     = it->scroll_y;
                state.view_mode    = it->view_mode;
                char msg[64];
                snprintf(msg, sizeof(msg), "Resumed at page %d", it->last_page + 1);
                state_set_message(&state, msg);
            } else {
                auto_fit(&state, doc);
                LibraryItem item;
                memset(&item, 0, sizeof(item));
                snprintf(item.path,  sizeof(item.path),  "%s", state.path);
                snprintf(item.title, sizeof(item.title), "%s", state.title);
                item.page_count = d->page_count;
                library_add(&library, &item);
                storage_save(&library);
            }
            ictx.page_dirty = true;
        } else {
            fprintf(stderr, "Cannot open: %s\n", path);
        }
    }

    /* Start in library view if no document */
    if (!state.has_document) {
        state.screen_mode = SCREEN_LIBRARY;
    }

    /* ---- Page texture cache --------------------------------------------- */
    SDL_Texture *tex1 = NULL;  int t1w = 0, t1h = 0;
    SDL_Texture *tex2 = NULL;  int t2w = 0, t2h = 0;
    SDL_Texture **pages = NULL;
    int *page_w = NULL;
    int *page_h = NULL;
    int cached_pages = 0;
    float cache_zoom = -1.0f;
    int cache_rotation = -1;
    int cache_first = -1;
    int cache_last = -1;

    /* ---- Main loop ------------------------------------------------------ */
    Uint32 frame_ms = 1000 / FPS;
    Uint32 prev_time = SDL_GetTicks();

    while (state.running) {
        Uint32 now = SDL_GetTicks();
        Uint32 dt  = now - prev_time;
        prev_time  = now;

        /* ---- Events ----- */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            input_handle(&ictx, &event);
        }

        /* ---- Load thumbnails lazily whenever library needs them --------- */
        if (state.screen_mode == SCREEN_LIBRARY && !library.thumbs_loaded) {
            library_load_thumbs(&library, renderer, CARD_W - 16, THUMB_H);
        }

        /* ---- Re-render page if dirty ------------------------------------ */
        if (ictx.page_dirty && doc) {
            if (tex1) { SDL_DestroyTexture(tex1); tex1 = NULL; }
            if (tex2) { SDL_DestroyTexture(tex2); tex2 = NULL; }
            t1w = t1h = t2w = t2h = 0;

            if (state.view_mode == MODE_SINGLE_PAGE) {
                bool rebuild_cache = !pages ||
                    cached_pages != state.page_count ||
                    cache_zoom != state.zoom ||
                    cache_rotation != state.rotation;

                if (rebuild_cache) {
                    free_page_cache(&pages, &page_w, &page_h, &cached_pages);
                    cached_pages = state.page_count;
                    pages = calloc((size_t)cached_pages, sizeof(*pages));
                    page_w = calloc((size_t)cached_pages, sizeof(*page_w));
                    page_h = calloc((size_t)cached_pages, sizeof(*page_h));
                    cache_zoom = state.zoom;
                    cache_rotation = state.rotation;
                    cache_first = -1;
                    cache_last = -1;
                    if (page_w && page_h) {
                        int est_w = 0;
                        int est_h = estimate_page_height(doc, &state);
                        pdf_get_page_size(doc, clamp_page_index(state.current_page, state.page_count),
                                          state.zoom, state.rotation, &est_w, &est_h);
                        if (est_w <= 0) est_w = state.window_width - READER_SIDEBAR_W - STATUS_W - 80;
                        if (est_w < 100) est_w = 100;
                        if (est_h < 100) est_h = estimate_page_height(doc, &state);
                        for (int i = 0; i < cached_pages; i++) {
                            page_w[i] = est_w;
                            page_h[i] = est_h;
                        }
                    }
                }

                if (pages && page_w && page_h) {
                    bool shift_window = cache_first < 0 ||
                        state.current_page < cache_first + 2 ||
                        state.current_page > cache_last - 2;

                    if (shift_window) {
                        cache_first = state.current_page - 3;
                        cache_last  = state.current_page + 3;
                        if (cache_first < 0) {
                            cache_last -= cache_first;
                            cache_first = 0;
                        }
                        if (cache_last >= cached_pages) {
                            int over = cache_last - cached_pages + 1;
                            cache_first -= over;
                            cache_last = cached_pages - 1;
                            if (cache_first < 0) cache_first = 0;
                        }
                    }

                    for (int i = cache_first; i <= cache_last && i < cached_pages; i++) {
                        if (i < 0) continue;
                        if (page_w[i] <= 0 || page_h[i] <= 0) {
                            pdf_get_page_size(doc, i, state.zoom, state.rotation,
                                              &page_w[i], &page_h[i]);
                        }
                    }

                    for (int i = 0; i < cached_pages; i++) {
                        if (i < cache_first || i > cache_last) {
                            if (pages[i]) {
                                SDL_DestroyTexture(pages[i]);
                                pages[i] = NULL;
                            }
                        }
                    }
                    if (rebuild_cache && state.scroll_y == 0 && state.current_page > 0) {
                        for (int i = 0; i < state.current_page && i < cached_pages; i++)
                            state.scroll_y += page_h[i] + READER_PAGE_GAP;
                    }
                } else {
                    free_page_cache(&pages, &page_w, &page_h, &cached_pages);
                }
            } else {
                free_page_cache(&pages, &page_w, &page_h, &cached_pages);
                cache_zoom = -1.0f;
                cache_rotation = -1;
                cache_first = -1;
                cache_last = -1;
                tex1 = pdf_render_page(doc, renderer,
                                       state.current_page,
                                       state.zoom, 0, &t1w, &t1h);
                int p2 = state.current_page + 1;
                if (p2 < state.page_count)
                    tex2 = pdf_render_page(doc, renderer, p2,
                                            state.zoom, 0, &t2w, &t2h);
            }
            ictx.page_dirty = false;
        }

        /* Render at most one missing continuous-scroll page per frame.
           This avoids a visible hitch when the cache window shifts. */
        if (state.screen_mode == SCREEN_READER &&
            state.view_mode == MODE_SINGLE_PAGE &&
            doc && pages && page_w && page_h &&
            cache_first >= 0 && cache_last >= cache_first) {
            int target = -1;
            if (state.current_page >= cache_first &&
                state.current_page <= cache_last &&
                !pages[state.current_page]) {
                target = state.current_page;
            } else {
                for (int dist = 1; dist <= 3 && target < 0; dist++) {
                    int before = state.current_page - dist;
                    int after = state.current_page + dist;
                    if (after <= cache_last && after < cached_pages && !pages[after])
                        target = after;
                    else if (before >= cache_first && before >= 0 && !pages[before])
                        target = before;
                }
            }

            if (target >= 0) {
                pages[target] = pdf_render_page(doc, renderer, target,
                                                state.zoom, state.rotation,
                                                &page_w[target], &page_h[target]);
            }
        }

        /* Clamp scroll so we can't scroll past page bottom */
        if (state.view_mode == MODE_SINGLE_PAGE &&
            cached_pages > 0 && pages && page_w && page_h) {
            int max_scroll = continuous_max_scroll(&state, page_h, cached_pages);
            if (state.scroll_y > max_scroll) state.scroll_y = max_scroll;
            if (state.scroll_y < 0)          state.scroll_y = 0;
        } else if (t1h > 0) {
            int visible = state.window_height - STATUS_H;
            int spread_h = t1h > t2h ? t1h : t2h;
            int max_scroll = spread_h - visible + 40;
            if (max_scroll < 0) max_scroll = 0;
            if (state.scroll_y > max_scroll) state.scroll_y = max_scroll;
            if (state.scroll_y < 0)          state.scroll_y = 0;
        }

        /* ---- Draw ------------------------------------------------------- */
        SDL_SetRenderDrawColor(renderer, C_BG_R, C_BG_G, C_BG_B, 255);
        SDL_RenderClear(renderer);

        if (state.screen_mode == SCREEN_READER) {
            if (!state.has_document) {
                if (ui) ui_render_no_doc(renderer, ui, &state);
            } else {
                ui_render_reader(renderer, ui, &state,
                                 tex1, t1w, t1h,
                                 tex2, t2w, t2h,
                                 pages, page_w, page_h, cached_pages);
            }
        } else {
            if (ui) ui_render_library(renderer, ui, &state, &library);
        }

        /* Status bar always visible */
        if (ui) ui_render_status(renderer, ui, &state);

        /* Overlay message */
        if (state.msg_timer > 0) {
            if (ui) ui_render_message(renderer, ui, state.status_msg);
            state.msg_timer--;
        }

        SDL_RenderPresent(renderer);

        /* Cap frame rate */
        Uint32 elapsed = SDL_GetTicks() - now;
        if (elapsed < frame_ms) SDL_Delay(frame_ms - elapsed);
        (void)dt;
    }

    /* ---- Cleanup ------------------------------------------------------- */
    if (tex1) SDL_DestroyTexture(tex1);
    if (tex2) SDL_DestroyTexture(tex2);
    free_page_cache(&pages, &page_w, &page_h, &cached_pages);
    library_free_thumbs(&library);
    pdf_close(doc);
    if (ui) ui_free(ui);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "ui.h"
#include "state.h"
#include "library.h"

/* ---- font search -------------------------------------------------------- */
static const char *FONT_PATHS[] = {
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
    NULL
};

static TTF_Font *load_font(int pt)
{
    for (int i = 0; FONT_PATHS[i]; i++) {
        TTF_Font *f = TTF_OpenFont(FONT_PATHS[i], pt);
        if (f) return f;
    }
    return NULL;
}

/* ---- UICtx --------------------------------------------------------------- */
UICtx *ui_init(void)
{
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        return NULL;
    }
    UICtx *u = calloc(1, sizeof(UICtx));
    if (!u) {
        TTF_Quit();
        return NULL;
    }
    u->font_md = load_font(15);
    u->font_sm = load_font(12);
    u->font_lg = load_font(22);
    return u;
}

void ui_free(UICtx *u)
{
    if (!u) return;
    if (u->font_md) TTF_CloseFont(u->font_md);
    if (u->font_sm) TTF_CloseFont(u->font_sm);
    if (u->font_lg) TTF_CloseFont(u->font_lg);
    TTF_Quit();
    free(u);
}

/* ---- drawing helpers ----------------------------------------------------- */
void ui_set_color(SDL_Renderer *r, Uint8 R, Uint8 G, Uint8 B, Uint8 A)
{
    SDL_SetRenderDrawColor(r, R, G, B, A);
}

void ui_fill_rect(SDL_Renderer *r, int x, int y, int w, int h,
                  Uint8 R, Uint8 G, Uint8 B, Uint8 A)
{
    SDL_SetRenderDrawBlendMode(r, A < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, R, G, B, A);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* Approximate rounded rect using smaller filled rects + circles via ellipse fills */
void ui_rounded_rect(SDL_Renderer *r, int x, int y, int w, int h,
                     int radius, Uint8 R, Uint8 G, Uint8 B, Uint8 A)
{
    if (radius <= 0) { ui_fill_rect(r, x, y, w, h, R, G, B, A); return; }
    if (radius > w/2) radius = w/2;
    if (radius > h/2) radius = h/2;

    SDL_SetRenderDrawBlendMode(r, A < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, R, G, B, A);

    /* horizontal strip (full width) */
    SDL_Rect hr = {x, y + radius, w, h - 2*radius};
    SDL_RenderFillRect(r, &hr);
    /* top and bottom strips (minus corners) */
    SDL_Rect tr = {x + radius, y, w - 2*radius, radius};
    SDL_RenderFillRect(r, &tr);
    SDL_Rect br = {x + radius, y + h - radius, w - 2*radius, radius};
    SDL_RenderFillRect(r, &br);

    /* Draw corners as filled quarter-circles using scanlines */
    int cx[4] = {x+radius,        x+w-radius-1,   x+w-radius-1,   x+radius};
    int cy[4] = {y+radius,        y+radius,        y+h-radius-1,   y+h-radius-1};
    for (int c = 0; c < 4; c++) {
        for (int dy = -radius; dy <= 0; dy++) {
            int dx = (int)sqrtf((float)(radius*radius - dy*dy));
            int lx = cx[c] - dx;
            int rx = cx[c] + dx;
            int py = cy[c] + dy;
            if (c >= 2) py = cy[c] - dy;
            SDL_RenderDrawLine(r, lx, py, rx, py);
        }
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void ui_text(SDL_Renderer *r, TTF_Font *f, const char *txt,
             int x, int y, Uint8 R, Uint8 G, Uint8 B)
{
    if (!f || !txt || txt[0] == '\0') return;
    SDL_Color col = {R, G, B, 255};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(f, txt, col);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    if (tex) {
        SDL_Rect dst = {x, y, surf->w, surf->h};
        SDL_RenderCopy(r, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

void ui_text_centered(SDL_Renderer *r, TTF_Font *f, const char *txt,
                      int cx, int y, Uint8 R, Uint8 G, Uint8 B)
{
    if (!f || !txt) return;
    int tw, th;
    TTF_SizeUTF8(f, txt, &tw, &th);
    ui_text(r, f, txt, cx - tw/2, y, R, G, B);
}

static void ui_text_rotated(SDL_Renderer *r, TTF_Font *f, const char *txt,
                            int cx, int bottom, double angle,
                            Uint8 R, Uint8 G, Uint8 B)
{
    if (!f || !txt || txt[0] == '\0') return;
    SDL_Color col = {R, G, B, 255};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(f, txt, col);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    if (tex) {
        int final_h = (angle == 90.0 || angle == -90.0) ? surf->w : surf->h;
        int center_y = bottom - final_h / 2;
        SDL_Rect dst = {
            cx - surf->w / 2,
            center_y - surf->h / 2,
            surf->w,
            surf->h
        };
        SDL_Point center = {surf->w / 2, surf->h / 2};
        SDL_RenderCopyEx(r, tex, NULL, &dst, angle, &center, SDL_FLIP_NONE);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

/* ---- page shadow --------------------------------------------------------- */
static void draw_page_shadow(SDL_Renderer *r, int x, int y, int w, int h)
{
    for (int i = 4; i >= 1; i--) {
        Uint8 alpha = (Uint8)(50 / i);
        ui_fill_rect(r, x - i, y + i, w + i*2, h + i*2, 0, 0, 0, alpha);
    }
}

static void draw_sidebar_button(SDL_Renderer *r, UICtx *u,
                                int x, int y, int w, int h,
                                const char *label, bool active)
{
    Uint8 br = active ? C_HOVER_R : C_CARD_R;
    Uint8 bg = active ? C_HOVER_G : C_CARD_G;
    Uint8 bb = active ? C_HOVER_B : C_CARD_B;
    ui_rounded_rect(r, x, y, w, h, 6, br, bg, bb, 255);
    if (active) {
        SDL_SetRenderDrawColor(r, C_ACC_R, C_ACC_G, C_ACC_B, 255);
        SDL_Rect rect = {x, y, w, h};
        SDL_RenderDrawRect(r, &rect);
    }
    int tw = 0, th = 0;
    if (u->font_sm) TTF_SizeUTF8(u->font_sm, label, &tw, &th);
    ui_text(r, u->font_sm, label, x + (w - tw) / 2, y + (h - th) / 2,
            C_TEXT_R, C_TEXT_G, C_TEXT_B);
}

static void draw_reader_sidebar(SDL_Renderer *r, UICtx *u, const AppState *s)
{
    int wh = s->window_height - STATUS_H;
    ui_fill_rect(r, 0, 0, READER_SIDEBAR_W, wh, C_BAR_R, C_BAR_G, C_BAR_B, 235);
    ui_fill_rect(r, READER_SIDEBAR_W - 1, 0, 1, wh,
                 C_ACC_R, C_ACC_G, C_ACC_B, 70);

    float t = (s->zoom - 0.2f) / (2.0f - 0.2f);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    int track_x = READER_SIDEBAR_W / 2;
    int track_top = 80;
    int track_bot = wh - 156;
    if (track_bot < track_top + 120) track_bot = track_top + 120;
    int track_h = track_bot - track_top;
    int knob_y = track_bot - (int)(t * track_h);

    ui_text_centered(r, u->font_sm, "Zoom", track_x, 44,
                     C_SUB_R, C_SUB_G, C_SUB_B);
    ui_fill_rect(r, track_x - 2, track_top, 4, track_h, 58, 58, 88, 255);
    ui_fill_rect(r, track_x - 2, knob_y, 4, track_bot - knob_y,
                 C_ACC_R, C_ACC_G, C_ACC_B, 255);
    ui_rounded_rect(r, track_x - 9, knob_y - 5, 18, 10, 5,
                    C_ACC_R, C_ACC_G, C_ACC_B, 255);

    char ztxt[32];
    snprintf(ztxt, sizeof(ztxt), "%d%%", (int)(s->zoom * 100));
    ui_text_centered(r, u->font_sm, ztxt, track_x, track_bot + 14,
                     C_TEXT_R, C_TEXT_G, C_TEXT_B);

    int btn_w = READER_SIDEBAR_W - 6;
    int book_y = wh - 94;
    int single_y = wh - 52;
    draw_sidebar_button(r, u, 3, book_y, btn_w, 34, "Book",
                        s->view_mode == MODE_TWO_PAGE);
    draw_sidebar_button(r, u, 3, single_y, btn_w, 34, "Single",
                        s->view_mode == MODE_SINGLE_PAGE);
}

/* ---- READER -------------------------------------------------------------- */
void ui_render_reader(SDL_Renderer *r, UICtx *u, const AppState *s,
                      SDL_Texture *page1, int p1w, int p1h,
                      SDL_Texture *page2, int p2w, int p2h,
                      SDL_Texture **pages, const int *page_w,
                      const int *page_h, int cached_pages)
{
    int ww = s->window_width;
    int wh = s->window_height - STATUS_H;
    int view_x = READER_SIDEBAR_W;
    int view_w = ww - view_x - STATUS_W;
    int view_top = 0;
    int view_h = wh;
    if (view_w < 1) view_w = 1;

    draw_reader_sidebar(r, u, s);

    if (!page1 && (!pages || cached_pages <= 0)) {
        ui_text_centered(r, u->font_md, "Loading...",
                         view_x + view_w/2, view_top + view_h/2 - 10,
                         C_SUB_R, C_SUB_G, C_SUB_B);
        return;
    }

    SDL_Rect clip = {view_x, view_top, view_w, view_h};
    SDL_RenderSetClipRect(r, &clip);

    if (s->view_mode == MODE_SINGLE_PAGE) {
        int total_h = READER_PAGE_MARGIN;
        for (int i = 0; i < cached_pages; i++)
            total_h += page_h[i] + READER_PAGE_GAP;
        if (cached_pages > 0) total_h -= READER_PAGE_GAP;
        total_h += READER_PAGE_MARGIN;

        int y = READER_PAGE_MARGIN - s->scroll_y;
        if (total_h < view_h) y += (view_h - total_h) / 2;

        for (int i = 0; i < cached_pages; i++) {
            if (page_w[i] > 0 && page_h[i] > 0) {
                int draw_w = page_w[i];
                int draw_h = page_h[i];
                if (pages[i])
                    SDL_QueryTexture(pages[i], NULL, NULL, &draw_w, &draw_h);
                int px = view_x + (view_w - draw_w) / 2;
                int py = view_top + y;
                if (py + draw_h >= view_top && py <= view_top + view_h) {
                    draw_page_shadow(r, px, py, draw_w, draw_h);
                    SDL_Rect dst = {px, py, draw_w, draw_h};
                    if (pages[i]) {
                        SDL_RenderCopy(r, pages[i], NULL, &dst);
                    } else {
                        ui_fill_rect(r, px, py, draw_w, draw_h, 255, 255, 255, 255);
                    }
                }
            }
            y += page_h[i] + READER_PAGE_GAP;
        }

    } else {
        /* TWO-PAGE: page1 on left, page2 on right */
        int gap  = 16;
        int total_w = p1w + gap + (page2 ? p2w : 0);
        int max_h   = p1h > p2h ? p1h : p2h;

        /* Centre the spread */
        int ox = view_x + (view_w - total_w) / 2;
        int oy = view_top + (view_h - max_h) / 2 - s->scroll_y;
        if (max_h > view_h && oy > view_top + 4) oy = view_top + 4;
        if (max_h <= view_h && oy < view_top + 4) oy = view_top + 4;

        /* Page 1 */
        int y1 = oy + (max_h - p1h) / 2;
        draw_page_shadow(r, ox, y1, p1w, p1h);
        SDL_Rect d1 = {ox, y1, p1w, p1h};
        SDL_RenderCopy(r, page1, NULL, &d1);

        /* Page 2 */
        if (page2) {
            int x2 = ox + p1w + gap;
            int y2 = oy + (max_h - p2h) / 2;
            draw_page_shadow(r, x2, y2, p2w, p2h);
            SDL_Rect d2 = {x2, y2, p2w, p2h};
            SDL_RenderCopy(r, page2, NULL, &d2);
        }
    }

    SDL_RenderSetClipRect(r, NULL);
}

/* ---- STATUS BAR ---------------------------------------------------------- */
void ui_render_status(SDL_Renderer *r, UICtx *u, const AppState *s)
{
    int ww = s->window_width;
    int wh = s->window_height;
    int x = ww - STATUS_W;

    ui_fill_rect(r, x, 0, STATUS_W, wh, C_BAR_R, C_BAR_G, C_BAR_B, 255);
    ui_fill_rect(r, x, 0, 1, wh, C_ACC_R, C_ACC_G, C_ACC_B, 80);

    if (!u->font_sm) return;

    if (s->has_document) {
        int track_x = x + STATUS_W / 2;
        int track_h = wh / 2;
        if (track_h < 160) track_h = 160;
        if (track_h > 360) track_h = 360;
        int track_top = (wh - track_h) / 2;
        int track_bot = track_top + track_h;
        float p = s->page_count > 1
            ? (float)s->current_page / (float)(s->page_count - 1)
            : 0.0f;
        if (p < 0.0f) p = 0.0f;
        if (p > 1.0f) p = 1.0f;
        int knob_y = track_top + (int)(p * (track_bot - track_top));

        draw_sidebar_button(r, u, x + 5, 10, STATUS_W - 10, 30, "Lib", false);

        char cur_page[16];
        snprintf(cur_page, sizeof(cur_page), "%d", s->current_page + 1);
        ui_text_centered(r, u->font_sm, cur_page, track_x, track_top - 20,
                         C_TEXT_R, C_TEXT_G, C_TEXT_B);

        ui_fill_rect(r, track_x - 2, track_top, 4, track_bot - track_top,
                     58, 58, 88, 255);
        ui_fill_rect(r, track_x - 2, track_top, 4, knob_y - track_top,
                     C_ACC_R, C_ACC_G, C_ACC_B, 255);
        ui_rounded_rect(r, track_x - 9, knob_y - 5, 18, 10, 5,
                        C_ACC_R, C_ACC_G, C_ACC_B, 255);

        char total_pages[16];
        snprintf(total_pages, sizeof(total_pages), "%d", s->page_count);
        ui_text_centered(r, u->font_sm, total_pages, track_x, track_bot + 14,
                         C_SUB_R, C_SUB_G, C_SUB_B);

        char mode[96];
        snprintf(mode, sizeof(mode), "%d%%  %s%s",
                 (int)(s->zoom * 100),
                 s->view_mode == MODE_TWO_PAGE ? "2-Page" : "1-Page",
                 s->rotation ? "↺" : "");
        ui_text_rotated(r, u->font_sm, mode, x + STATUS_W / 2, wh - 18, -90.0,
                        C_SUB_R, C_SUB_G, C_SUB_B);
    } else {
        ui_text_rotated(r, u->font_sm, "Press b for library",
                        x + STATUS_W / 2, wh - 12, -90.0,
                        C_SUB_R, C_SUB_G, C_SUB_B);
    }
}

/* ---- OVERLAY MESSAGE ----------------------------------------------------- */
void ui_render_message(SDL_Renderer *r, UICtx *u, const char *msg)
{
    if (!msg || msg[0] == '\0') return;
    TTF_Font *f = u ? u->font_md : NULL;
    int tw = 200, th = 20;
    if (f) TTF_SizeUTF8(f, msg, &tw, &th);
    int pw = tw + 28, ph = th + 16;
    /* query renderer output size */
    int ww, wh;
    SDL_GetRendererOutputSize(r, &ww, &wh);
    int x = (ww - pw) / 2;
    int y = wh - STATUS_H - ph - 20;
    ui_rounded_rect(r, x, y, pw, ph, 8, 30, 30, 60, 230);
    if (f) ui_text(r, f, msg, x + 14, y + 8, C_TEXT_R, C_TEXT_G, C_TEXT_B);
}

/* ---- NO-DOC SPLASH ------------------------------------------------------- */
void ui_render_no_doc(SDL_Renderer *r, UICtx *u, const AppState *s)
{
    int cx = s->window_width / 2;
    int cy = (s->window_height - STATUS_H) / 2;
    ui_text_centered(r, u->font_lg, "creader", cx, cy - 40,
                     C_ACC_R, C_ACC_G, C_ACC_B);
    ui_text_centered(r, u->font_sm, "No document open", cx, cy,
                     C_SUB_R, C_SUB_G, C_SUB_B);
    ui_text_centered(r, u->font_sm, "Press  b  to open Library", cx, cy + 22,
                     C_SUB_R, C_SUB_G, C_SUB_B);
}

/* ---- LIBRARY ------------------------------------------------------------- */
static void truncate_str(const char *src, char *dst, int max_chars)
{
    int len = (int)strlen(src);
    if (len <= max_chars) { strcpy(dst, src); return; }
    strncpy(dst, src, max_chars - 3);
    dst[max_chars - 3] = '\0';
    strcat(dst, "...");
}

static void draw_card(SDL_Renderer *r, UICtx *u,
                      int cx, int cy, /* top-left of card */
                      const LibraryItem *item,
                      SDL_Texture *thumb,
                      bool selected)
{
    /* Card background */
    Uint8 cr = selected ? C_HOVER_R : C_CARD_R;
    Uint8 cg = selected ? C_HOVER_G : C_CARD_G;
    Uint8 cb = selected ? C_HOVER_B : C_CARD_B;
    ui_rounded_rect(r, cx, cy, CARD_W, CARD_H, 8, cr, cg, cb, 255);

    /* Accent border for selected */
    if (selected) {
        SDL_SetRenderDrawColor(r, C_ACC_R, C_ACC_G, C_ACC_B, 255);
        SDL_Rect brd = {cx, cy, CARD_W, CARD_H};
        SDL_RenderDrawRect(r, &brd);
    }

    /* Thumbnail area */
    int thumb_y = cy + 8;
    int thumb_x = cx + 8;
    int tw = CARD_W - 16;
    int th = THUMB_H;

    if (thumb) {
        int src_w = tw, src_h = th;
        SDL_QueryTexture(thumb, NULL, NULL, &src_w, &src_h);
        float scale_x = (float)tw / (float)src_w;
        float scale_y = (float)th / (float)src_h;
        float scale = scale_x < scale_y ? scale_x : scale_y;
        int dst_w = (int)(src_w * scale);
        int dst_h = (int)(src_h * scale);
        SDL_Rect dst = {
            thumb_x + (tw - dst_w) / 2,
            thumb_y + (th - dst_h) / 2,
            dst_w,
            dst_h
        };
        ui_fill_rect(r, thumb_x, thumb_y, tw, th, 40, 40, 70, 255);
        SDL_RenderCopy(r, thumb, NULL, &dst);
        /* small rounded overlay at top-right for progress */
    } else {
        ui_fill_rect(r, thumb_x, thumb_y, tw, th, 40, 40, 70, 255);
        ui_text_centered(r, u->font_sm, "PDF", cx + CARD_W/2, thumb_y + th/2 - 8,
                         C_SUB_R, C_SUB_G, C_SUB_B);
    }

    /* Progress bar (thin, below thumbnail) */
    int bar_y = cy + 8 + th + 4;
    int bar_w = CARD_W - 16;
    ui_fill_rect(r, thumb_x, bar_y, bar_w, 3, 50, 50, 80, 255);
    float prog = item->page_count > 0
                 ? (float)item->last_page / (float)item->page_count
                 : 0.0f;
    if (prog > 1.0f) prog = 1.0f;
    if (prog > 0.0f) {
        int filled = (int)(bar_w * prog);
        ui_fill_rect(r, thumb_x, bar_y, filled, 3, C_ACC_R, C_ACC_G, C_ACC_B, 255);
    }

    /* Title */
    char short_title[32];
    truncate_str(item->title, short_title, 18);
    int title_y = bar_y + 8;
    ui_text(r, u->font_sm, short_title, thumb_x, title_y,
            C_TEXT_R, C_TEXT_G, C_TEXT_B);

    /* Progress text */
    if (item->page_count > 0) {
        char prog_str[32];
        snprintf(prog_str, sizeof(prog_str), "%d/%d",
                 item->last_page + 1, item->page_count);
        ui_text(r, u->font_sm, prog_str, thumb_x, title_y + 16,
                C_SUB_R, C_SUB_G, C_SUB_B);
    }
}

/* Draw search/input bar */
static void draw_input_bar(SDL_Renderer *r, UICtx *u,
                           const char *label, const char *buf,
                           int ww, int wh)
{
    int bw = 500, bh = 44;
    int bx = (ww - bw) / 2;
    int by = (wh - bh) / 2;
    ui_rounded_rect(r, bx - 4, by - 4, bw + 8, bh + 8, 10, 20, 20, 50, 240);
    ui_rounded_rect(r, bx, by, bw, bh, 8, 35, 35, 70, 255);
    char display[600];
    snprintf(display, sizeof(display), "%s%s|", label, buf);
    ui_text(r, u->font_md, display, bx + 12, by + 12,
            C_TEXT_R, C_TEXT_G, C_TEXT_B);
}

/* ---- category-aware layout traversal ------------------------------------ */
/*
 * Iterates display items in category order and calls back with the position
 * of each item and any category header that precedes it.
 * Returns total content height (pixels, not counting GRID_TOP offset).
 *
 * cb_header(y_rel, cat, user)  — called before the first card of each category
 * cb_card  (y_rel, col, lib_idx, user) — called for each card
 *
 * Pass NULL callbacks to just measure total height.
 */
typedef void (*cb_header_fn)(int y, const char *cat, void *user);
typedef void (*cb_card_fn)(int y, int col, int lib_idx, void *user);

static int traverse_library(const Library *lib,
                             const int *sorted, int count, int cols,
                             cb_header_fn cb_hdr, cb_card_fn cb_card,
                             void *user)
{
    int y_rel = 0;
    int col   = 0;
    const char *prev_cat = NULL;

    for (int s = 0; s < count; s++) {
        int idx = sorted[s];
        const char *cat = lib->items[idx].category;

        /* New category? */
        bool new_cat = (prev_cat == NULL) ||
                       (strcmp(cat, prev_cat) != 0);

        if (new_cat) {
            /* Finish current row before header */
            if (col > 0) {
                y_rel += CARD_H + CARD_GAP;
                col    = 0;
            }
            /* Draw category header for non-empty categories */
            if (cat[0] != '\0') {
                if (cb_hdr) cb_hdr(y_rel, cat, user);
                y_rel += CAT_HEADER_H + 6;
            }
            prev_cat = cat;
        }

        if (cb_card) cb_card(y_rel, col, idx, user);

        col++;
        if (col >= cols) {
            y_rel += CARD_H + CARD_GAP;
            col    = 0;
        }
    }
    if (col > 0) y_rel += CARD_H + CARD_GAP;

    return y_rel;
}

/* ---- render callbacks --------------------------------------------------- */
typedef struct {
    SDL_Renderer *r;
    UICtx        *u;
    Library      *lib;
    int           scroll_y;
    int           grid_top;
    int           ww;
    int           wh;
} RenderCtx;

static void render_cat_header(int y_rel, const char *cat, void *user)
{
    RenderCtx *rc = (RenderCtx *)user;
    int screen_y  = rc->grid_top + y_rel - rc->scroll_y;
    if (screen_y + CAT_HEADER_H < rc->grid_top || screen_y > rc->wh) return;

    /* Accent line + folder icon area */
    ui_fill_rect(rc->r, GRID_LEFT, screen_y + CAT_HEADER_H - 2,
                 rc->ww - GRID_LEFT * 2, 1,
                 C_ACC_R, C_ACC_G, C_ACC_B, 60);

    /* Category name */
    char label[MAX_TITLE_LEN + 4];
    snprintf(label, sizeof(label), "  %s", cat);
    ui_text(rc->r, rc->u->font_md, label,
            GRID_LEFT, screen_y + 6,
            C_ACC_R, C_ACC_G, C_ACC_B);
}

static void render_card_cb(int y_rel, int col, int lib_idx, void *user)
{
    RenderCtx *rc  = (RenderCtx *)user;
    int cx = GRID_LEFT + col * (CARD_W + CARD_GAP);
    int cy = rc->grid_top + y_rel - rc->scroll_y;

    if (cy + CARD_H < rc->grid_top || cy > rc->wh) return;

    draw_card(rc->r, rc->u, cx, cy,
              &rc->lib->items[lib_idx],
              rc->lib->thumbs[lib_idx],
              rc->lib->selected == lib_idx);
}

/* ---- public render ------------------------------------------------------- */
void ui_render_library(SDL_Renderer *r, UICtx *u,
                       AppState *s, Library *lib)
{
    int ww = s->window_width;
    int wh = s->window_height - STATUS_H;

    /* Top bar */
    ui_fill_rect(r, 0, 0, ww, GRID_TOP - 4, C_BAR_R, C_BAR_G, C_BAR_B, 200);
    ui_fill_rect(r, 0, GRID_TOP - 5, ww, 1, C_ACC_R, C_ACC_G, C_ACC_B, 80);
    ui_text(r, u->font_lg, "creader", 20, 14, C_ACC_R, C_ACC_G, C_ACC_B);
    ui_text(r, u->font_sm,
            "Enter: open  |  o: add file  |  d: add folder by name/path  |"
            "  /: search  |  Del/x: remove  |  b: back",
            192, 20, C_SUB_R, C_SUB_G, C_SUB_B);

    /* Build display order */
    int sorted[MAX_LIBRARY];
    int display_count;

    if (lib->search_active && lib->search_len > 0) {
        display_count = library_filter(lib, lib->search_buf,
                                       sorted, MAX_LIBRARY);
        library_sort_indices(lib, sorted, display_count);
    } else {
        library_sorted_indices(lib, sorted);
        display_count = lib->count;
    }

    if (display_count == 0) {
        const char *msg = lib->count == 0
            ? "Library empty — drag a PDF here or press  a"
            : "No results";
        ui_text_centered(r, u->font_md, msg, ww/2, wh/2,
                         C_SUB_R, C_SUB_G, C_SUB_B);
    }

    int cols     = (ww - GRID_LEFT * 2 + CARD_GAP) / (CARD_W + CARD_GAP);
    if (cols < 1) cols = 1;
    int visible_h = wh - GRID_TOP;

    /* Measure total content height */
    int total_h = traverse_library(lib, sorted, display_count, cols,
                                   NULL, NULL, NULL);

    /* Clamp scroll */
    int max_scroll = total_h - visible_h;
    if (max_scroll < 0) max_scroll = 0;
    if (lib->scroll_y < 0)           lib->scroll_y = 0;
    if (lib->scroll_y > max_scroll)   lib->scroll_y = max_scroll;

    /* Clip to grid area and render */
    SDL_Rect clip = {0, GRID_TOP, ww, visible_h};
    SDL_RenderSetClipRect(r, &clip);

    RenderCtx rc = {r, u, lib, lib->scroll_y, GRID_TOP, ww, wh};
    traverse_library(lib, sorted, display_count, cols,
                     render_cat_header, render_card_cb, &rc);

    SDL_RenderSetClipRect(r, NULL);

    /* Scrollbar */
    if (total_h > visible_h && max_scroll > 0) {
        float ratio = (float)visible_h / (float)total_h;
        int   bar_h = (int)(visible_h * ratio);
        int   bar_y = GRID_TOP + (int)((visible_h - bar_h) *
                       (float)lib->scroll_y / (float)max_scroll);
        ui_fill_rect(r, ww - 6, bar_y, 4, bar_h, C_ACC_R, C_ACC_G, C_ACC_B, 140);
    }

    /* Input overlays */
    if (lib->input_active)
        draw_input_bar(r, u,
                       lib->input_dir_mode ? "Add folder: " : "Add path: ",
                       lib->input_buf, ww, wh);
    else if (lib->search_active)
        draw_input_bar(r, u, "Search: ", lib->search_buf, ww, wh);
}

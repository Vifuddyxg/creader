#ifndef UI_H
#define UI_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "state.h"
#include "library.h"

/* Colour palette */
#define C_BG_R   26
#define C_BG_G   26
#define C_BG_B   46

#define C_SURF_R 22
#define C_SURF_G 33
#define C_SURF_B 62

#define C_CARD_R 28
#define C_CARD_G 40
#define C_CARD_B 76

#define C_HOVER_R 38
#define C_HOVER_G 54
#define C_HOVER_B 100

#define C_ACC_R  94
#define C_ACC_G  169
#define C_ACC_B  235

#define C_TEXT_R  224
#define C_TEXT_G  224
#define C_TEXT_B  224

#define C_SUB_R  140
#define C_SUB_G  140
#define C_SUB_B  160

#define C_BAR_R  13
#define C_BAR_G  13
#define C_BAR_B  26

#define STATUS_H        0
#define STATUS_W        46
#define READER_SIDEBAR_W 50
#define READER_PAGE_MARGIN 20
#define READER_PAGE_GAP    24
#define CARD_W          168
#define CARD_H          258
#define THUMB_H         190
#define CARD_GAP        18
#define GRID_LEFT       20
#define GRID_TOP        70
#define CAT_HEADER_H    36

typedef struct UICtx {
    TTF_Font *font_md;   /* 15px  */
    TTF_Font *font_sm;   /* 12px  */
    TTF_Font *font_lg;   /* 22px  */
} UICtx;

UICtx *ui_init(void);
void   ui_free(UICtx *u);

/* Drawing helpers */
void ui_set_color(SDL_Renderer *r, Uint8 R, Uint8 G, Uint8 B, Uint8 A);
void ui_fill_rect(SDL_Renderer *r, int x, int y, int w, int h,
                  Uint8 R, Uint8 G, Uint8 B, Uint8 A);
void ui_rounded_rect(SDL_Renderer *r, int x, int y, int w, int h,
                     int radius, Uint8 R, Uint8 G, Uint8 B, Uint8 A);
void ui_text(SDL_Renderer *r, TTF_Font *f, const char *txt,
             int x, int y, Uint8 R, Uint8 G, Uint8 B);
void ui_text_centered(SDL_Renderer *r, TTF_Font *f, const char *txt,
                      int cx, int y, Uint8 R, Uint8 G, Uint8 B);

/* Top-level render calls */
void ui_render_reader(SDL_Renderer *r, UICtx *u, const AppState *s,
                      SDL_Texture *page1, int p1w, int p1h,
                      SDL_Texture *page2, int p2w, int p2h,
                      SDL_Texture **pages, const int *page_w,
                      const int *page_h, int cached_pages);
void ui_render_library(SDL_Renderer *r, UICtx *u,
                       AppState *s, Library *lib);
void ui_render_status(SDL_Renderer *r, UICtx *u, const AppState *s);
void ui_render_message(SDL_Renderer *r, UICtx *u, const char *msg);
void ui_render_no_doc(SDL_Renderer *r, UICtx *u, const AppState *s);

#endif

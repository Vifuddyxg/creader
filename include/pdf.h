#ifndef PDF_H
#define PDF_H

#include <SDL2/SDL.h>
#include <mupdf/fitz.h>

typedef struct PDFDoc {
    fz_context  *ctx;
    fz_document *doc;
    int          page_count;
    char         path[4096];
} PDFDoc;

PDFDoc *pdf_open(const char *path);
void    pdf_close(PDFDoc *d);

SDL_Texture *pdf_render_page(PDFDoc *d, SDL_Renderer *r,
                              int page_idx, float zoom, int rotation,
                              int *out_w, int *out_h);

SDL_Texture *pdf_render_thumbnail(const char *path, SDL_Renderer *r,
                                   int target_w, int target_h);

void pdf_get_page_size(PDFDoc *d, int page_idx, float zoom, int rotation,
                       int *w, int *h);

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pdf.h"

PDFDoc *pdf_open(const char *path)
{
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!ctx) return NULL;

    fz_register_document_handlers(ctx);

    fz_document *doc      = NULL;
    int          pg_count = 0;

    fz_try(ctx) {
        doc      = fz_open_document(ctx, path);
        pg_count = fz_count_pages(ctx, doc);
    }
    fz_catch(ctx) {
        fprintf(stderr, "pdf_open: %s\n", fz_caught_message(ctx));
        if (doc) fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        return NULL;
    }

    /* Allocate after fz_try to avoid longjmp clobbering warning */
    PDFDoc *d = calloc(1, sizeof(PDFDoc));
    if (!d) {
        fz_drop_document(ctx, doc);
        fz_drop_context(ctx);
        return NULL;
    }

    d->ctx        = ctx;
    d->doc        = doc;
    d->page_count = pg_count;
    snprintf(d->path, sizeof(d->path), "%s", path);
    return d;
}

void pdf_close(PDFDoc *d)
{
    if (!d) return;
    if (d->doc) fz_drop_document(d->ctx, d->doc);
    fz_drop_context(d->ctx);
    free(d);
}

/* Build combined rotation+scale matrix. */
static fz_matrix make_ctm(float zoom, int rotation)
{
    fz_matrix r = fz_rotate((float)rotation);
    fz_matrix s = fz_scale(zoom, zoom);
    return fz_concat(r, s);
}

static fz_matrix page_ctm(fz_rect bounds, float zoom, int rotation,
                          fz_irect *bbox)
{
    fz_matrix ctm = make_ctm(zoom, rotation);
    *bbox = fz_round_rect(fz_transform_rect(bounds, ctm));

    /* Move transformed page content to texture origin. Without this, pages
       with non-zero crop boxes or rotation render as a small corner plus white. */
    ctm = fz_concat(ctm, fz_translate((float)-bbox->x0, (float)-bbox->y0));
    bbox->x1 -= bbox->x0;
    bbox->y1 -= bbox->y0;
    bbox->x0 = 0;
    bbox->y0 = 0;
    return ctm;
}

static SDL_Texture *pixmap_to_texture(SDL_Renderer *renderer, fz_pixmap *pix)
{
    int w = pix->w;
    int h = pix->h;

    /* MuPDF RGB pixmap has 3 components, no alpha */
    SDL_Texture *tex = SDL_CreateTexture(renderer,
                                         SDL_PIXELFORMAT_RGB24,
                                         SDL_TEXTUREACCESS_STATIC,
                                         w, h);
    if (!tex) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return NULL;
    }
    SDL_UpdateTexture(tex, NULL, pix->samples, pix->stride);
    return tex;
}

SDL_Texture *pdf_render_page(PDFDoc *d, SDL_Renderer *r,
                              int page_idx, float zoom, int rotation,
                              int *out_w, int *out_h)
{
    if (!d || page_idx < 0 || page_idx >= d->page_count) return NULL;

    SDL_Texture *tex = NULL;
    fz_page    *page = NULL;
    fz_pixmap  *pix  = NULL;
    fz_device  *dev  = NULL;

    fz_var(page);
    fz_var(pix);
    fz_var(dev);
    fz_var(tex);

    fz_try(d->ctx) {
        page = fz_load_page(d->ctx, d->doc, page_idx);
        fz_rect bounds = fz_bound_page(d->ctx, page);
        fz_irect  irect;
        fz_matrix ctm  = page_ctm(bounds, zoom, rotation, &irect);

        pix = fz_new_pixmap_with_bbox(d->ctx, fz_device_rgb(d->ctx), irect, NULL, 0);
        fz_clear_pixmap_with_value(d->ctx, pix, 255);

        dev = fz_new_draw_device(d->ctx, fz_identity, pix);
        fz_run_page(d->ctx, page, dev, ctm, NULL);
        fz_close_device(d->ctx, dev);
        fz_drop_device(d->ctx, dev);
        dev = NULL;

        if (out_w) *out_w = pix->w;
        if (out_h) *out_h = pix->h;

        tex = pixmap_to_texture(r, pix);
    }
    fz_always(d->ctx) {
        if (dev) { fz_close_device(d->ctx, dev); fz_drop_device(d->ctx, dev); }
        if (pix)  fz_drop_pixmap(d->ctx, pix);
        if (page) fz_drop_page(d->ctx, page);
    }
    fz_catch(d->ctx) {
        fprintf(stderr, "pdf_render_page %d: %s\n", page_idx, fz_caught_message(d->ctx));
        if (tex) { SDL_DestroyTexture(tex); tex = NULL; }
    }

    return tex;
}

SDL_Texture *pdf_render_thumbnail(const char *path, SDL_Renderer *r,
                                   int target_w, int target_h)
{
    PDFDoc *d = pdf_open(path);
    if (!d) return NULL;

    SDL_Texture *tex = NULL;
    fz_page    *page = NULL;
    fz_pixmap  *pix  = NULL;
    fz_device  *dev  = NULL;

    fz_var(page);
    fz_var(pix);
    fz_var(dev);
    fz_var(tex);

    fz_try(d->ctx) {
        page = fz_load_page(d->ctx, d->doc, 0);
        fz_rect bounds = fz_bound_page(d->ctx, page);

        float pw = bounds.x1 - bounds.x0;
        float ph = bounds.y1 - bounds.y0;
        if (pw <= 0 || ph <= 0) { pw = 595; ph = 842; }

        float zoom_x = (float)target_w / pw;
        float zoom_y = (float)target_h / ph;
        float zoom   = (zoom_x < zoom_y) ? zoom_x : zoom_y;

        fz_irect  irect;
        fz_matrix ctm  = page_ctm(bounds, zoom, 0, &irect);

        pix = fz_new_pixmap_with_bbox(d->ctx, fz_device_rgb(d->ctx), irect, NULL, 0);
        fz_clear_pixmap_with_value(d->ctx, pix, 255);

        dev = fz_new_draw_device(d->ctx, fz_identity, pix);
        fz_run_page(d->ctx, page, dev, ctm, NULL);
        fz_close_device(d->ctx, dev);
        fz_drop_device(d->ctx, dev);
        dev = NULL;

        tex = pixmap_to_texture(r, pix);
    }
    fz_always(d->ctx) {
        if (dev) { fz_close_device(d->ctx, dev); fz_drop_device(d->ctx, dev); }
        if (pix)  fz_drop_pixmap(d->ctx, pix);
        if (page) fz_drop_page(d->ctx, page);
    }
    fz_catch(d->ctx) {
        fprintf(stderr, "pdf_render_thumbnail %s: %s\n", path, fz_caught_message(d->ctx));
        if (tex) { SDL_DestroyTexture(tex); tex = NULL; }
    }

    pdf_close(d);
    return tex;
}

void pdf_get_page_size(PDFDoc *d, int page_idx, float zoom, int rotation,
                       int *w, int *h)
{
    if (!d || page_idx < 0 || page_idx >= d->page_count) { *w = 0; *h = 0; return; }

    fz_page *page = NULL;
    fz_var(page);

    fz_try(d->ctx) {
        page = fz_load_page(d->ctx, d->doc, page_idx);
        fz_rect  bounds = fz_bound_page(d->ctx, page);
        fz_irect  irect;
        page_ctm(bounds, zoom, rotation, &irect);

        *w = irect.x1 - irect.x0;
        *h = irect.y1 - irect.y0;
    }
    fz_always(d->ctx) {
        if (page) fz_drop_page(d->ctx, page);
    }
    fz_catch(d->ctx) {
        *w = 0; *h = 0;
    }
}

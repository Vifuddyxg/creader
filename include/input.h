#ifndef INPUT_H
#define INPUT_H

#include <SDL2/SDL.h>
#include "state.h"
#include "library.h"

typedef struct PDFDoc PDFDoc;

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    AppState     *state;
    Library      *library;
    PDFDoc      **doc;          /* pointer to pointer so we can replace it */
    bool          page_dirty;   /* need re-render */
    bool          zoom_dragging;
    bool          page_dragging;
} InputCtx;

void input_handle(InputCtx *ctx, SDL_Event *e);

#endif

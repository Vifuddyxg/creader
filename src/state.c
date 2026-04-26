#include <string.h>
#include <stdio.h>
#include "state.h"

void state_init(AppState *s)
{
    memset(s, 0, sizeof(*s));
    s->zoom         = 1.0f;
    s->rotation     = 0;
    s->view_mode    = MODE_SINGLE_PAGE;
    s->screen_mode  = SCREEN_LIBRARY;
    s->running      = true;
    s->window_width  = 1280;
    s->window_height = 800;
    s->page_changed = true;
}

void state_set_message(AppState *s, const char *msg)
{
    snprintf(s->status_msg, MAX_MSG_LEN, "%s", msg);
    s->msg_timer = MESSAGE_FRAMES;
}

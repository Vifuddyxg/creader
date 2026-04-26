#ifndef STATE_H
#define STATE_H

#include <stdbool.h>

#define MAX_PATH_LEN    4096
#define MAX_TITLE_LEN   512
#define MAX_DATE_LEN    64
#define MAX_MSG_LEN     256
#define MAX_LIBRARY     256
#define MESSAGE_FRAMES  120

typedef enum {
    MODE_SINGLE_PAGE = 0,
    MODE_TWO_PAGE    = 1
} ViewMode;

typedef enum {
    SCREEN_READER  = 0,
    SCREEN_LIBRARY = 1
} ScreenMode;

typedef struct {
    char path[MAX_PATH_LEN];
    char title[MAX_TITLE_LEN];
    int  current_page;
    int  page_count;
    float zoom;
    int  rotation;      /* 0, 90, 180, 270 */
    int  scroll_y;
    int  window_width;
    int  window_height;
    ViewMode   view_mode;
    ScreenMode screen_mode;
    bool fullscreen;
    bool running;
    bool page_changed;
    bool has_document;
    char status_msg[MAX_MSG_LEN];
    int  msg_timer;
} AppState;

typedef struct {
    char path[MAX_PATH_LEN];
    char title[MAX_TITLE_LEN];
    char category[MAX_TITLE_LEN]; /* folder name, empty = uncategorised */
    int  last_page;
    int  page_count;
    float zoom;
    int  rotation;
    int  scroll_y;
    ViewMode view_mode;
    char last_opened[MAX_DATE_LEN];
} LibraryItem;

void state_init(AppState *s);
void state_set_message(AppState *s, const char *msg);

#endif

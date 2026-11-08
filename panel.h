#ifndef PANEL_H
#define PANEL_H

#include <stdbool.h>
#include "settings.h"

#define PANEL_STATUS (-2)

typedef struct {
    char *items[LINK_MAX_CANDIDATES];
    int count, selected, scale;
    bool dark;
} PanelContent;

bool panel_content_update(PanelContent *content, const char *const *items,
                          int count, int selected, int scale, bool dark);
void panel_content_clear(PanelContent *content);

void panel_size(const char *const *candidates, int count, bool status, int *width, int *height);
void panel_draw(unsigned char *pixels, int width, int height, int stride,
                int scale, const char *const *candidates, int count, int selected, bool dark);

#endif

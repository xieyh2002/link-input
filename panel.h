#ifndef PANEL_H
#define PANEL_H

#include <stdbool.h>

#define PANEL_STATUS (-2)

void panel_size(const char *const *candidates, int count, bool status, int *width, int *height);
void panel_draw(unsigned char *pixels, int width, int height, int stride,
                int scale, const char *const *candidates, int count, int selected, bool dark);

#endif

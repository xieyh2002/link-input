#ifndef QINGYIN_JAPANESE_H
#define QINGYIN_JAPANESE_H
#include <stdbool.h>
#include <stdint.h>

typedef struct Japanese Japanese;
Japanese *japanese_new(void);
void japanese_free(Japanese *jp);
void japanese_clear(Japanese *jp);
bool japanese_key(Japanese *jp, uint32_t keysym, bool shift);
const char *japanese_preedit(Japanese *jp);
int japanese_candidates(Japanese *jp, const char **items, int cap, int *selected);
char *japanese_take_commit(Japanese *jp);
#endif

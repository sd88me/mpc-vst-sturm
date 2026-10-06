/* Built-in sounds of this project's own (the instrument's factory sounds are not shipped; put your own .syx in SYSEX). */
#pragma once
#include <stdint.h>
#include "patch_tab.h"
int presets_count(void);
void presets_apply(int k, uint8_t f[NFIELD], char *name);   /* writes the whole sound (init values plus the preset's own) */

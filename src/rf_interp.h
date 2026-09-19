#ifndef RF_INTERP_H
#define RF_INTERP_H
#include <stdbool.h>
#include "id_rf.h"
bool RFI_Enabled(void);
void RFI_NoteSprite(RF_SpriteDrawEntry *sde, int unitX, int unitY, bool fresh);
void RFI_Frame(void);
#endif

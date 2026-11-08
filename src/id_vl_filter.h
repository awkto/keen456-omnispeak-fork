/* Screen filters (CRT / scanlines / ...) applied to the final 4x ARGB frame, ported from the
 * old DOSBox-X web shell (keen456 js/app.js FILTERS). CPU based so every SDL2 backend and the
 * WASM build get them. Frame dumps and the CI pixel compares are made from the composed buffer
 * before this runs, so they are never filtered. keen456-omnispeak-fork #101 */
#ifndef ID_VL_FILTER_H
#define ID_VL_FILTER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
	VLF_Filter_Off,
	VLF_Filter_Scanlines,
	VLF_Filter_CRT,
	VLF_Filter_Curved,
	VLF_Filter_RGB,
	VLF_Filter_Soft,
	VLF_Filter_Amber,
	VLF_Filter_Green,
	VLF_Filter_Count
} VLF_Filter;

typedef enum
{
	VLF_Pixels_Crisp,
	VLF_Pixels_Light,
	VLF_Pixels_Low,
	VLF_Pixels_Medium,
	VLF_Pixels_High,
	VLF_Pixels_Count
} VLF_Pixels;

extern const char *vlf_filterStrings[]; /* NULL terminated, for CFG_GetConfigEnum */
extern const char *vlf_pixelsStrings[];

void VLF_SetFilter(VLF_Filter f);
void VLF_SetPixels(VLF_Pixels p);
VLF_Filter VLF_GetFilter(void);
VLF_Pixels VLF_GetPixels(void);
/* true when anything is applied (filter other than off, or pixels other than crisp) */
bool VLF_Active(void);

/* seconds the backends spent in VLF_Apply since rf_interp last cleared it (debug stats) */
extern double vl_filterWorkS;

/* Filter a w x h ARGB frame. A 320x200 frame is first expanded 4x (nearest). Returns a buffer
 * owned by the module (valid until the next call) and its size, or src itself and unchanged
 * size when nothing is active. src is never modified. */
const uint32_t *VLF_Apply(const uint32_t *src, int w, int h, int *outW, int *outH);

#endif

/*
PSP_SEETHROUGH.H

Build 116: a see-through shader's fade, packed above its blend function in
the short the transparent queue already carries. The engine side packs it
(psp_fixups.py, psp_transparent_map; psp_limits.h includes this header for
the game's units) and psp_ge.c unpacks it (it includes this header by path:
the plain-C units have no -I for psp/include).

  bits 0-3   the blend function (0 alpha blend, 1 multiply, 2 double
             multiply, 3 add, 4 subtract, 5 min, 6 max, 7 alpha-multiply-add)
  bits 4-5   the framebuffer fade mode: 0 none, 1 fade when perpendicular,
             2 fade when parallel
  bit 6      "don't fade with active camouflage" (chicago extra flags, bit 0)
  bits 7-14  how much the fade source takes away (0: none, 255: all)

A plain blend function (nothing above bit 3) is full strength, no fade, so
every queue entry made before build 116 reads the same as before.
*/
#ifndef __PSP_SEETHROUGH_H
#define __PSP_SEETHROUGH_H

#define PSP_SEETHROUGH_PACK(blend, mode, fade, keep_with_camouflage) \
	((short)(((blend) & 15) | (((mode) & 3) << 4) | ((keep_with_camouflage) ? 64 : 0) | \
	((255 - (int)((((fade) < 0.0f ? 0.0f : (fade) > 1.0f ? 1.0f : (fade)) * 255.0f) + 0.5f)) << 7)))
#define PSP_SEETHROUGH_BLEND(packed) ((packed) & 15)
#define PSP_SEETHROUGH_MODE(packed) (((packed) >> 4) & 3)
#define PSP_SEETHROUGH_KEEP_WITH_CAMOUFLAGE(packed) (((packed) & 64) != 0)
#define PSP_SEETHROUGH_FADE(packed) ((float)(255 - (((packed) >> 7) & 255)) / 255.0f)

#endif /* __PSP_SEETHROUGH_H */

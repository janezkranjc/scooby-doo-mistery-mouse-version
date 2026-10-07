// Software image scalers (0xD3F0-0xFA76).
//
// Source images hold one pixel per byte. A table at 0xC3F0 has, for every
// scale value 0..255, a bit mask telling which source rows and columns
// survive. Kept pixels are packed eight at a time into 4bpp tile data,
// placed through a layout table that maps each 8-pixel strip to its position
// in the sprite's tile buffer. The result sits top-left in a zero-padded
// canvas. The original unrolls this into four routines for four image sizes.
#pragma once
#include "../core/types.h"

namespace scaler {

void actorTall(u32 out);      // 0xD468: 48x80 from 0xFF6000
void actorWide(u32 out);      // 0xDA6A: 80x48 from 0xFF6000
void copyTall(u32 out);       // 0xD3F0: unscaled 48x80
void copyWide(u32 out);       // 0xD42C: unscaled 80x48
void titleLogoFront();        // 0xE40C: 120x40 from 0xFF3800 to 0xFFE000
void titleLogoBack();         // 0xF272: 64x32 from 0xFF5000 to 0xFFC400

}  // namespace scaler

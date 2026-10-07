// Verb panel, inventory, status line and dialogue.
#pragma once
#include "../core/types.h"

namespace ui {

void highlightVerb(u16 style);   // 0x5BDC: redraw verb 0xFF06C2 in style 0..2
void buildInventoryCells();      // 0x5D1C
void uploadInventory();          // 0x5CB6
void inventoryFrameWork();       // 0x5C8E: deferred upload, run from the frame interrupt
void redrawInventory();          // 0x5CF6
void updatePageArrows();         // 0x5B92
void setArrowUp(u16 state);      // 0x5C5A
void setArrowDown(u16 state);    // 0x5C74
u32 objectName(u16 number);      // 0x5B5E

void clearTopText();             // 0xA172
void showTopText(u32 text);      // 0xA054
void waitMessage();              // 0xA18C
void showMessage(u32 text);      // 0xA130
void refreshStatusLine();        // 0xA828
u16 savePanel();                 // 0x47AC
void openTextBox();              // 0x482E
void closeTextBox();             // 0x4862
void collectChoices();           // 0x2320
void dialogue();                 // 0x1C0C
void talkGesture();              // 0x20DA

}  // namespace ui

// Routines that are referenced but not yet ported. Each one is replaced as
// its part of the engine is brought over.
#include "../core/types.h"
#include "menu.h"

namespace irq {
void titleFlashWork() {
    menu::titleFlash();
    menu::titleFrameWork();
}
}  // namespace irq

namespace menu {
void debugMenu(u16) {}       // 0x88A2 / 0x8C40, not yet ported
}  // namespace menu

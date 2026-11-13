// Shared icon glyph helper for Home Assistant media controls.
#include "deck_ui_int.h"

namespace deckui {
void mediaGlyph(lv_obj_t* label, const char* name, const char* fallback, int px) {
  const uint32_t cp = iconCodepoint(name);
  const lv_font_t* font = cp ? iconFont(px, cp) : nullptr;
  char text[5];
  if (font) iconUtf8(cp, text);
  setFont(label, font ? font : &deck_font_20);
  setText(label, font ? text : fallback);
}


}  // namespace deckui

#ifndef UI_H
#define UI_H

#include <raylib.h>

typedef struct {
  const char *label; // e.g., "Shadow maps"
  const char *value; // e.g., "1.2ms"
} UiTextItem;

// Returns the total height of the drawn box (useful for stacking multiple
// boxes)
float draw_ui_text_box(Vector2 position, // top-left corner
                       const char *title, UiTextItem *items, int item_count,
                       Color title_color, Color text_color, int font_size);

#endif

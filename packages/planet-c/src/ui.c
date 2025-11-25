#include "ui.h"
#include <raylib.h>
#include <stdint.h>
#include <stdio.h>

const int LINE_THICKNESS = 5;
const int PADDING = 10;
const int ITEM_SPACING = 5;
const int LABEL_VALUE_GAP = 2;

static inline int max_int(int a, int b) { return (a > b) ? a : b; }

float draw_ui_text_box(Vector2 position, // top-left corner
                       const char *title, UiTextItem *items, int item_count,
                       Color title_color, Color text_color, int font_size) {
  int height = 0;
  const int titleTextWidth = MeasureText(title, font_size);

  int largestTextWidth = 0;
  char item_text[256];
  for (unsigned short i = 0; i < item_count; i++) {
    snprintf(item_text, sizeof(item_text), "%s: %s", items[i].label,
             items[i].value);
    largestTextWidth =
        max_int(largestTextWidth, MeasureText(item_text, font_size));
  }

  int min_box_width = largestTextWidth + (PADDING * 2);
  int title_header_width = 10 + 5 + titleTextWidth + 5 + 10;
  int box_width = max_int(min_box_width, title_header_width);
  int box_right_x = position.x + box_width;

  float top_line_y = position.y + font_size * 0.55f;

  DrawLineEx((Vector2){position.x - 2.5, top_line_y},
             (Vector2){position.x + 10, top_line_y}, 5, title_color);
  DrawText(title, position.x + 15, position.y, font_size, title_color);
  DrawLineEx((Vector2){position.x + 15 + titleTextWidth + 5, top_line_y},
             (Vector2){box_right_x + 2.5, top_line_y}, 5, title_color);
  height += font_size;

  float box_top_y = position.y + font_size + 5;
  float box_height = (PADDING * 2) + (item_count * font_size) +
                     ((item_count - 1) * ITEM_SPACING);

  int current_y = box_top_y + PADDING;
  for (unsigned short i = 0; i < item_count; i++) {
    snprintf(item_text, sizeof(item_text), "%s: %s", items[i].label,
             items[i].value);
    DrawText(item_text, position.x + PADDING, current_y, font_size, text_color);
    current_y += font_size + ITEM_SPACING;
  }

  // Left Line
  DrawLineEx((Vector2){position.x, top_line_y},
             (Vector2){position.x, current_y + PADDING}, 5, title_color);
  // Right line
  DrawLineEx((Vector2){position.x + box_width, top_line_y},
             (Vector2){position.x + box_width, current_y + PADDING}, 5,
             title_color);
  // Bottom line
  DrawLineEx((Vector2){position.x - 2.5, current_y + PADDING},
             (Vector2){position.x + box_width + 2.5, current_y + PADDING}, 5,
             title_color);

  height = font_size + 5 + box_height;

  return height;
}

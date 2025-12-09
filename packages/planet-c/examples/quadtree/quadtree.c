#include <raylib.h>
#include <raymath.h>
#include <stddef.h>
#include "../../src/quadtree.h"

// Draw quadtree visualization (example-specific, not in library)
void draw_quadtree_node(QuadTreeNode *node) {
  if (node == NULL) {
    return;
  }

  DrawRectangleLines(node->x + 1, node->y + 1, node->width - 1,
                     node->height - 1, GREEN);

  for (unsigned short i = 0; i < 4; i++) {
    draw_quadtree_node(node->children[i]);
  }
}

int main(void) {
  // ===== Raylib Init =====
  const int screenWidth = 1280;
  const int screenHeight = 720;
  InitWindow(screenWidth, screenHeight, "Planet Renderer - Shadows");
  SetTargetFPS(60);
  ToggleBorderlessWindowed();

  // ===== Initialize Quadtree =====
  QuadTreeNode *root_node = create_quadtree_node(10, 60, 1280, 1280, 0, NULL);

  // ===== Mode State =====
  typedef enum { MODE_MANUAL, MODE_AUTOMATIC } Mode;
  Mode current_mode = MODE_MANUAL;

  // ===== UI Buttons =====
  Rectangle manual_button = {10, 10, 100, 40};
  Rectangle automatic_button = {120, 10, 100, 40};

  while (!WindowShouldClose()) {
    Vector2 mouse_pos = GetMousePosition();

    // ===== Button Input =====
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      if (CheckCollisionPointRec(mouse_pos, manual_button)) {
        current_mode = MODE_MANUAL;
      } else if (CheckCollisionPointRec(mouse_pos, automatic_button)) {
        current_mode = MODE_AUTOMATIC;
      }
    }

    // ===== Mode Logic =====
    BeginDrawing();
    ClearBackground(BLACK);

    switch (current_mode) {
    case MODE_MANUAL: {
      QuadTreeNode *hovered =
          find_quadtree_node_at_point(root_node, mouse_pos.x, mouse_pos.y);

      if (IsMouseButtonPressed(0) && hovered &&
          !CheckCollisionPointRec(mouse_pos, manual_button) &&
          !CheckCollisionPointRec(mouse_pos, automatic_button)) {
        subdivide_quadtree_node(hovered, NULL, NULL);
      }

      if (IsMouseButtonPressed(1) && hovered) {
        merge_quadtree_node(hovered, NULL, NULL);
      }

      // Draw quadtree
      draw_quadtree_node(root_node);

      // Draw hovered node
      if (hovered) {
        DrawRectangle(hovered->x + 1, hovered->y + 1, hovered->width - 1,
                      hovered->height - 1, GREEN);
      }

      break;
    }
    case MODE_AUTOMATIC: {
      process_leaf_nodes(root_node, mouse_pos, 8, NULL, NULL);
      merge_distant_leaves(root_node, mouse_pos, NULL, NULL);
      draw_quadtree_node(root_node);

      break;
    }
    }

    // ===== Draw UI =====
    Color manual_color = (current_mode == MODE_MANUAL) ? DARKGREEN : DARKGRAY;
    Color automatic_color =
        (current_mode == MODE_AUTOMATIC) ? DARKGREEN : DARKGRAY;

    DrawRectangleRec(manual_button, manual_color);
    DrawRectangleLinesEx(manual_button, 2, WHITE);
    DrawText("Manual", 20, 20, 20, WHITE);

    DrawRectangleRec(automatic_button, automatic_color);
    DrawRectangleLinesEx(automatic_button, 2, WHITE);
    DrawText("Auto", 140, 20, 20, WHITE);

    EndDrawing();
  }

  delete_quadtree_node(root_node, NULL);
  CloseWindow();

  return 0;
}

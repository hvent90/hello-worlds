#include <raylib.h>
#include <raymath.h>
#include <stdlib.h>

// ===== Quadtree Definitions
typedef struct QuadTreeNode {
  // bounds
  float x, y, width, height;

  // tree structure
  struct QuadTreeNode *parent;
  struct QuadTreeNode *children[4];

  // metadata
  int depth;
} QuadTreeNode;

QuadTreeNode *create_quadtree_node(float x, float y, float width, float height,
                                   int depth, QuadTreeNode *parent) {
  QuadTreeNode *node = malloc(sizeof(QuadTreeNode));
  node->x = x;
  node->y = y;
  node->width = width;
  node->height = height;
  node->depth = depth;
  node->parent = parent;

  for (unsigned short i = 0; i < 4; i++) {
    node->children[i] = NULL;
  }

  return node;
}

void delete_quadtree_node(QuadTreeNode *node) {
  if (node == NULL) {
    return;
  }

  for (unsigned short i = 0; i < 4; i++) {
    delete_quadtree_node(node->children[i]);
  }

  free(node);
}

void subdivide_quadtree_node(QuadTreeNode *node) {
  if (node == NULL) {
    return;
  }

  if (node->children[0] != NULL) {
    return;
  }

  const float width = node->width * 0.5f;
  const float height = node->height * 0.5f;

  for (unsigned short i = 0; i < 4; i++) {
    const float x = i % 2 * width + node->x;
    const float y = (i >= 2) * height + node->y;
    node->children[i] =
        create_quadtree_node(x, y, width, height, node->depth + 1, node);
  }
}

void merge_quadtree_node(QuadTreeNode *node) {
  if (node == NULL || node->parent == NULL) {
    return;
  }

  for (unsigned short i = 0; i < 4; i++) {
    delete_quadtree_node(node->parent->children[i]);
    node->parent->children[i] = NULL;
  }
}

QuadTreeNode *find_quadtree_node_at_point(QuadTreeNode *node, int x, int y) {
  if (node == NULL) {
    return NULL;
  }

  int in_bounds = x >= node->x && x <= node->x + node->width && y >= node->y &&
                  y <= node->y + node->height;
  if (!in_bounds) {
    return NULL;
  }

  for (unsigned short i = 0; i < 4; i++) {
    QuadTreeNode *found = find_quadtree_node_at_point(node->children[i], x, y);
    if (found) {
      return found;
    }
  }

  return node;
}

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

void process_leaf_nodes(QuadTreeNode *node, Vector2 mouse_pos, int max_depth) {
  if (node == NULL) {
    return;
  }

  int is_leaf = node->children[0] == NULL;

  if (!is_leaf) {
    for (unsigned short i = 0; i < 4; i++) {
      process_leaf_nodes(node->children[i], mouse_pos, max_depth);
    }

    return;
  }

  const float distance = Vector2Distance(
      (Vector2){node->x + node->width / 2.0f, node->y + node->height / 2.0f},
      mouse_pos);
  const float subdivide_distance = node->width;

  // Check for subdivision
  if (node->depth < max_depth && distance < subdivide_distance) {
    subdivide_quadtree_node(node);
  }
}

void merge_distant_leaves(QuadTreeNode *node, Vector2 mouse_pos) {
  if (node == NULL) {
    return;
  }

  // Skip leaves first
  if (node->children[0] == NULL) {
    return;
  }

  // Recurse into children first
  for (unsigned short i = 0; i < 4; i++) {
    merge_distant_leaves(node->children[i], mouse_pos);
  }

  // Make sure all children are leaves
  for (unsigned short i = 0; i < 4; i++) {
    if (node->children[i]->children[0] != NULL) {
      return;
    }
  }

  const float merge_distance = node->width * 1.1;

  for (unsigned short i = 0; i < 4; i++) {
    const float distance = Vector2Distance(
        (Vector2){node->children[i]->x + node->children[i]->width / 2.0f,
                  node->children[i]->y + node->children[i]->height / 2.0f},
        mouse_pos);

    if (distance < merge_distance) {
      return;
    }
  }

  merge_quadtree_node(node->children[0]);
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
        subdivide_quadtree_node(hovered);
      }

      if (IsMouseButtonPressed(1) && hovered) {
        merge_quadtree_node(hovered);
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
      process_leaf_nodes(root_node, mouse_pos, 8);
      merge_distant_leaves(root_node, mouse_pos);
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

  delete_quadtree_node(root_node);
  CloseWindow();

  return 0;
}

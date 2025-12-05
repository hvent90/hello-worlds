#include <raylib.h>
#include <raymath.h>
#include "quadtree.h"
#include <stdlib.h>

QuadTreeNode *create_quadtree_node(float x, float y, float width, float height,
                                   int depth, QuadTreeNode *parent) {
  QuadTreeNode *node = malloc(sizeof(QuadTreeNode));
  node->x = x;
  node->y = y;
  node->width = width;
  node->height = height;
  node->depth = depth;
  node->parent = parent;
  node->user_data = NULL;

  for (unsigned short i = 0; i < 4; i++) {
    node->children[i] = NULL;
  }

  return node;
}

void delete_quadtree_node(QuadTreeNode *node,
                          QuadTreeUserDataCleanupFunc cleanup) {
  if (node == NULL) {
    return;
  }

  // Recursively delete children
  for (unsigned short i = 0; i < 4; i++) {
    delete_quadtree_node(node->children[i], cleanup);
  }

  // Clean up user data if cleanup function provided
  if (cleanup && node->user_data) {
    cleanup(node->user_data);
  }

  free(node);
}

void subdivide_quadtree_node(QuadTreeNode *node,
                             QuadTreeUserDataCreateFunc create_child_data) {
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

    // Create user data for child if callback provided
    if (create_child_data) {
      node->children[i]->user_data =
          create_child_data(node, node->children[i], node->user_data);
    }
  }
}

void merge_quadtree_node(QuadTreeNode *node,
                         QuadTreeUserDataCleanupFunc cleanup) {
  if (node == NULL || node->parent == NULL) {
    return;
  }

  for (unsigned short i = 0; i < 4; i++) {
    delete_quadtree_node(node->parent->children[i], cleanup);
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

void process_leaf_nodes(QuadTreeNode *node, Vector2 point, int max_depth,
                       QuadTreeUserDataCreateFunc create_child_data) {
  if (node == NULL) {
    return;
  }

  int is_leaf = node->children[0] == NULL;

  if (!is_leaf) {
    for (unsigned short i = 0; i < 4; i++) {
      process_leaf_nodes(node->children[i], point, max_depth, create_child_data);
    }

    return;
  }

  const float distance = Vector2Distance(
      (Vector2){node->x + node->width / 2.0f, node->y + node->height / 2.0f},
      point);
  const float subdivide_distance = node->width;

  // Check for subdivision
  if (node->depth < max_depth && distance < subdivide_distance) {
    subdivide_quadtree_node(node, create_child_data);
  }
}

void merge_distant_leaves(QuadTreeNode *node, Vector2 point,
                         QuadTreeUserDataCleanupFunc cleanup) {
  if (node == NULL) {
    return;
  }

  // Skip leaves first
  if (node->children[0] == NULL) {
    return;
  }

  // Recurse into children first
  for (unsigned short i = 0; i < 4; i++) {
    merge_distant_leaves(node->children[i], point, cleanup);
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
        point);

    if (distance < merge_distance) {
      return;
    }
  }

  merge_quadtree_node(node->children[0], cleanup);
}

#ifndef QUADTREE_H
#define QUADTREE_H

// NOTE: Include raylib.h before this header to get Vector2 definition

// ===== Quadtree Definitions =====

typedef struct QuadTreeNode QuadTreeNode;

// Callback to clean up user_data when deleting a node
typedef void (*QuadTreeUserDataCleanupFunc)(void *user_data);

// Callback to create user_data for child nodes during subdivision
// Parameters: parent node, child node, parent's user_data
// Returns: user_data for the child node
typedef void *(*QuadTreeUserDataCreateFunc)(QuadTreeNode *parent,
                                             QuadTreeNode *child,
                                             void *parent_user_data);

struct QuadTreeNode {
  // Bounds
  float x, y, width, height;

  // Tree structure
  QuadTreeNode *parent;
  QuadTreeNode *children[4];

  // Metadata
  int depth;

  // Generic user data (can be NULL)
  void *user_data;
};

// Core quadtree operations
QuadTreeNode *create_quadtree_node(float x, float y, float width, float height,
                                   int depth, QuadTreeNode *parent);
void delete_quadtree_node(QuadTreeNode *node,
                          QuadTreeUserDataCleanupFunc cleanup);
void subdivide_quadtree_node(QuadTreeNode *node,
                             QuadTreeUserDataCreateFunc create_child_data);
void merge_quadtree_node(QuadTreeNode *node,
                         QuadTreeUserDataCleanupFunc cleanup);
QuadTreeNode *find_quadtree_node_at_point(QuadTreeNode *node, int x, int y);

// LOD operations (distance-based subdivision/merging)
void process_leaf_nodes(QuadTreeNode *node, Vector2 point, int max_depth,
                       QuadTreeUserDataCreateFunc create_child_data);
void merge_distant_leaves(QuadTreeNode *node, Vector2 point,
                         QuadTreeUserDataCleanupFunc cleanup);

#endif // QUADTREE_H

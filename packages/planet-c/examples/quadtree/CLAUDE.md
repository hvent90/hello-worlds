# Quadtree Visualizer

## Goal

Interactive 2D quadtree visualization for learning and debugging quadtree data structures using raylib.

## Claude's Role

**Guide, don't write.** Provide explanations, step-by-step instructions, and help the user understand what to implement - but do NOT write the code for them. This is a learning exercise.

## Requirements

**Visualization:**

- 2D top-down view (screen = root quad boundary)
- Draw quad boundaries as colored lines (different color per depth level)
- Display stats: total quad count, max depth

**Interaction:**

- **Left-click** any quad → subdivide into 4 children
- **Right-click** any quad → merge with all siblings, recursively delete all descendants

**Data Structure:**

- QuadTreeNode struct with: bounds (x, y, width, height), parent pointer, children[4] pointers, depth level

## Implementation Plan

1. **QuadTreeNode Structure & Memory**
   - Define `QuadTreeNode` struct (bounds, parent, children[4], depth)
   - Implement `create_quadtree_node()` and `delete_quadtree_node()` (recursive cleanup)

2. **Subdivision**
   - Implement `subdivide_quadtree_node()`: allocate 4 children, set bounds/parent/depth
   - Calculate child bounds (half width/height, appropriate offsets)

3. **Merge**
   - Implement `merge_quadtree_siblings()`: recursively delete all siblings and their descendants
   - Null out parent's children pointers, making it a leaf

4. **Mouse Picking**
   - Implement `find_quadtree_node_at_point()`: recursive search for deepest leaf containing mouse position
   - Handle both leaf quads and parent quads

5. **Rendering**
   - Implement `draw_quadtree_node()`: recursive traversal, draw boundaries with depth-based color
   - Draw stats overlay

6. **Main Loop**
   - Initialize root quad (full screen bounds)
   - Handle mouse input (left/right click detection)
   - Call find_quadtree_node_at_point, subdivide_quadtree_node/merge_quadtree_siblings, draw_quadtree_node

## Scope Notes

- **Standalone raylib application** - independent of main project code
- **Raylib application structure (three-phase pattern):**
  - **Initialization phase:**
    - InitWindow(width, height, title) to create window
    - SetTargetFPS(60) to limit frame rate
    - Optional window config: DisableCursor(), ToggleBorderlessWindowed()
    - Load all resources upfront (no resources loaded during main loop)
  - **Main loop phase:**
    - while (!WindowShouldClose()) wraps entire loop
    - Input handling: IsMouseButtonPressed(MOUSE_BUTTON_LEFT/RIGHT), GetMousePosition()
    - State updates based on input
    - Rendering wrapped in BeginDrawing() / EndDrawing()
    - ClearBackground(COLOR) immediately after BeginDrawing()
    - All draw calls between Begin/End (DrawLine, DrawRectangleLines, DrawText)
  - **Cleanup phase:**
    - Free/unload all resources that were allocated
    - CloseWindow() as final call
- Use 2D drawing functions: DrawLine, DrawRectangleLines, DrawText, etc.
- Mouse input: IsMouseButtonPressed(MOUSE_BUTTON_LEFT/RIGHT), GetMousePosition()
- Keep it simple: ~150-200 lines total
- Focus on clarity over optimization

## Implementation Guide: Mesh-Based Quadtree LOD

  Step 1: Define the User Data Structure

  First, you need a structure to store in each node's user_data field.

  Create a new struct (probably at the top of quadtree_mesh.c):
  typedef struct {
      Mesh mesh;
      Vector3 position;  // World-space position of this mesh patch
      float scale;       // Size of this mesh patch
  } QuadTreeMeshData;

  This stores everything needed to render a terrain patch: the mesh itself,
  where to draw it, and how big it is.

  ---
  Step 2: Implement the Cleanup Callback

  This function will be called when nodes are deleted or merged.

  Signature: void cleanup_mesh_data(void *user_data)

  What it should do:

- Cast user_data to QuadTreeMeshData*
- Call UnloadMesh() on the mesh
- Free the allocated structure

  Why: Prevents memory leaks when terrain chunks are unloaded as you fly away.

  ---
  Step 3: Implement the Create Child Data Callback

  This function generates meshes for child nodes during subdivision.

  Signature: void*create_child_mesh_data(QuadTreeNode*parent, QuadTreeNode
  *child, void*parent_user_data)

  What it should do:

  1. Cast parent_user_data to QuadTreeMeshData* to access parent info
  2. Calculate child's scale (parent scale / 2)
  3. Calculate child's world position based on:
  - Parent position
  - Child's index in parent->children[] array (0=top-left, 1=top-right,
  2=bottom-left, 3=bottom-right)
  - Offset from parent's center
  4. Generate mesh with create_plane_mesh_with_noise(child_scale, resolution)
  5. Allocate and populate a new QuadTreeMeshData struct
  6. Return it

  Why: Each subdivision creates 4 children at half the size, positioned to tile
   seamlessly.

  ---
  Step 4: Create the Root Node with Initial Mesh

  In main(), replace the current root node creation with one that includes mesh
   data.

  Steps:

  1. Allocate a QuadTreeMeshData struct
  2. Generate a large root mesh (e.g., create_plane_mesh_with_noise(10000.0f,
  50))
  3. Set position to world origin or offset appropriately
  4. Set scale to match mesh size
  5. Create root node with create_quadtree_node() as usual
  6. Manually assign the allocated mesh data to root_node->user_data

  Why: The root node is the initial terrain chunk that covers everything before
   any subdivision.

  ---
  Step 5: Convert to 3D Rendering

  Replace the 2D raylib drawing code with 3D.

  Changes needed:

  1. Create a Camera3D instead of drawing 2D rectangles
  2. Initialize it with position, target, up vector, fovy, and projection type
  3. Wrap rendering in BeginMode3D(camera) / EndMode3D()
  4. Replace draw_quadtree_node() with a 3D version

  ---
  Step 6: Implement 3D Quadtree Drawing Function

  Create a recursive function to draw all nodes' meshes.

  Signature: void draw_quadtree_meshes(QuadTreeNode *node, Material material)

  What it should do:

  1. Base case: if node is NULL, return
  2. If node has user_data:
  - Cast to QuadTreeMeshData*
  - Create transform matrix with MatrixTranslate(position.x, position.y,
  position.z)
  - Call DrawMesh(mesh_data->mesh, material, transform_matrix)
  3. Recursively call itself on all 4 children

  Why: Only leaf nodes (no children) or nodes that haven't subdivided yet will
  actually render. This creates the LOD effect.

  ---
  Step 7: Wire Up Callbacks to Subdivision/Merge

  Update your manual and automatic mode code.

  Manual mode changes:

- Pass create_child_mesh_data to subdivide_quadtree_node(hovered,
  create_child_mesh_data)
- Pass cleanup_mesh_data to merge_quadtree_node(hovered, cleanup_mesh_data)

  Automatic mode changes:

- Pass create_child_mesh_data to process_leaf_nodes()
- Pass cleanup_mesh_data to merge_distant_leaves()

  Why: This hooks up mesh generation/destruction to the quadtree operations.

  ---
  Step 8: Add Camera Movement

  Implement simple camera controls to fly around and test LOD.

  Basic WASD movement:

- Use IsKeyDown() to detect W/A/S/D keys
- Update camera.position based on key state and deltaTime
- You can copy camera movement from planet_example.c or implement simpler
  version

  Why: You need to move around to see the distance-based LOD system work.

  ---
  Step 9: Cleanup on Exit

  Before CloseWindow(), properly cleanup the quadtree.

  What to do:

- Call delete_quadtree_node(root_node, cleanup_mesh_data)
- This recursively unloads all meshes and frees all nodes

  Why: Proper resource cleanup prevents memory leaks.

  ---
  Step 10: Test and Debug

  Testing sequence:

  1. Run program - should see root mesh rendering
  2. Manual mode: click on mesh - should subdivide into 4 smaller meshes
  3. Automatic mode: fly toward terrain - should see progressive subdivision
  4. Fly away - should see meshes merge back together

  Common issues to watch for:

- Child mesh positions not aligning (check offset calculations)
- Meshes not unloading (verify cleanup callback is called)
- Crashes on merge (ensure parent mesh data is preserved or regenerated)
- Z-fighting at boundaries (mesh vertices need to align exactly)

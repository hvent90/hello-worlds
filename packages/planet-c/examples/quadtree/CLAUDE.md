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

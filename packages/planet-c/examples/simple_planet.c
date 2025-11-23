#include <stdio.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

int main(void) {
  const int screenWidth = 1280;
  const int screenHeight = 720;

  InitWindow(screenWidth, screenHeight, "Planet Renderer");
  DisableCursor();

  // rlSetClipPlanes(0.1, 10000000.0);

  Camera3D camera = { 0 };
  camera.position = (Vector3){ 0.0f, 100.0f, 100.0f };
  camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
  camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
  camera.fovy = 45.0f;
  camera.projection = CAMERA_PERSPECTIVE;

  SetTargetFPS(60);

  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(BLACK);

    UpdateCamera(&camera, CAMERA_FIRST_PERSON);
    
    BeginMode3D(camera);
    DrawGrid(1000, 1.0f);
    EndMode3D();

    DrawText("WASD to move, Mouse to look", 10, 10, 20, RAYWHITE);
    EndDrawing();
  }

  CloseWindow();

  return 0;
}

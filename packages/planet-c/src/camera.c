#include "camera.h"
#include "raymath.h"
#include <raylib.h>

float UpdateCameraMovement(Camera3D *camera, const float deltaTime) {
  // Quaternion-based camera orientation (static to persist between frames)
  static Quaternion orientation = {0.0f, 0.0f, 0.0f, 1.0f}; // Identity quaternion
  static bool initialized = false;
  static float movementSpeed = 50.0f;
  const float mouseSensitivity = 0.1f;
  const float rollSpeed = 60.0f; // Degrees per second
  const float sprint_multiplier = 50.0f;
  float is_sprinting = IsKeyDown(KEY_LEFT_SHIFT);

  // Initialize orientation to look toward -Z on first call
  if (!initialized) {
    orientation = QuaternionFromAxisAngle((Vector3){0.0f, 1.0f, 0.0f}, -90.0f * DEG2RAD);
    initialized = true;
  }

  // Mouse wheel for speed control (logarithmic scaling)
  float wheelMove = GetMouseWheelMove();
  if (wheelMove != 0.0f) {
    const float maxSpeed = 500000.0f;
    const float minSpeed = 1.0f;
    movementSpeed *= (1.0f + wheelMove * 0.2f);
    if (movementSpeed < minSpeed)
      movementSpeed = minSpeed;
    if (movementSpeed > maxSpeed)
      movementSpeed = maxSpeed;
  }

  // Extract current camera axes from orientation
  Vector3 forward = Vector3RotateByQuaternion((Vector3){0.0f, 0.0f, -1.0f}, orientation);
  Vector3 right = Vector3RotateByQuaternion((Vector3){1.0f, 0.0f, 0.0f}, orientation);
  Vector3 up = Vector3RotateByQuaternion((Vector3){0.0f, 1.0f, 0.0f}, orientation);

  // Mouse look - rotate around camera's local axes
  Vector2 mouseDelta = GetMouseDelta();
  if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
    // Yaw: rotate around camera's local up axis
    float yawAngle = -mouseDelta.x * mouseSensitivity * DEG2RAD;
    Quaternion yawRotation = QuaternionFromAxisAngle(up, yawAngle);

    // Pitch: rotate around camera's local right axis
    float pitchAngle = -mouseDelta.y * mouseSensitivity * DEG2RAD;
    Quaternion pitchRotation = QuaternionFromAxisAngle(right, pitchAngle);

    // Apply rotations: first yaw, then pitch
    orientation = QuaternionMultiply(yawRotation, orientation);
    orientation = QuaternionMultiply(pitchRotation, orientation);
    orientation = QuaternionNormalize(orientation);

    // Recalculate axes after rotation
    forward = Vector3RotateByQuaternion((Vector3){0.0f, 0.0f, -1.0f}, orientation);
    right = Vector3RotateByQuaternion((Vector3){1.0f, 0.0f, 0.0f}, orientation);
    up = Vector3RotateByQuaternion((Vector3){0.0f, 1.0f, 0.0f}, orientation);
  }

  // Roll controls (Q/E) - rotate around camera's local forward axis
  if (IsKeyDown(KEY_Q) || IsKeyDown(KEY_E)) {
    float rollAngle = 0.0f;
    if (IsKeyDown(KEY_Q))
      rollAngle -= rollSpeed * deltaTime * DEG2RAD;
    if (IsKeyDown(KEY_E))
      rollAngle += rollSpeed * deltaTime * DEG2RAD;

    Quaternion rollRotation = QuaternionFromAxisAngle(forward, rollAngle);
    orientation = QuaternionMultiply(rollRotation, orientation);
    orientation = QuaternionNormalize(orientation);

    // Recalculate up and right after roll
    right = Vector3RotateByQuaternion((Vector3){1.0f, 0.0f, 0.0f}, orientation);
    up = Vector3RotateByQuaternion((Vector3){0.0f, 1.0f, 0.0f}, orientation);
  }

  // Movement input (camera-relative using local axes)
  Vector3 movement = {0.0f, 0.0f, 0.0f};

  if (IsKeyDown(KEY_W))
    movement = Vector3Add(movement, forward);
  if (IsKeyDown(KEY_S))
    movement = Vector3Subtract(movement, forward);
  if (IsKeyDown(KEY_D))
    movement = Vector3Add(movement, right);
  if (IsKeyDown(KEY_A))
    movement = Vector3Subtract(movement, right);
  if (IsKeyDown(KEY_SPACE))
    movement = Vector3Add(movement, up); // Camera-relative up
  if (IsKeyDown(KEY_LEFT_CONTROL))
    movement = Vector3Subtract(movement, up); // Camera-relative down

  // Normalize movement vector if moving (to prevent faster diagonal movement)
  if (Vector3Length(movement) > 0.0f) {
    movement = Vector3Normalize(movement);
    movement =
        Vector3Scale(movement, movementSpeed * deltaTime *
                                   (is_sprinting ? sprint_multiplier : 1));
    camera->position = Vector3Add(camera->position, movement);
  }

  // Update camera target and up
  camera->target = Vector3Add(camera->position, Vector3Scale(forward, 1000.0f));
  camera->up = up;

  return movementSpeed; // Return speed for UI display
}

#include "camera.h"
#include "raymath.h"

float UpdateCameraMovement(Camera3D *camera, const float deltaTime) {
    // Floating pawn control variables (static to persist between frames)
    static float pitch = 0.0f;
    static float yaw = -90.0f; // Start looking toward -Z
    static float roll = 0.0f;
    static float movementSpeed = 50.0f;
    const float mouseSensitivity = 0.1f;
    const float rollSpeed = 60.0f; // Degrees per second

    // Mouse wheel for speed control (logarithmic scaling)
    float wheelMove = GetMouseWheelMove();
    if (wheelMove != 0.0f) {
        const float maxSpeed = 500000.0f;
        const float minSpeed = 1.0f;
        movementSpeed *= (1.0f + wheelMove * 0.2f);
        if (movementSpeed < minSpeed) movementSpeed = minSpeed;
        if (movementSpeed > maxSpeed) movementSpeed = maxSpeed;
    }

    // Mouse look (pitch and yaw)
    Vector2 mouseDelta = GetMouseDelta();
    yaw += mouseDelta.x * mouseSensitivity;
    pitch -= mouseDelta.y * mouseSensitivity;

    // Clamp pitch to prevent gimbal lock
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    // Roll controls (Q/E)
    if (IsKeyDown(KEY_Q)) roll -= rollSpeed * deltaTime;
    if (IsKeyDown(KEY_E)) roll += rollSpeed * deltaTime;

    // Calculate camera forward vector from pitch and yaw
    Vector3 forward = {
        cosf(pitch * DEG2RAD) * cosf(yaw * DEG2RAD),
        sinf(pitch * DEG2RAD),
        cosf(pitch * DEG2RAD) * sinf(yaw * DEG2RAD)
    };
    forward = Vector3Normalize(forward);

    // Calculate right vector (perpendicular to forward and world up)
    Vector3 worldUp = {0.0f, 1.0f, 0.0f};
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, worldUp));

    // Calculate camera up vector (perpendicular to forward and right)
    Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));

    // Apply roll rotation to up vector
    if (roll != 0.0f) {
        up = Vector3RotateByAxisAngle(up, forward, roll * DEG2RAD);
        right = Vector3Normalize(Vector3CrossProduct(forward, up));
    }

    // Movement input (camera-relative)
    Vector3 movement = {0.0f, 0.0f, 0.0f};

    if (IsKeyDown(KEY_W)) movement = Vector3Add(movement, forward);
    if (IsKeyDown(KEY_S)) movement = Vector3Subtract(movement, forward);
    if (IsKeyDown(KEY_D)) movement = Vector3Add(movement, right);
    if (IsKeyDown(KEY_A)) movement = Vector3Subtract(movement, right);
    if (IsKeyDown(KEY_SPACE)) movement = Vector3Add(movement, worldUp); // World-relative up
    if (IsKeyDown(KEY_LEFT_CONTROL)) movement = Vector3Subtract(movement, worldUp); // World-relative down

    // Normalize movement vector if moving (to prevent faster diagonal movement)
    if (Vector3Length(movement) > 0.0f) {
        movement = Vector3Normalize(movement);
        movement = Vector3Scale(movement, movementSpeed * deltaTime);
        camera->position = Vector3Add(camera->position, movement);
    }

    // Update camera target and up
    camera->target = Vector3Add(camera->position, forward);
    camera->up = up;

    return movementSpeed;  // Return speed for UI display
}

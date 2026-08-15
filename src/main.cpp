#include "raylib.h"
#include <cinttypes>
#include <cstdint>
#include <bit>
#include "rlgl.h"
#include <unordered_map>
#include "raymath.h"
#include <iostream>
#include <cmath>

const int width = 800;
const int height = 800;

inline static Vector3 GetScreenToWorldRayOptimized(Vector2 position, Camera camera, int width, int height, Matrix matView, Matrix viewInv, Matrix matProj)
{
    float x = (2.0f * position.x) / (float)width - 1.0f;
    float y = 1.0f - (2.0f * position.y) / (float)height;

    const float p0 = 1.73205;
    const float p5 = 1.73205;

    float viewX = x / p0;
    float viewY = y / p5;
    Vector2 viewDir = { viewX, viewY };

    Vector3 worldDir = {
        viewInv.m0 * viewDir.x + viewInv.m4 * viewDir.y + viewInv.m8,
        viewInv.m1 * viewDir.x + viewInv.m5 * viewDir.y + viewInv.m9,
        viewInv.m2 * viewDir.x + viewInv.m6 * viewDir.y + viewInv.m10
    };

    float length = sqrtf(worldDir.x*worldDir.x + worldDir.y*worldDir.y + worldDir.z*worldDir.z);
    if (length > 0.0f) {
        worldDir.x /= length;
        worldDir.y /= length;
        worldDir.z /= length;
    }

    return worldDir;
}

class App {
    public:
    Camera camera;
    Matrix matProj;
    Image imageBuffer;
    Texture displayBuffer;
    App() {
        InitWindow(width,height,"Voxelized");
        camera.position = (Vector3){ 0.0f, 2.0f, 4.0f };
        camera.target = (Vector3){ 0.0f, 2.0f, 0.0f };
        camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        camera.fovy = 60.0f;
        camera.projection = CAMERA_PERSPECTIVE;
        matProj = MatrixIdentity();
        matProj = MatrixPerspective(camera.fovy*DEG2RAD, ((double)width/(double)height), 0.01f, 10000.0f);
        imageBuffer = GenImageColor(width,height,BLACK);
        displayBuffer = LoadTextureFromImage(imageBuffer);
        DisableCursor();
    }
    void Run() {
        int frame = 0 ;
        while (!WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(SKYBLUE);
            Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
            Matrix viewInv = MatrixInvert(matView);
            frame++;
            for (int x = 0; x < width; x++) {
                for (int y = 0; y < height; y++) {
                    if ((x + y + frame) % 2 == 0) continue;

                    Vector3 direction = GetScreenToWorldRayOptimized({(float)x,(float)y},camera,width,height, matView,viewInv,matProj);

                    int idx = (y*imageBuffer.width + x)*4;
                    ((unsigned char *)imageBuffer.data)[idx]     = SKYBLUE.r;
                    ((unsigned char *)imageBuffer.data)[idx + 1] = SKYBLUE.g;
                    ((unsigned char *)imageBuffer.data)[idx + 2] = SKYBLUE.b;

                    if (direction.y < 0.0f) {
                        int voxelX = (int)floorf(camera.position.x);
                        int voxelY = (int)floorf(camera.position.y);
                        int voxelZ = (int)floorf(camera.position.z);

                        int stepX = (direction.x > 0.0f) - (direction.x < 0.0f);
                        int stepZ = (direction.z > 0.0f) - (direction.z < 0.0f);

                        float tDeltaX = (direction.x != 0.0f) ? fabsf(1.0f / direction.x) : INFINITY;
                        float tDeltaY = fabsf(1.0f / direction.y);
                        float tDeltaZ = (direction.z != 0.0f) ? fabsf(1.0f / direction.z) : INFINITY;

                        float tMaxX = (direction.x > 0.0f) ? (((voxelX + 1) - camera.position.x) / direction.x) :
                                      (direction.x < 0.0f) ? ((voxelX - camera.position.x) / direction.x) : INFINITY;
                        float tMaxY = (voxelY - camera.position.y) / direction.y;
                        float tMaxZ = (direction.z > 0.0f) ? (((voxelZ + 1) - camera.position.z) / direction.z) :
                                      (direction.z < 0.0f) ? ((voxelZ - camera.position.z) / direction.z) : INFINITY;

                        int steps = 0;
                        while (steps < 1000) {
                            if (tMaxX < tMaxY && tMaxX < tMaxZ) {
                                voxelX += stepX;
                                tMaxX += tDeltaX;
                            } else if (tMaxZ < tMaxY) {
                                voxelZ += stepZ;
                                tMaxZ += tDeltaZ;
                            } else {
                                voxelY--;
                                tMaxY += tDeltaY;
                            }
                                
                            if (voxelY <= 0) {
                                if (voxelX%2==0 || voxelZ%2==0) {
                                    ((unsigned char *)imageBuffer.data)[idx]     = WHITE.r;
                                    ((unsigned char *)imageBuffer.data)[idx + 1] = WHITE.g;
                                    ((unsigned char *)imageBuffer.data)[idx + 2] = WHITE.b;
                                } else {
                                    ((unsigned char *)imageBuffer.data)[idx]     = BLACK.r;
                                    ((unsigned char *)imageBuffer.data)[idx + 1] = BLACK.g;
                                    ((unsigned char *)imageBuffer.data)[idx + 2] = BLACK.b;
                                }
                                break;
                            }
                            steps++;
                        }

                    }
                }
            }
            UpdateTexture(displayBuffer,imageBuffer.data);
            DrawTexture(displayBuffer,0,0,WHITE);
            UpdateCamera(&camera,CAMERA_FREE);
            DrawFPS(0,0);
            EndDrawing();
        }
    }
};

int main() {
    App app;
    app.Run();
}
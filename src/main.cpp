#include "raylib.h"
#include <cinttypes>
#include <cstdint>
#include <bit>
#include "rlgl.h"
#include <unordered_map>
#include "raymath.h"
#include <iostream>
inline Vector3 GetScreenToWorldRayOptimized(Vector2 position, Camera camera, int width, int height, Matrix matView, Matrix viewInv, Matrix matProj)
{
    float x = (2.0f * position.x) / (float)width - 1.0f;
    float y = 1.0f - (2.0f * position.y) / (float)height;
    
    const float p0 = 1.73205;  
    const float p5 = 1.73205;  
    const float p10 = -1; 
    const float p14 = -1; 
    
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
void PrintMatrix(const Matrix& mat, const std::string& name) {
    std::cout << "\n=== " << name << " ===" << std::endl;
    std::cout << "[ " << mat.m0 << "  " << mat.m1 << "  " << mat.m2 << "  " << mat.m3 << " ]" << std::endl;
    std::cout << "[ " << mat.m4 << "  " << mat.m5 << "  " << mat.m6 << "  " << mat.m7 << " ]" << std::endl;
    std::cout << "[ " << mat.m8 << "  " << mat.m9 << "  " << mat.m10 << "  " << mat.m11 << " ]" << std::endl;
    std::cout << "[ " << mat.m12 << "  " << mat.m13 << "  " << mat.m14 << "  " << mat.m15 << " ]" << std::endl;
}
class App {
    public:
    Camera camera; 
    Matrix matProj;
    App() {
        InitWindow(200,200,"Voxelized");    
        camera.position = (Vector3){ 0.0f, 2.0f, 4.0f }; 
        camera.target = (Vector3){ 0.0f, 2.0f, 0.0f };
        camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        camera.fovy = 60.0f;
        camera.projection = CAMERA_PERSPECTIVE;
        matProj = MatrixIdentity();
        matProj = MatrixPerspective(camera.fovy*DEG2RAD, ((double)200/(double)200), 0.01f, 10000.0f);
        
        PrintMatrix(matProj, "matProj (Perspective)");
    }
    void Run() {
        while (!WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(SKYBLUE);
            Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);

                
            Matrix viewInv = MatrixInvert(matView);
            for (int x = 0; x < 200; x++) {
                for (int y = 0; y < 200; y++) {
                    Vector3 direction = GetScreenToWorldRayOptimized({(float)x,(float)y},camera,200,200, matView,viewInv,matProj);
                    Vector3 pos = camera.position;
                    
                    DrawRectangle(x,y,1,1,BLACK);
                    for (int i = 0; i < 100; i++) {
                        pos.y+=direction.y;
                        if (pos.y<0) {
                            DrawRectangle(x,y,1,1,WHITE);
                            break;
                        }
                    }
                }
            }
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
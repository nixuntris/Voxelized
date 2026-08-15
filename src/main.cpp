#include "raylib.h"
#include <cinttypes>
#include <cstdint>
#include <bit>
#include "rlgl.h"
#include <unordered_map>
#include "raymath.h"
class App {
    public:
    Camera camera; 
    App() {
        InitWindow(200,200,"Voxelized");    
        camera.position = (Vector3){ 0.0f, 2.0f, 4.0f }; 
        camera.target = (Vector3){ 0.0f, 2.0f, 0.0f };
        camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        camera.fovy = 60.0f;
        camera.projection = CAMERA_PERSPECTIVE;
    }
    void Run() {
        while (!WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(SKYBLUE);
            for (int x = 0; x < 200; x++) {
                for (int y = 0; y < 200; y++) {
                    Vector3 direction = GetScreenToWorldRay({x,y},camera).direction;
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
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
const int SIZE = 256;
struct World {
    uint8_t voxels[SIZE][SIZE][SIZE];
    uint8_t accel[SIZE/4][SIZE/4][SIZE/4];
    World() {

        Image noise = GenImagePerlinNoise(
            SIZE,
            SIZE,
            0.0f, 
            0.0f, 
            0.02f 
        );

        Color *pixels = LoadImageColors(noise);

        for (int x = 0; x < SIZE/4; x++) {
            for (int z = 0; z < SIZE/4; z++) {
                for (int y = 0; y < SIZE/4; y++) {
                    accel[x][y][z] = 0;
                }
            }
        }
        for (int x = 0; x < SIZE; x++) {
            for (int z = 0; z < SIZE; z++) {


                int height = GetRandomValue(45,50);


                for (int y = 0; y < SIZE; y++) {
                    accel[x/4][y/4][z/4] = 1;
                    voxels[x][y][z] = (y<height)*GetRandomValue(1,3);
                }
            }
        }

        UnloadImageColors(pixels);
        UnloadImage(noise);
    }
};
class App {
    public:
    Camera camera;
    Matrix matProj;
    Image imageBuffer;
    Texture displayBuffer;
    Vector3 *directionStorage;
    Vector3 *accelerationPosition;
    World world;
    int *stepStorage;
    App() {
        InitWindow(width,height,"Voxelized");
        camera.position = (Vector3){ SIZE/2, 50, SIZE/2 };
        camera.target = (Vector3){ 0.0f, 2.0f, 0.0f };
        camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        camera.fovy = 60.0f;
        camera.projection = CAMERA_PERSPECTIVE;
        matProj = MatrixIdentity();
        matProj = MatrixPerspective(camera.fovy*DEG2RAD, ((double)width/(double)height), 0.01f, 10000.0f);
        imageBuffer = GenImageColor(width,height,BLACK);
        displayBuffer = LoadTextureFromImage(imageBuffer);
        directionStorage = (Vector3*)MemAlloc(width*height*sizeof(Vector3));
        accelerationPosition = (Vector3*)MemAlloc(width*height*sizeof(Vector3));
        stepStorage = (int*)MemAlloc(width*height*sizeof(int));
        DisableCursor();
    }
    void Run() {
        int frame = 0;

        const int lowWidth  = width / 4;
        const int lowHeight = height / 4;
        const Color colors[10] = {SKYBLUE,WHITE,BLACK,GRAY};
        while (!WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(SKYBLUE);

            Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
            Matrix viewInv = MatrixInvert(matView);

            frame++;

            for (int x = 0; x < width; x++) {
                for (int y = 0; y < height; y++) {
                    if ((x+y+frame)%2==0) continue; 
                    directionStorage[x + y * width] =
                        GetScreenToWorldRayOptimized(
                            {(float)x, (float)y},
                            camera,
                            width,
                            height,
                            matView,
                            viewInv,
                            matProj
                        );
                }
            }

            for (int x = 0; x < lowWidth; x++) {
                for (int y = 0; y < lowHeight; y++) {

                    int sampleX = x * 4 + 2;
                    int sampleY = y * 4 + 2;

                    Vector3 direction =
                        directionStorage[sampleX + sampleY * width];

                    Vector3 start = camera.position;

                    accelerationPosition[x + y * lowWidth] = start;
                    stepStorage[x + y * lowWidth] = 0;

                    int voxelX = (int)floorf(start.x);
                    int voxelY = (int)floorf(start.y);
                    int voxelZ = (int)floorf(start.z);
                    int stepX = (direction.x > 0.0f) -(direction.x < 0.0f);
                    int stepZ = (direction.z > 0.0f) - (direction.z < 0.0f);
                    float tDeltaX =(direction.x != 0.0f)? fabsf(1.0f / direction.x): INFINITY;
                    float tDeltaY = fabsf(1.0f / direction.y);
                    float tDeltaZ =(direction.z != 0.0f)? fabsf(1.0f / direction.z): INFINITY;
                    float tMaxX =(direction.x > 0.0f)? (((voxelX + 1) - start.x) / direction.x): (direction.x < 0.0f)? ((voxelX - start.x) / direction.x): INFINITY;
                    float tMaxY =(voxelY - start.y) / direction.y;
                    float tMaxZ =(direction.z > 0.0f)? (((voxelZ + 1) - start.z) / direction.z): (direction.z < 0.0f)? ((voxelZ - start.z) / direction.z): INFINITY;
                    float previousT = 0.0f;
                    int steps = 0;
                    const int safetyBacktrack = 4;
                    while (steps < 1000) {

                        previousT =
                            fminf(tMaxX, fminf(tMaxY, tMaxZ));

                        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
                            voxelX += stepX;
                            tMaxX += tDeltaX;
                        }
                        else if (tMaxZ < tMaxY) {
                            voxelZ += stepZ;
                            tMaxZ += tDeltaZ;
                        }
                        else {
                            voxelY--;
                            tMaxY += tDeltaY;
                        }

                        steps++;
                        if (voxelX>=0 && voxelY>=0 && voxelZ>=0 && voxelX<SIZE && voxelY<SIZE && voxelZ<SIZE && world.voxels[voxelX][voxelY][voxelZ]!=0) break;
                    }


                    float safeT =
                        fmaxf(0.0f, previousT - (float)safetyBacktrack);


                    Vector3 accelerationPoint = {
                        start.x + direction.x * safeT,
                        start.y + direction.y * safeT,
                        start.z + direction.z * safeT
                    };

                    accelerationPosition[x + y * lowWidth] =
                        accelerationPoint;

                    stepStorage[x + y * lowWidth] = steps;
                }
            }

            for (int x = 0; x < width; x++) {
                for (int y = 0; y < height; y++) {

                    if ((x+y+frame)%2==0) continue;
                    int idx = (y * imageBuffer.width + x) * 4;

                    ((unsigned char *)imageBuffer.data)[idx] =SKYBLUE.r;
                    ((unsigned char *)imageBuffer.data)[idx + 1] =SKYBLUE.g;
                    ((unsigned char *)imageBuffer.data)[idx + 2] =SKYBLUE.b;
                    ((unsigned char *)imageBuffer.data)[idx + 3] = 255;


                    Vector3 direction =
                        directionStorage[x + y * width];

                //    if (direction.y >= 0.0f)
                 //       continue;


                    int lowX = x / 4;
                    int lowY = y / 4;

                    Vector3 accelPos =
                        accelerationPosition[
                            lowX + lowY * lowWidth
                        ];


                    Vector3 rayStart = accelPos;

                    int voxelX = (int)floorf(rayStart.x);
                    int voxelY = (int)floorf(rayStart.y);
                    int voxelZ = (int)floorf(rayStart.z);


                    int stepX =(direction.x > 0.0f) -(direction.x < 0.0f);

                    int stepZ = (direction.z > 0.0f) -(direction.z < 0.0f);


                    float tDeltaX =(direction.x != 0.0f)? fabsf(1.0f / direction.x): INFINITY;

                    float tDeltaY = fabsf(1.0f / direction.y);

                    float tDeltaZ = (direction.z != 0.0f) ? fabsf(1.0f / direction.z) : INFINITY;

                    float tMaxX =(direction.x > 0.0f)? (((voxelX + 1) - rayStart.x) / direction.x): (direction.x < 0.0f)? ((voxelX - rayStart.x) / direction.x): INFINITY;


                    float tMaxY =(direction.y < 0.0f)? ((voxelY - rayStart.y) / direction.y): ((voxelY + 1 - rayStart.y) / direction.y);


                    float tMaxZ =(direction.z > 0.0f)? (((voxelZ + 1) - rayStart.z) / direction.z): (direction.z < 0.0f)? ((voxelZ - rayStart.z) / direction.z): INFINITY;


                    bool hit = false;

                    int steps = 0;

                    while (steps < 1000) {

                        if (voxelX>=0 && voxelY>=0 && voxelZ>=0 && voxelX<SIZE && voxelY<SIZE && voxelZ<SIZE && world.voxels[voxelX][voxelY][voxelZ]!=0) {
                            

                            ((unsigned char *)imageBuffer.data)[idx] = colors[world.voxels[voxelX][voxelY][voxelZ]].r;
                            ((unsigned char *)imageBuffer.data)[idx + 1] = colors[world.voxels[voxelX][voxelY][voxelZ]].g;
                            ((unsigned char *)imageBuffer.data)[idx + 2] =  colors[world.voxels[voxelX][voxelY][voxelZ]].b;
                            break;
                        }

                        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
                            voxelX += stepX;
                            tMaxX += tDeltaX;
                        }
                        else if (tMaxZ < tMaxY) {
                            voxelZ += stepZ;
                            tMaxZ += tDeltaZ;
                        }
                        else {

                            voxelY--;
                            tMaxY += tDeltaY;
                        }

                        steps++;
                    }
                }
            }
            UpdateTexture(displayBuffer,imageBuffer.data);
            DrawTexture(displayBuffer,0,0,WHITE);
            UpdateCamera(&camera,CAMERA_FREE);
            DrawFPS(0, 0);

            EndDrawing();
        }
    }

};

int main() {
    App *app = new App;
    app->Run();
}
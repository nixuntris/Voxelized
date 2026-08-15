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
const int SIZE = 1024;
struct Chunk {
    uint8_t *voxels;
    bool containsBlocks;
    uint8_t maxX;
    uint8_t maxY;
    uint8_t maxZ;

    void Clear() {
        containsBlocks = false;
        for (int x = 0; x < 32; x++) {
            for (int y= 0 ; y < 32; y++) {
                for (int z = 0; z < 32; z++) {
                    voxels[x*32*32+y*32+z] = 0;
                }
            }
        }
    }
    void CalculateMaxSkipDistance() {
        if (!containsBlocks) {
            maxX = 15; //from the centre
            maxY = 15; 
            maxZ = 15; 
        }
    }
};
struct World {
    Chunk chunks[SIZE/32][SIZE/32][SIZE/32];
    uint8_t GetVoxel(int x, int y, int z) const {
            
        int cx = x / 32;
        int cy = y / 32;
        int cz = z / 32;
        int lx = x % 32;
        int ly = y % 32;
        int lz = z % 32;
        if (!chunks[cx][cy][cz].containsBlocks) return 0;
        return chunks[cx][cy][cz].voxels[lx * 32 * 32 + ly * 32 + lz];
    }

    World() {
        for (int x = 0; x < SIZE/32; x++) {
            for (int y= 0 ; y < SIZE/32; y++) {
                for (int z = 0; z < SIZE/32; z++) {
                    chunks[x][y][z].voxels = (uint8_t*)MemAlloc(32*32*32);
                    chunks[x][y][z].Clear();
                }
            }
        }
        Image noise = GenImagePerlinNoise(
            SIZE,
            SIZE,
            0.0f, 
            0.0f, 
            0.2f 
        );

#pragma omp parallel for collapse(2)
        for (int x = 0; x < SIZE; x++) {
            for (int z = 0; z < SIZE; z++) {

                

                int height = GetImageColor(noise,x,z).r;


                for (int y = 0; y <= height; y++) {
                    if (height==y) {
                        chunks[x/32][y/32][z/32].voxels[(x%32)*32*32+(y%32)*32+z%32] = GetRandomValue(1,2);
                        chunks[x/32][y/32][z/32].containsBlocks = true;
                    }
                    else {
                        chunks[x/32][y/32][z/32].containsBlocks = true;
                        chunks[x/32][y/32][z/32].voxels[(x%32)*32*32+(y%32)*32+z%32] = 1;
                    }
                }
            }
        }
        for (int x = 0; x < SIZE/32; x++) {
            for (int y= 0 ; y < SIZE/32; y++) {
                for (int z = 0; z < SIZE/32; z++) {
                    if (!chunks[x][y][z].containsBlocks) {
                        free(chunks[x][y][z].voxels);
                    }
                }
            }
        }

        UnloadImage(noise);
    }
};
Vector3 sunDirection = Vector3Normalize((Vector3){ 0.8f, 0.2f, 0.2f });
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
        camera.position = (Vector3){ SIZE/2, 128, SIZE/2 };
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
        const Color colors[10] = {SKYBLUE,GREEN,{uint8_t(GREEN.r*0.9),uint8_t(GREEN.g*0.9),uint8_t(GREEN.b*0.9),255},GRAY};
        while (!WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(SKYBLUE);

            Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
            Matrix viewInv = MatrixInvert(matView);

            frame++;

#pragma omp parallel for collapse(2)
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

#pragma omp parallel for collapse(2)
            for (int x = 0; x < lowWidth; x++) {
                for (int y = 0; y < lowHeight; y++) {
                    
                    if ((x+y+frame)%2==0) continue; 
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
                    int stepY = (direction.y > 0.0f) - (direction.y < 0.0f);
                    float tDeltaX =(direction.x != 0.0f)? fabsf(1.0f / direction.x): INFINITY;
                    float tDeltaY = fabsf(1.0f / direction.y);
                    float tDeltaZ =(direction.z != 0.0f)? fabsf(1.0f / direction.z): INFINITY;
                    float tMaxX =(direction.x > 0.0f)? (((voxelX + 1) - start.x) / direction.x): (direction.x < 0.0f)? ((voxelX - start.x) / direction.x): INFINITY;
                    float tMaxY =(direction.y < 0.0f)? ((voxelY - start.y) / direction.y): ((voxelY + 1 - start.y) / direction.y);
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
                            voxelY += stepY;
                            tMaxY += tDeltaY;
                        }

                        steps++;
                        if (voxelX>=0 && voxelY>=0 && voxelZ>=0 && voxelX<SIZE && voxelY<SIZE && voxelZ<SIZE){
                            if (world.GetVoxel(voxelX,voxelY,voxelZ)!=0) break;
                        }
                        else {
                            steps = 1000;
                            break;
                        }
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
#pragma omp parallel for collapse(2)
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

                    int lowX = x / 4;
                    int lowY = y / 4;

                    Vector3 accelPos =
                        accelerationPosition[
                            lowX + lowY * lowWidth
                        ];

                    const float boundaryEps = 1e-4f;
                    Vector3 rayStart = {
                        accelPos.x + direction.x * boundaryEps,
                        accelPos.y + direction.y * boundaryEps,
                        accelPos.z + direction.z * boundaryEps
                    };

                    int voxelX = (int)floorf(rayStart.x);
                    int voxelY = (int)floorf(rayStart.y);
                    int voxelZ = (int)floorf(rayStart.z);


                    int stepX =(direction.x > 0.0f) -(direction.x < 0.0f);
                    int stepY = (direction.y > 0.0f) - (direction.y < 0.0f);
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

                        if (voxelX>=0 && voxelY>=0 && voxelZ>=0 && voxelX<SIZE && voxelY<SIZE && voxelZ<SIZE) {
                            if (world.GetVoxel(voxelX,voxelY,voxelZ)!=0) {
                                                        
                                Vector3 start = {
                                    (float)voxelX + 0.5f + sunDirection.x * 0.1f,
                                    (float)voxelY + 0.5f + sunDirection.y * 0.1f,
                                    (float)voxelZ + 0.5f + sunDirection.z * 0.1f
                                };
                                float strength = 1.0;
                                for (int k = 0; k < 128; k++) {
                                    start.x += sunDirection.x;
                                    start.y += sunDirection.y;
                                    start.z += sunDirection.z;
                                                                
                                    if (start.x >= 0 &&  start.y >= 0 && start.z  >= 0 && 
                                        start.x < SIZE &&  start.y < SIZE && start.z  < SIZE) {
                                        if (world.GetVoxel((int)start.x,(int)start.y,(int)start.z) != 0) {
                                            strength = 0.8f; // In shadow
                                            break;
                                        }
                                    } else {
                                        break; // Left the world, no shadow
                                    }
                                }
                                uint8_t type = world.GetVoxel(voxelX,voxelY,voxelZ);
                                ((unsigned char *)imageBuffer.data)[idx] = colors[type].r*strength;
                                ((unsigned char *)imageBuffer.data)[idx + 1] = colors[type].g*strength;
                                ((unsigned char *)imageBuffer.data)[idx + 2] =  colors[type].b*strength;
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

                                voxelY += stepY;
                                tMaxY += tDeltaY;
                            }

                        }
                        else {
                            break;
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
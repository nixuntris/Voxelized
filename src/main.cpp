#include "raylib.h"
#include <cinttypes>
#include <cstdint>
#include <bit>
#include "rlgl.h"
#include <unordered_map>
#include "raymath.h"
#include <iostream>
#include <cmath>
#include <chrono>
#include <immintrin.h>
using Clock = std::chrono::steady_clock;

auto ms = [](auto start, auto end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
};
const int width = 1400;
const int height = 1400;

inline static void GetScreenToWorldRay8(
    float x0, float py, int width, int height, const Matrix &viewInv,
    float *out_x, float *out_y, float *out_z)
{
    const __m256 lane = _mm256_setr_ps(0,1,2,3,4,5,6,7);
    const __m256 xs   = _mm256_add_ps(_mm256_set1_ps(x0), lane);

    const __m256 two      = _mm256_set1_ps(2.0f);
    const __m256 one      = _mm256_set1_ps(1.0f);
    const __m256 invWidth = _mm256_set1_ps(1.0f / (float)width);
    const __m256 invP     = _mm256_set1_ps(1.0f / 1.73205f);

    __m256 x = _mm256_sub_ps(_mm256_mul_ps(_mm256_mul_ps(two, xs), invWidth), one);
    float yndc = 1.0f - (2.0f * py) / (float)height;
    __m256 y = _mm256_set1_ps(yndc);

    __m256 viewX = _mm256_mul_ps(x, invP);
    __m256 viewY = _mm256_mul_ps(y, invP);

    const __m256 m0 = _mm256_set1_ps(viewInv.m0), m4 = _mm256_set1_ps(viewInv.m4), m8  = _mm256_set1_ps(viewInv.m8);
    const __m256 m1 = _mm256_set1_ps(viewInv.m1), m5 = _mm256_set1_ps(viewInv.m5), m9  = _mm256_set1_ps(viewInv.m9);
    const __m256 m2 = _mm256_set1_ps(viewInv.m2), m6 = _mm256_set1_ps(viewInv.m6), m10 = _mm256_set1_ps(viewInv.m10);

    __m256 wx = _mm256_fmadd_ps(m4, viewY, _mm256_fmadd_ps(m0, viewX, m8));
    __m256 wy = _mm256_fmadd_ps(m5, viewY, _mm256_fmadd_ps(m1, viewX, m9));
    __m256 wz = _mm256_fmadd_ps(m6, viewY, _mm256_fmadd_ps(m2, viewX, m10));

    __m256 lenSq = _mm256_fmadd_ps(wz, wz, _mm256_fmadd_ps(wy, wy, _mm256_mul_ps(wx, wx)));

    __m256 r0 = _mm256_rsqrt_ps(lenSq);
    __m256 half = _mm256_set1_ps(0.5f), three = _mm256_set1_ps(3.0f);
    __m256 invLen = _mm256_mul_ps(half, _mm256_mul_ps(r0,
                        _mm256_sub_ps(three, _mm256_mul_ps(lenSq, _mm256_mul_ps(r0, r0)))));

    __m256 nonZero = _mm256_cmp_ps(lenSq, _mm256_setzero_ps(), _CMP_GT_OQ);
    invLen = _mm256_and_ps(invLen, nonZero);

    _mm256_storeu_ps(out_x, _mm256_mul_ps(wx, invLen));
    _mm256_storeu_ps(out_y, _mm256_mul_ps(wy, invLen));
    _mm256_storeu_ps(out_z, _mm256_mul_ps(wz, invLen));
}

inline static Vector2 GetWorldToScreenOptimized(Vector3 position, Camera camera, int width, int height, Matrix matProj, Matrix matView)
{
    Quaternion worldPos = { position.x, position.y, position.z, 1.0f };
    worldPos = QuaternionTransform(worldPos, matView);
    worldPos = QuaternionTransform(worldPos, matProj);
    Vector3 ndcPos = { worldPos.x/worldPos.w, -worldPos.y/worldPos.w, worldPos.z/worldPos.w };
    Vector2 screenPosition = { (ndcPos.x + 1.0f)/2.0f*(float)width, (ndcPos.y + 1.0f)/2.0f*(float)height };

    return screenPosition;
}
const int SIZE = 1024;
struct Chunk {
    uint8_t *voxels;
    uint8_t *voxelLightValue;
    bool containsBlocks;
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
                    else {
                        chunks[x][y][z].voxelLightValue = (uint8_t*)MemAlloc(32*32*32); 
                        for (int i = 0; i < 32*32*32; i++) {
                            chunks[x][y][z].voxelLightValue[i] = 0;
                        }
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
    int *oldStep;
    Vector3 *oldPos;
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
        oldPos = (Vector3*)MemAlloc(width*height*sizeof(Vector3));
        oldStep = (int*)MemAlloc(width*height*sizeof(int));
        for (int i = 0; i < width*height; i++) {
            oldStep[i] = -1;
            oldPos[i] = camera.position;

        }
        DisableCursor();
    }
    void Run() {
    int frame = 0;

    const int lowWidth  = width / 4;
    const int lowHeight = height / 4;
    const Color colors[10] = {SKYBLUE,GREEN,{uint8_t(GREEN.r*0.9),uint8_t(GREEN.g*0.9),uint8_t(GREEN.b*0.9),255},GRAY};
    
    auto frameStart = Clock::now();
    auto totalStart = Clock::now();
    
    while (!WindowShouldClose()) {
        auto loopStart = Clock::now();
        BeginDrawing();
        ClearBackground(SKYBLUE);

        Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
        Matrix viewInv = MatrixInvert(matView);

        frame++;
        
        if (frame % 3 == 0) {
            #pragma omp parallel for
            for (int i = 0; i < width * height; i++) {
                oldStep[i] = -1;
                oldPos[i] = {-1, -1, -1};
            }
        }

        auto dirStart = Clock::now();
        #pragma omp parallel for
        for (int y = 0; y < height; y++) {
            alignas(32) float xs[8], ys[8], zs[8];
            for (int x = 0; x < width; x += 8) {
                GetScreenToWorldRay8((float)x, (float)y, width, height, viewInv, xs, ys, zs);
                for (int i = 0; i < 8; i++) {
                    int px = x + i;
                    if ((px + y + frame) % 2 == 0) continue;
                    directionStorage[px + y * width] = { xs[i], ys[i], zs[i] };
                }
            }
        }
        auto dirEnd = Clock::now();

        auto accelStart = Clock::now();
        #pragma omp parallel for collapse(2)
        for (int x = 0; x < lowWidth; x++) {
            for (int y = 0; y < lowHeight; y++) {
                if ((x + y + frame) % 2 == 0) continue; 
                int sampleX = x * 4 + 2;
                int sampleY = y * 4 + 2;

                Vector3 direction = directionStorage[sampleX + sampleY * width];
                Vector3 start = camera.position;

                accelerationPosition[x + y * lowWidth] = start;
                stepStorage[x + y * lowWidth] = 0;

                int voxelX = (int)floorf(start.x);
                int voxelY = (int)floorf(start.y);
                int voxelZ = (int)floorf(start.z);
                int stepX = (direction.x > 0.0f) - (direction.x < 0.0f);
                int stepZ = (direction.z > 0.0f) - (direction.z < 0.0f);
                int stepY = (direction.y > 0.0f) - (direction.y < 0.0f);
                float tDeltaX = (direction.x != 0.0f) ? fabsf(1.0f / direction.x) : INFINITY;
                float tDeltaY = fabsf(1.0f / direction.y);
                float tDeltaZ = (direction.z != 0.0f) ? fabsf(1.0f / direction.z) : INFINITY;
                float tMaxX = (direction.x > 0.0f) ? (((voxelX + 1) - start.x) / direction.x) : (direction.x < 0.0f) ? ((voxelX - start.x) / direction.x) : INFINITY;
                float tMaxY = (direction.y < 0.0f) ? ((voxelY - start.y) / direction.y) : ((voxelY + 1 - start.y) / direction.y);
                float tMaxZ = (direction.z > 0.0f) ? (((voxelZ + 1) - start.z) / direction.z) : (direction.z < 0.0f) ? ((voxelZ - start.z) / direction.z) : INFINITY;
                float previousT = 0.0f;
                int steps = 0;
                const int safetyBacktrack = 4;
                
                while (steps < 1000) {
                    previousT = fminf(tMaxX, fminf(tMaxY, tMaxZ));

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
                    if (voxelX >= 0 && voxelY >= 0 && voxelZ >= 0 && 
                        voxelX < SIZE && voxelY < SIZE && voxelZ < SIZE) {
                        if (world.GetVoxel(voxelX, voxelY, voxelZ) != 0) break;
                    }
                    else {
                        steps = 1000;
                        break;
                    }
                }
                
                float safeT = fmaxf(0.0f, previousT - (float)safetyBacktrack);
                Vector3 accelerationPoint = {
                    start.x + direction.x * safeT,
                    start.y + direction.y * safeT,
                    start.z + direction.z * safeT
                };

                accelerationPosition[x + y * lowWidth] = accelerationPoint;
                stepStorage[x + y * lowWidth] = steps;
            }
        }
        auto accelEnd = Clock::now();

        auto renderStart = Clock::now();
        #pragma omp parallel for collapse(2)
        for (int x = 0; x < width; x++) {
            for (int y = 0; y < height; y++) {
                int idx = (y * imageBuffer.width + x) * 4;
                int pixelIndex = x + y * width;

                if ((x + y + frame) % 3 == 0) continue;
                
                ((unsigned char *)imageBuffer.data)[idx] = SKYBLUE.r;
                ((unsigned char *)imageBuffer.data)[idx + 1] = SKYBLUE.g;
                ((unsigned char *)imageBuffer.data)[idx + 2] = SKYBLUE.b;
                ((unsigned char *)imageBuffer.data)[idx + 3] = 255;

                Vector3 direction = directionStorage[x + y * width];
                int lowX = x / 4;
                int lowY = y / 4;

                Vector3 rayStart = accelerationPosition[lowX + lowY * lowWidth];
                
                int startSteps = stepStorage[lowX + lowY * lowWidth];
                
                bool hasOldData = (oldPos[pixelIndex].x != -1) && (oldStep[pixelIndex] > 0);
                
                if (hasOldData && frame % 2 == 1) { 
                    Vector3 oldWorldPos = oldPos[pixelIndex];
                    
                    Vector3 offset = {
                        oldWorldPos.x - camera.position.x,
                        oldWorldPos.y - camera.position.y,
                        oldWorldPos.z - camera.position.z
                    };
                    
                    float t = Vector3DotProduct(offset, direction);
                    
                    if (t > 0) {
                        Vector3 pushedPos = {
                            camera.position.x + direction.x * t,
                            camera.position.y + direction.y * t,
                            camera.position.z + direction.z * t
                        };
                        
                        int px = (int)floorf(pushedPos.x);
                        int py = (int)floorf(pushedPos.y);
                        int pz = (int)floorf(pushedPos.z);
                        
                        if (px >= 0 && py >= 0 && pz >= 0 && 
                            px < SIZE && py < SIZE && pz < SIZE) {
                            if (world.GetVoxel(px, py, pz) == 0) {
                                rayStart = pushedPos;
                                if (oldStep[pixelIndex]-5>0) startSteps = oldStep[pixelIndex]-5;
                            }
                            else {
                                rayStart = accelerationPosition[lowX + lowY * lowWidth];
                                startSteps = 0;
                            }
                        }
                    }
                }

                const float boundaryEps = 1e-4f;
                Vector3 actualStart = {
                    rayStart.x + direction.x * boundaryEps,
                    rayStart.y + direction.y * boundaryEps,
                    rayStart.z + direction.z * boundaryEps
                };

                int voxelX = (int)floorf(actualStart.x);
                int voxelY = (int)floorf(actualStart.y);
                int voxelZ = (int)floorf(actualStart.z);

                int stepX = (direction.x > 0.0f) - (direction.x < 0.0f);
                int stepY = (direction.y > 0.0f) - (direction.y < 0.0f);
                int stepZ = (direction.z > 0.0f) - (direction.z < 0.0f);

                float tDeltaX = (direction.x != 0.0f) ? fabsf(1.0f / direction.x) : INFINITY;
                float tDeltaY = fabsf(1.0f / direction.y);
                float tDeltaZ = (direction.z != 0.0f) ? fabsf(1.0f / direction.z) : INFINITY;

                float tMaxX = (direction.x > 0.0f) ? (((voxelX + 1) - actualStart.x) / direction.x) : 
                              (direction.x < 0.0f) ? ((voxelX - actualStart.x) / direction.x) : INFINITY;
                float tMaxY = (direction.y < 0.0f) ? ((voxelY - actualStart.y) / direction.y) : 
                              ((voxelY + 1 - actualStart.y) / direction.y);
                float tMaxZ = (direction.z > 0.0f) ? (((voxelZ + 1) - actualStart.z) / direction.z) : 
                              (direction.z < 0.0f) ? ((voxelZ - actualStart.z) / direction.z) : INFINITY;

                int steps = startSteps;
                bool hitFound = false;
                
                while (steps < 1000) {
                    if (voxelX >= 0 && voxelY >= 0 && voxelZ >= 0 && 
                        voxelX < SIZE && voxelY < SIZE && voxelZ < SIZE) {
                        
                        if (world.GetVoxel(voxelX, voxelY, voxelZ) != 0) {
                            oldPos[pixelIndex] = {
                                (float)voxelX + 0.5f - direction.x * 0.3f,
                                (float)voxelY + 0.5f - direction.y * 0.3f,
                                (float)voxelZ + 0.5f - direction.z * 0.3f
                            };
                            oldStep[pixelIndex] = steps;
                            hitFound = true;
                            
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
                                
                                if (start.x >= 0 && start.y >= 0 && start.z >= 0 &&
                                    start.x < SIZE && start.y < SIZE && start.z < SIZE) {
                                    if (world.GetVoxel((int)start.x, (int)start.y, (int)start.z) != 0) {
                                        strength = 0.8f;
                                        break;
                                    }
                                } else {
                                    break;
                                }
                            }
                            
                            uint8_t type = world.GetVoxel(voxelX, voxelY, voxelZ);
                            ((unsigned char *)imageBuffer.data)[idx] = colors[type].r * strength;
                            ((unsigned char *)imageBuffer.data)[idx + 1] = colors[type].g * strength;
                            ((unsigned char *)imageBuffer.data)[idx + 2] = colors[type].b * strength;
                            ((unsigned char *)imageBuffer.data)[idx + 3] = 255;
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
                
                if (!hitFound) {
                    oldPos[pixelIndex] = {-1, -1, -1};
                    oldStep[pixelIndex] = -1;
                }
            }
        }
        auto renderEnd = Clock::now();
        
        UpdateTexture(displayBuffer, imageBuffer.data);
        DrawTexture(displayBuffer, 0, 0, WHITE);
        UpdateCamera(&camera, CAMERA_FREE);
        
        auto loopEnd = Clock::now();
        
        double dirTime = ms(dirStart, dirEnd);
        double accelTime = ms(accelStart, accelEnd);
        double renderTime = ms(renderStart, renderEnd);
        double loopTime = ms(loopStart, loopEnd);
        double totalTime = ms(totalStart, loopEnd);
        
        std::cout << "Frame " << frame << " | Direction: " << dirTime << "ms | Acceleration: " << accelTime << "ms | Render: " << renderTime << "ms | Total Loop: " << loopTime << "ms | Total Runtime: " << totalTime << "ms" << std::endl;
        
        DrawFPS(0, 0);

        EndDrawing();
    }
}


};

int main() {
    App *app = new App;
    app->Run();
}
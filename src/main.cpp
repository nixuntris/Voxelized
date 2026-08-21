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
#include <limits>
#include "world.hpp"
#include "math.hpp"
enum VoxelTypes {
    AIR=0,
    GRASS=1,
    GRASS_VARIANT=2,
    TREE_BARK=3,
    LEAF=4
};
using Clock = std::chrono::steady_clock;

auto ms = [](auto start, auto end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
};

Vector3 sunDirection = Vector3Normalize((Vector3){ 0.8f, 0.2f, 0.2f });

float sunDirSX = copysignf(1.0f, sunDirection.x);
float sunDirSY = copysignf(1.0f, sunDirection.y);
float sunDirSZ = copysignf(1.0f, sunDirection.z);
float invDx = 1.0f / sunDirection.x;
float invDy = 1.0f / sunDirection.y;
float invDz = 1.0f / sunDirection.z;
class App {
    public:
    Camera camera;
    Matrix matProj;
    Image imageBuffer;
    Texture displayBuffer;
    Vector3 *directionStorage;
    World world;
    int *stepStorage;
    int *oldStep;
    float *oldDistance;
    App() {
        InitWindow(width*SCALE,height*SCALE,"Voxelized");
        camera.position = (Vector3){ WORLD_WIDTH/2, 384, WORLD_DEPTH/2 };
        camera.target = (Vector3){ 0.0f, 2.0f, 0.0f };
        camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        camera.fovy = FOVY;
        camera.projection = CAMERA_PERSPECTIVE;
        matProj = MatrixIdentity();
        matProj = MatrixPerspective(camera.fovy*DEG2RAD, ((double)width/(double)height), 0.01f, 10000.0f);
        imageBuffer = GenImageColor(width,height,BLACK);
        ImageFormat(&imageBuffer,PIXELFORMAT_UNCOMPRESSED_R8G8B8);
        displayBuffer = LoadTextureFromImage(imageBuffer);
        directionStorage = (Vector3*)MemAlloc(width*height*sizeof(Vector3));
        stepStorage = (int*)MemAlloc(width*height*sizeof(int));
        oldDistance = (float*)MemAlloc(width*height*sizeof(float));
        oldStep = (int*)MemAlloc(width*height*sizeof(int));
        for (int i = 0; i < width*height; i++) {
            oldStep[i] = 0;
            oldDistance[i] = 0;
        }
        world.Init(camera.position);
        DisableCursor();
        std::cout<<LOD2_START<<" "<<LOD4_START<<" "<<LOD8_START<<" "<<LOD16_START<<"\n";
    }
    void Run() {
        int frame = 0;

        const Color colors[10] = {SKYBLUE,GREEN,{uint8_t(GREEN.r*0.9),uint8_t(GREEN.g*0.9),uint8_t(GREEN.b*0.9),255},BROWN,DARKGREEN};
        
        auto totalStart = Clock::now();
        bool lowResPass = false;
        while (!WindowShouldClose()) {
            auto loopStart = Clock::now();
            BeginDrawing();
            ClearBackground(SKYBLUE);

            Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
            Matrix viewInv = MatrixInvert(matView);

            frame++;
            
            auto dirStart = Clock::now();
            #pragma omp parallel for
            for (int y = 0; y < height; y++) {
                alignas(32) float xs[8], ys[8], zs[8];

                int x = 0;
                for (; x + 7 < width; x += 8) {
                    GetScreenToWorldRay8((float)x, (float)y, width, height, viewInv, xs, ys, zs);
                    for (int i = 0; i < 8; i++) {
                        int px = x + i;
                        directionStorage[px + y * width] = { xs[i], ys[i], zs[i] };
                    }
                }

                if (x < width) {
                    const int tailX = width - 8;
                    GetScreenToWorldRay8((float)tailX, (float)y, width, height, viewInv, xs, ys, zs);
                    for (int i = 0; i < 8; i++) {
                        int px = tailX + i;
                        directionStorage[px + y * width] = { xs[i], ys[i], zs[i] };
                    }
                }
            }
            auto dirEnd = Clock::now();
            std::fill(oldDistance, oldDistance + width * height, 0.0f);
            if (IsKeyPressed(KEY_F)) lowResPass = !lowResPass;
            std::cout<<lowResPass<<"\n";
            if (lowResPass) {
                
                constexpr int LOW_SCALE = 4;
                constexpr float CONE_GUARD = 1.5f;

                #pragma omp parallel for collapse(2)
                for (int by = 0; by < height / LOW_SCALE; ++by) {
                    for (int bx = 0; bx < width / LOW_SCALE; ++bx) {
                        const int baseX = bx * LOW_SCALE;
                        const int baseY = by * LOW_SCALE;

                        const Vector3 d00 = directionStorage[(baseX + 0) + (baseY + 0) * width];
                        const Vector3 d30 = directionStorage[(baseX + 3) + (baseY + 0) * width];
                        const Vector3 d03 = directionStorage[(baseX + 0) + (baseY + 3) * width];
                        const Vector3 d33 = directionStorage[(baseX + 3) + (baseY + 3) * width];

                        Vector3 direction = Vector3Normalize({
                            d00.x + d30.x + d03.x + d33.x,
                            d00.y + d30.y + d03.y + d33.y,
                            d00.z + d30.z + d03.z + d33.z
                        });

                        const float coneSlope = std::max({
                            DIRECTION_DELTA(d00), DIRECTION_DELTA(d30),
                            DIRECTION_DELTA(d03), DIRECTION_DELTA(d33)
                        });

                        float t = 0.0f;

                        while (t < RENDERDISTANCE) {
                            const float voxelX = camera.position.x + direction.x * t;
                            const float voxelY = camera.position.y + direction.y * t;
                            const float voxelZ = camera.position.z + direction.z * t;

                            if (voxelX < 0.0f || voxelY < 0.0f || voxelZ < 0.0f ||
                                voxelX >= WORLD_WIDTH || voxelY >= WORLD_HEIGHT || voxelZ >= WORLD_DEPTH) {
                                break;
                            }

                            const int ix = (int)voxelX;
                            const int iy = (int)voxelY;
                            const int iz = (int)voxelZ;
                            TraversalChunk &chunk = world.traversalChunks[ix >> 5][iy >> 5][iz >> 5];
                            const int lx = ix & 31;
                            const int ly = iy & 31;
                            const int lz = iz & 31;

                            const float jump = std::max({
                                STEP(chunk.distanceToClosestVoxel, 32.0f),
                                STEP(chunk.distance16[IDX(lx >> 4,ly >> 4,lz >> 4,2)], 16.0f),
                                STEP(chunk.distance8[IDX(lx >> 3,ly >> 3,lz >> 3,4)], 8.0f),
                                STEP(chunk.distance4[IDX(lx >> 2,ly >> 2,lz >> 2,8)], 4.0f)
                            });
                            const float coneRadius = t * coneSlope + CONE_GUARD;
                            const float remainingSafe = jump - coneRadius;
                            if (remainingSafe <= 0.0f) break;

                            const float advance = remainingSafe / (1.0f + coneSlope);
                            if (advance <= 0.0001f) break;

                            t += advance;
                        }

                        const float seedT = std::max(0.0f, t - 0.25f);
                        for (int dy = 0; dy < LOW_SCALE; ++dy) {
                            for (int dx = 0; dx < LOW_SCALE; ++dx) {
                                oldDistance[(baseX + dx) + (baseY + dy) * width] = seedT;
                            }
                        }
                    }
                }
            }
            
            auto renderStart = Clock::now();
            #pragma omp parallel for simd
            for (int x = 0; x < width; x++) {
                for (int y = 0; y < height; y++) {
                    int idx = (y * imageBuffer.width + x) * 3;
                    int pixelIndex = x + y * width;

                    if ((x + y + frame) % 2 == 0) continue;
                    
                    ((unsigned char *)imageBuffer.data)[idx] = SKYBLUE.r;
                    ((unsigned char *)imageBuffer.data)[idx + 1] = SKYBLUE.g;
                    ((unsigned char *)imageBuffer.data)[idx + 2] = SKYBLUE.b;

                    Vector3 direction = directionStorage[pixelIndex];
                    float t = oldDistance[pixelIndex];
                    float sx = copysignf(1.0f, direction.x);
                    float sy = copysignf(1.0f, direction.y);
                    float sz = copysignf(1.0f, direction.z);
                    Vector3 invDirLocal = {1/direction.x,1/direction.y,1/direction.z};
                    while (t < RENDERDISTANCE ) {
                        float voxelX = camera.position.x + direction.x * t;
                        float voxelY = camera.position.y + direction.y * t;
                        float voxelZ = camera.position.z + direction.z * t;

                        if (voxelX < 0.0f || voxelY < 0.0f || voxelZ < 0.0f ||
                            voxelX >= WORLD_WIDTH || voxelY >= WORLD_HEIGHT || voxelZ >= WORLD_DEPTH) {
                            break;
                        }
                        int lod = 1;
                        if (t > LOD16_START) lod = 16;
                        else if (t > LOD8_START) lod = 8;
                        else if (t > LOD4_START) lod = 4;
                        else if (t > LOD2_START) lod = 2;
                        else lod = 1;
                        int cx = voxelX / 32;
                        int cy = voxelY / 32;
                        int cz = voxelZ / 32;
                        if (world.voxelChunks[cx][cy][cz].containsBlocks) {
                            int lx = int(voxelX) % 32;
                            int ly = int(voxelY) % 32;
                            int lz = int(voxelZ) % 32;
                            int index = lx * 32 * 32 + ly * 32 + lz;
                            
                            if (world.traversalChunks[cx][cy][cz].occupancy[index >> 6] & (1ull << (index & 63))) {
                                uint8_t type;
                                if (world.voxelChunks[cx][cy][cz].palletized==0) {
                                    int lod = world.voxelChunks[cx][cy][cz].lod; 
                                    type = world.voxelChunks[cx][cy][cz].voxels[IDX(lx/lod,ly/lod,lz/lod,world.voxelChunks[cx][cy][cz].size)];
                                    
                                }
                                else type = world.voxelChunks[cx][cy][cz].palletized;
                                if (type != 0) {
                                    float strength = 1;
                                    float t = 0;
                                    int voX = voxelX;
                                    int voY = voxelY;
                                    int voZ = voxelZ;
                                    
                                    Vector3 sampleDir = sunDirection;
                                    voxelX+=sampleDir.x;
                                    voxelY+=sampleDir.y;
                                    voxelZ+=sampleDir.z;
                                    int size = world.voxelChunks[(int)voX/32][(int)voY/32][(int)voZ/32].size;
                                    int lod = world.voxelChunks[(int)voX/32][(int)voY/32][(int)voZ/32].lod;
                                    uint8_t lightVal = world.voxelChunks[(int)voX/32][(int)voY/32][(int)voZ/32].voxelLightValue[IDX((voX%32)/lod,(voY%32)/lod,(voZ%32)/lod,size)];
                                    if (lightVal!=0) {
                                        if (lightVal==2) {
                                            strength = 0.8f;
                                        }
                                    }
                                    else {
                                        while (t < 256.0f) {

                                            if (voxelX < 0.0f || voxelY < 0.0f || voxelZ < 0.0f ||
                                                voxelX >= WORLD_WIDTH ||
                                                voxelY >= WORLD_HEIGHT ||
                                                voxelZ >= WORLD_DEPTH)
                                            {
                                                world.voxelChunks[voX >> 5][voY >> 5][voZ >> 5]
                                                    .voxelLightValue[
                                                        IDX((voX%32)/lod,(voY%32)/lod,(voZ%32)/lod,size)
                                                    ] = 1;
                                                break;
                                            }

                                            int ix = (int)voxelX;
                                            int iy = (int)voxelY;
                                            int iz = (int)voxelZ;

                                            int cx = ix >> 5;
                                            int cy = iy >> 5;
                                            int cz = iz >> 5;

                                            int lx = ix & 31;
                                            int ly = iy & 31;
                                            int lz = iz & 31;

                                            TraversalChunk& chunk =
                                                world.traversalChunks[cx][cy][cz];

                                            if (world.voxelChunks[cx][cy][cz].containsBlocks) {
                                                int index = lx * 32 * 32 + ly * 32 + lz;

                                                if (world.traversalChunks[cx][cy][cz].occupancy[index >> 6] & (1ull << (index & 63))) {
                                                
                                                    strength = 0.8f;

                                                    world.voxelChunks[voX >> 5][voY >> 5][voZ >> 5]
                                                        .voxelLightValue[
                                                            IDX((voX%32)/lod,(voY%32)/lod,(voZ%32)/lod,size)
                                                        ] = 2;

                                                    break;
                                                }
                                            }

                                            float jump = std::max({
                                                STEP(chunk.distanceToClosestVoxel,  std::max(32, lod)),
                                                STEP(chunk.distance16[IDX(lx >> 4,ly >> 4,lz >> 4,2)],  std::max(16, lod)),
                                                STEP(chunk.distance8 [IDX(lx >> 3,ly >> 3,lz >> 3,4)],   std::max(8, lod)),
                                                STEP(chunk.distance4 [IDX(lx >> 2,ly >> 2,lz >> 2,8)],   std::max(4, lod))
                                            });

                                            if (jump > 0.0f) {
                                                t += jump;

                                                voxelX += sampleDir.x * jump;
                                                voxelY += sampleDir.y * jump;
                                                voxelZ += sampleDir.z * jump;
                                            }
                                            else {
                                                int cellSize = 1;

                                                if (chunk.distanceToClosestVoxel != 0)
                                                    cellSize = 32;
                                                else if (chunk.distance16[IDX(lx >> 4,ly >> 4,lz >> 4,2)] != 0 && lod<=16)
                                                    cellSize = 16;
                                                else if (chunk.distance8[IDX(lx >> 3,ly >> 3,lz >> 3,4)] != 0 && lod<=8)
                                                    cellSize = 8;
                                                else if (chunk.distance4[IDX(lx >> 2,ly >> 2,lz >> 2,8)] != 0 && lod<=4)
                                                    cellSize = 4;

                                                int bx = ix & ~(cellSize - 1);
                                                int by = iy & ~(cellSize - 1);
                                                int bz = iz & ~(cellSize - 1);

                                                float tx = ((sunDirSX > 0.0f ? bx + cellSize : bx) - voxelX)
                                                        * invDx;

                                                float ty = ((sunDirSY > 0.0f ? by + cellSize : by) - voxelY)
                                                        * invDy;

                                                float tz = ((sunDirSZ > 0.0f ? bz + cellSize : bz) - voxelZ)
                                                        * invDz;

                                                float change = std::min({tx, ty, tz}) + 0.0001f;

                                                t += change;

                                                voxelX += sampleDir.x * change;
                                                voxelY += sampleDir.y * change;
                                                voxelZ += sampleDir.z * change;
                                            }
                                        }
                                    }
                                    ((unsigned char *)imageBuffer.data)[idx] = colors[type].r*strength;
                                    ((unsigned char *)imageBuffer.data)[idx + 1] = colors[type].g*strength;
                                    ((unsigned char *)imageBuffer.data)[idx + 2] = colors[type].b*strength;
                                    break;
                                }
                            }
                            
                        }

                        int ix = (int)voxelX, iy = (int)voxelY, iz = (int)voxelZ;
                        TraversalChunk &chunk = world.traversalChunks[ix >> 5][iy >> 5][iz >> 5];
                        int lx = ix & 31, ly = iy & 31, lz = iz & 31;

                        float jump = std::max({
                            STEP(chunk.distanceToClosestVoxel,        std::max(32, lod)),
                            STEP(chunk.distance16[IDX(lx >> 4,ly >> 4,lz >> 4,2)], std::max(16, lod)),
                            STEP(chunk.distance8 [IDX(lx >> 3,ly >> 3,lz >> 3,4)], std::max(8,  lod)),
                            STEP(chunk.distance4 [IDX(lx >> 2,ly >> 2,lz >> 2,8)], std::max(4,  lod))
                        });

                        if (jump > 0.0f) {
                            t+=jump;
                        }
                        else {
                            int cellSize = 1;
                            
                            if (chunk.distanceToClosestVoxel != 0) {
                                cellSize = 32;
                            }
                            else if (chunk.distance16[IDX(lx >> 4,ly >> 4,lz >> 4,2)] != 0 && lod<=16) {
                                cellSize = 16;
                            }
                            else if (chunk.distance8[IDX(lx >> 3,ly >> 3,lz >> 3,4)] != 0 && lod<=8) {
                                cellSize = 8;
                            }
                            else if (chunk.distance4[IDX(lx >> 2,ly >> 2,lz >> 2,8)] != 0 && lod<=4) {
                                cellSize = 4;
                            }

                            int bx = ix & ~(cellSize - 1);
                            int by = iy & ~(cellSize - 1);
                            int bz = iz & ~(cellSize - 1);

                            float tx = ((sx > 0.0f ? bx + cellSize : bx) - voxelX) * invDirLocal.x;
                            float ty = ((sy > 0.0f ? by + cellSize : by) - voxelY) * invDirLocal.y;
                            float tz = ((sz > 0.0f ? bz + cellSize : bz) - voxelZ) * invDirLocal.z;

                            t += std::min({tx, ty, tz}) + 0.0001f;

                        }                    
                    }
                    oldDistance[pixelIndex] = t*0.9;
                }
            }
            auto renderEnd = Clock::now();
            
            UpdateTexture(displayBuffer, imageBuffer.data);
                    
            DrawTexturePro(displayBuffer, 
                (Rectangle){0, 0, width, height},
                (Rectangle){0, 0, width*SCALE, height*SCALE},
                (Vector2){0, 0}, 0, WHITE);
            UpdateCamera(&camera, CAMERA_FREE);
            
            auto loopEnd = Clock::now();
            
            double dirTime = ms(dirStart, dirEnd);
            double renderTime = ms(renderStart, renderEnd);
            double loopTime = ms(loopStart, loopEnd);
            double totalTime = ms(totalStart, loopEnd);
            
            std::cout << "Frame " << frame << " | Direction: " << dirTime  << "ms | Render: " << renderTime << "ms | Total Loop: " << loopTime << "ms | Total Runtime: " << totalTime << "ms" << std::endl;
            
            DrawFPS(0, 0);

            EndDrawing();
    }
}


};

int main() {
    App *app = new App;
    app->Run();
}
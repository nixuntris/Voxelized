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
#include <thread>
#include <atomic>
using Clock = std::chrono::steady_clock;
const Color colors[10] = {SKYBLUE,GREEN,{uint8_t(GREEN.r*0.9),uint8_t(GREEN.g*0.9),uint8_t(GREEN.b*0.9),255},BROWN,DARKGREEN,GRAY,YELLOW,BLUE};
       
constexpr int BUFFER_WIDTH = 1920;
constexpr int BUFFER_HEIGHT = 1080;
constexpr int BUFFER_SIZE = BUFFER_WIDTH * BUFFER_HEIGHT;
int baseFPS = 100;
Vector3 sunDirection = Vector3Normalize((Vector3){ 0.8f, 0.2f, 0.2f });
Color SKYCOLOR = SKYBLUE;
float sunDirSX = copysignf(1.0f, sunDirection.x);
float sunDirSY = copysignf(1.0f, sunDirection.y);
float sunDirSZ = copysignf(1.0f, sunDirection.z);
float invDx = 1.0f / sunDirection.x;
float invDy = 1.0f / sunDirection.y;
float invDz = 1.0f / sunDirection.z;
int sunPosX = sunDirection.x > 0.0f;
int sunPosY = sunDirection.y > 0.0f;
int sunPosZ = sunDirection.z > 0.0f;
bool has_avx2()
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2");
#else
    return false;
#endif
}
struct Hit {
    int x,y,z;
    uint8_t type;
    bool viable;
};

class App {
    public:
    int prevFPS = baseFPS;
    Camera camera;
    Matrix matProj;
    Image imageBuffer;
    Texture displayBuffer;
    Vector3 *directionStorage;
    World *world;
    Hit hits[BUFFER_SIZE];
    int *stepStorage;
    int *oldStep;
    float *oldDistance;
    std::atomic<int> worldFinished{0};
    std::thread worker;
    bool cameraMoved = true;
    int frame = 0;
    Vector3*ids;
    App() {
        InitWindow(width*SCALE,height*SCALE,"Voxelized");
        std::cout<<LOD4_START<<" "<<LOD8_START<<" "<<LOD16_START<<" "<<LOD32_START<<"\n";
        camera.target = (Vector3){ 0.0f, 2.0f, 0.0f };
        camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        camera.fovy = FOVY;
        camera.projection = CAMERA_PERSPECTIVE;
        matProj = MatrixIdentity();
        matProj = MatrixPerspective(camera.fovy*DEG2RAD, ((double)width/(double)height), 0.01f, 10000.0f);
        imageBuffer = GenImageColor(width,height,BLACK);
        ImageFormat(&imageBuffer,PIXELFORMAT_UNCOMPRESSED_R8G8B8);
        displayBuffer = LoadTextureFromImage(imageBuffer);
        directionStorage = (Vector3*)MemAlloc(BUFFER_SIZE*sizeof(Vector3));
        stepStorage = (int*)MemAlloc(BUFFER_SIZE*sizeof(int));
        oldDistance = (float*)MemAlloc(BUFFER_SIZE*sizeof(float));
        oldStep = (int*)MemAlloc(BUFFER_SIZE*sizeof(int));
        ids =  (Vector3*)MemAlloc(BUFFER_SIZE*sizeof(Vector3));
        for (int i = 0; i < BUFFER_SIZE; i++) {
            oldStep[i] = 0;
            oldDistance[i] = 0;
        }
        
    }
    void Render() {
        auto totalStart = Clock::now();
        Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
        Matrix viewInv = MatrixInvert(matView);
        auto dirStart = Clock::now();
        if (cameraMoved) {
            #pragma omp parallel for
            for (int y = 0; y < height; y++) {
                alignas(32) float xs[8], ys[8], zs[8];

            int x = 0;
                for (; x + 7 < width; x += 8) {
                    GetScreenToWorldRay8((float)x, (float)y, width, height, viewInv, xs, ys, zs);
                    for (int i = 0; i < 8; i++) {
                        int px = x + i;
                        directionStorage[px * BUFFER_HEIGHT + y] = { xs[i], ys[i], zs[i] };
                    }
                }

            if (x < width) {
                    const int tailX = width - 8;
                    GetScreenToWorldRay8((float)tailX, (float)y, width, height, viewInv, xs, ys, zs);
                    for (int i = 0; i < 8; i++) {
                        int px = tailX + i;
                        directionStorage[px * BUFFER_HEIGHT + y] = { xs[i], ys[i], zs[i] };
                    }
                }
            }
        }
        
        auto dirEnd = Clock::now();
        constexpr int LOW_SCALE = 4;
        constexpr float CONE_GUARD = 1.5f;

    auto lowrenderStart = Clock::now();
        if (frame%2==0) {
            std::fill(oldDistance, oldDistance + BUFFER_SIZE, 0.0f);
            #pragma omp parallel for collapse(2)
            for (int by = 0; by < height / LOW_SCALE; ++by) {
                for (int bx = 0; bx < width / LOW_SCALE; ++bx) {
                    
                    const int baseX = bx * LOW_SCALE;
                    const int baseY = by * LOW_SCALE;

                const Vector3 d00 = directionStorage[(baseX + 0) * BUFFER_HEIGHT + (baseY + 0)];
                    const Vector3 d30 = directionStorage[(baseX + 3) * BUFFER_HEIGHT + (baseY + 0)];
                    const Vector3 d03 = directionStorage[(baseX + 0) * BUFFER_HEIGHT + (baseY + 3)];
                    const Vector3 d33 = directionStorage[(baseX + 3) * BUFFER_HEIGHT + (baseY + 3)];
                    Vector3 direction = {
                        d00.x + d30.x + d03.x + d33.x,
                        d00.y + d30.y + d03.y + d33.y,
                        d00.z + d30.z + d03.z + d33.z
                    };

                float length = sqrtf(direction.x*direction.x + direction.y*direction.y + direction.z*direction.z);
                    if (length != 0.0f)
                    {
                        float ilength = 1.0f/length;

                    direction.x *= ilength;
                        direction.y *= ilength;
                        direction.z *= ilength;
                    }

                const float coneSlope = sqrtf(std::max({
                        DIRECTION_DELTA(d00), DIRECTION_DELTA(d30),
                        DIRECTION_DELTA(d03), DIRECTION_DELTA(d33)
                    }));

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
                        TraversalChunk &chunk = world->traversalChunks[ix >> 5][iy >> 5][iz >> 5];
                        const int lx = ix & 31;
                        const int ly = iy & 31;
                        const int lz = iz & 31;

                    const float jump = std::max({
                            STEP(chunk.distanceToClosestVoxel, 32.0f),
                            STEP(chunk.distance16[IDX(lx >> 4,ly >> 4,lz >> 4,2)], 16.0f),
                            STEP(chunk.GetDistance8(IDX(lx >> 3,ly >> 3,lz >> 3,4)), 8.0f),
                            STEP(chunk.GetDistance4(IDX(lx >> 2, ly >> 2, lz >> 2, 8)), 4.0f)
                        });
                        const float coneRadius = t * coneSlope + CONE_GUARD;
                        const float remainingSafe = jump - coneRadius;
                        if (remainingSafe <= 0.0f) break;

                    const float advance = remainingSafe / (1.0f + coneSlope);
                        if (advance <= 1.0f) break;
                        t += advance;
                        if (t>LOD2_START) t += advance*0.5;
                    }

                const float seedT = std::max(0.0f, t - 0.25f);
                    for (int dy = 0; dy < LOW_SCALE; ++dy) {
                        for (int dx = 0; dx < LOW_SCALE; ++dx) {
                            oldDistance[(baseX + dx) * BUFFER_HEIGHT + (baseY + dy)] = seedT;
                        }
                    }
                }
            }
            
        }
        auto lowrenderEnd = Clock::now();
        
        auto renderStart = Clock::now();
        #pragma omp parallel for collapse(2)
        for (int x = 0; x < width; ++x) {
            for (int y = 0; y < height; ++y) {
                int idx = (y * imageBuffer.width + x) * 3;
                int pixelIndex = x * BUFFER_HEIGHT + y;

            hits[pixelIndex].viable = false;
                if ((x + y + frame) % 2 == 0) continue;
                ((unsigned char *)imageBuffer.data)[idx] = SKYCOLOR.r;
                ((unsigned char *)imageBuffer.data)[idx + 1] = SKYCOLOR.g;
                ((unsigned char *)imageBuffer.data)[idx + 2] = SKYCOLOR.b;

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
                    if (world->voxelChunks[cx][cy][cz].containsBlocks) {
                        int lx = int(voxelX) % 32;
                        int ly = int(voxelY) % 32;
                        int lz = int(voxelZ) % 32;
                        int lodr = world->voxelChunks[cx][cy][cz].lod; 
                        int lodIndex = IDX(lx/lodr,ly/lodr,lz/lodr,world->voxelChunks[cx][cy][cz].size);
                        if (world->traversalChunks[cx][cy][cz].occupancy[lodIndex >> 6] & (1ull << (lodIndex & 63))) {
                            uint8_t type;
                            if (world->voxelChunks[cx][cy][cz].palletized==0) {
                                type = world->voxelChunks[cx][cy][cz].voxels[lodIndex];
                                
                            }
                            else type = world->voxelChunks[cx][cy][cz].palletized;
                            hits[pixelIndex].viable = true;
                            hits[pixelIndex].type = type;
                            hits[pixelIndex].x = voxelX;
                            hits[pixelIndex].y = voxelY;
                            hits[pixelIndex].z = voxelZ;
                            
                            break;
                            
                        }
                        
                    }

                int ix = (int)voxelX, iy = (int)voxelY, iz = (int)voxelZ;
                    TraversalChunk &chunk = world->traversalChunks[ix >> 5][iy >> 5][iz >> 5];
                    int lx = ix & 31, ly = iy & 31, lz = iz & 31;
                    
                    float jump = std::max({
                        STEP(chunk.distanceToClosestVoxel,  std::max(32,lod)),
                        STEP(chunk.distance16[IDX(lx >> 4, ly >> 4, lz >> 4, 2)], std::max(16,lod)),
                        STEP(chunk.GetDistance8(IDX(lx >> 3, ly >> 3, lz >> 3, 4)),  std::max(8,lod)),
                        STEP(chunk.GetDistance4(IDX(lx >> 2, ly >> 2, lz >> 2, 8)),  std::max(4,lod))
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
                        else if (chunk.GetDistance8(IDX(lx >> 3,ly >> 3,lz >> 3,4)) != 0 && lod<=8) {
                            cellSize = 8;
                        }
                        else if (chunk.GetDistance4(IDX(lx >> 2, ly >> 2, lz >> 2, 8))  != 0 && lod<=4) {
                            cellSize = 4;
                        }

                    int bx = ix & ~(cellSize - 1);
                        int by = iy & ~(cellSize - 1);
                        int bz = iz & ~(cellSize - 1);

                    float ox = (sx + 1.0f) * 0.5f * cellSize;
                        float oy = (sy + 1.0f) * 0.5f * cellSize;
                        float oz = (sz + 1.0f) * 0.5f * cellSize;

                    float tx = (bx + ox - voxelX) * invDirLocal.x;
                        float ty = (by + oy - voxelY) * invDirLocal.y;
                        float tz = (bz + oz - voxelZ) * invDirLocal.z;
                        t += std::min({tx, ty, tz}) + 0.01f;


                }                    
                }
                oldDistance[pixelIndex] = t*0.9;
            }
        }
        auto renderEnd = Clock::now();
        auto lightStart = Clock::now();
        int r = 0;
        for (int x = 0; x < width; x++) {
            for (int y = 0; y < height; y++) {
                int pixelIndex = x * BUFFER_HEIGHT + y;
                if ((x+y+frame)%2==0) continue;
                if (!hits[pixelIndex].viable) continue;
                
                int origVoxelX = (int)hits[pixelIndex].x;
                int origVoxelY = (int)hits[pixelIndex].y;
                int origVoxelZ = (int)hits[pixelIndex].z;
                int dx = origVoxelX >> 5;
                int dy = origVoxelY >> 5;
                int dz = origVoxelZ >> 5;
                if (!world->voxelChunks[dx][dy][dz].containsLight) {
                    ids[r]= {float(dx),float(dy),float(dz)};
                    world->voxelChunks[dx][dy][dz].containsLight = true;
                    r+=1;
                }
                
            }
        }
        #pragma omp parallel for
        for (int t = 0; t < r; t++) {
            int dx = ids[t].x;
            int dy = ids[t].y;
            int dz = ids[t].z;
            
            int size = 32/world->traversalChunks[dx][dy][dz].buildID;
            size/=shadowQuality;
            world->voxelChunks[dx][dy][dz].voxelLightValueR = (uint8_t*)MemAlloc(size*size*size); 
            world->voxelChunks[dx][dy][dz].voxelLightValueG = (uint8_t*)MemAlloc(size*size*size); 
            world->voxelChunks[dx][dy][dz].voxelLightValueB = (uint8_t*)MemAlloc(size*size*size); 
            for (int i = 0; i < size*size*size; i++) {
                world->voxelChunks[dx][dy][dz].voxelLightValueR[i] = 0;
                world->voxelChunks[dx][dy][dz].voxelLightValueG[i] = 0;
                world->voxelChunks[dx][dy][dz].voxelLightValueB[i] = 0;
            }
            

    }
        #pragma omp parallel for collapse(2)
        for (int x = 0; x < width; x++) {
            for (int y = 0; y < height; y++) {
                int pixelIndex = x * BUFFER_HEIGHT + y;
                int idx = (y * imageBuffer.width + x) * 3;
                if (!hits[pixelIndex].viable) continue;
                if ((x + y + frame) % 2 == 0) continue;
                uint8_t type = hits[pixelIndex].type;
                float ambienceEffect = 0.36;
                float strengthR = 1.0f-ambienceEffect+(float(SKYCOLOR.r)/255.0f)*ambienceEffect;
                float strengthG = 1.0f-ambienceEffect+(float(SKYCOLOR.g)/255.0f)*ambienceEffect;
                float strengthB = 1.0f-ambienceEffect+(float(SKYCOLOR.b)/255.0f)*ambienceEffect;
                
                int origVoxelX = (int)hits[pixelIndex].x;
                int origVoxelY = (int)hits[pixelIndex].y;
                int origVoxelZ = (int)hits[pixelIndex].z;
                int dx = origVoxelX >> 5;
                int dy = origVoxelY >> 5;
                int dz = origVoxelZ >> 5;
                
                int origLod = world->voxelChunks[dx][dy][dz].lod;
                int origSize = world->voxelChunks[dx][dy][dz].size/shadowQuality;
                origLod*=shadowQuality;
                int id = IDX((origVoxelX % 32) / origLod, (origVoxelY % 32) / origLod, (origVoxelZ % 32) / origLod, origSize);
                
                uint8_t lightValR = world->voxelChunks[dx][dy][dz].voxelLightValueR[id];
                uint8_t lightValG = world->voxelChunks[dx][dy][dz].voxelLightValueG[id];
                uint8_t lightValB = world->voxelChunks[dx][dy][dz].voxelLightValueB[id];

            if (lightValR != 0 || lightValB != 0|| lightValG !=0 ) {
                    if (lightValR != 1) {
                        strengthR = float(lightValR - 1) / 253.0f;
                        strengthG = float(lightValG - 1) / 253.0f;
                        strengthB = float(lightValB - 1) / 253.0f;
                    }
                } else {
                    float shadowT = 0.0f;
                    float shadowX = hits[pixelIndex].x;
                    float shadowY = hits[pixelIndex].y;
                    float shadowZ = hits[pixelIndex].z;
                    shadowX += sunDirection.x * 1.5f;
                    shadowY += sunDirection.y * 1.5f;
                    shadowZ += sunDirection.z * 1.5f;
                    shadowT = 0.0f;
                    
                    while (shadowT < 256.0f) {
                        if (shadowX < 0.0f || shadowY < 0.0f || shadowZ < 0.0f ||
                            shadowX >= WORLD_WIDTH || shadowY >= WORLD_HEIGHT || shadowZ >= WORLD_DEPTH) {
                            strengthR = 1.0f;
                            strengthG = 1.0f;
                            strengthB = 1.0f;
                            int dx = origVoxelX>>5;
                            int dy = origVoxelY>>5;
                            int dz = origVoxelZ>>5;
                            int id = IDX((origVoxelX % 32) / origLod, (origVoxelY % 32) / origLod, (origVoxelZ % 32) / origLod, origSize);
                            world->voxelChunks[dx][dy][dz].voxelLightValueR[id] = 255;
                            world->voxelChunks[dx][dy][dz].voxelLightValueG[id] = 255;
                            world->voxelChunks[dx][dy][dz].voxelLightValueB[id] = 255;
                            
                            break;
                        }

                    int ix = (int)shadowX;
                        int iy = (int)shadowY;
                        int iz = (int)shadowZ;
                        int cx = ix >> 5;
                        int cy = iy >> 5;
                        int cz = iz >> 5;
                        int lx = ix & 31;
                        int ly = iy & 31;
                        int lz = iz & 31;
                        if (world->voxelChunks[cx][cy][cz].containsBlocks) {
                            int lodr = world->voxelChunks[cx][cy][cz].lod; 
                            int lodIndex = IDX(lx/lodr,ly/lodr,lz/lodr,world->voxelChunks[cx][cy][cz].size);
                            if (world->traversalChunks[cx][cy][cz].occupancy[lodIndex >> 6] & (1ull << (lodIndex & 63))) {
                                uint8_t typer;
                                if (world->voxelChunks[cx][cy][cz].palletized==0) {
                                    typer = world->voxelChunks[cx][cy][cz].voxels[lodIndex];
                                    
                                }
                                else typer = world->voxelChunks[cx][cy][cz].palletized;
                                if (voxelMetaData[typer].translucent) {
                                    strengthR *= voxelMetaData[typer].lightAbsorbR; 
                                    strengthG *= voxelMetaData[typer].lightAbsorbG; 
                                    strengthB *= voxelMetaData[typer].lightAbsorbB; 
                                }
                                else {
                                    strengthR *= voxelMetaData[typer].lightAbsorbR;
                                    strengthG *= voxelMetaData[typer].lightAbsorbG;
                                    strengthB *= voxelMetaData[typer].lightAbsorbB;
                                    uint8_t cachedValR = (uint8_t)((strengthR * 253.0f) + 1);
                                    uint8_t cachedValG = (uint8_t)((strengthG * 253.0f) + 1);
                                    uint8_t cachedValB = (uint8_t)((strengthB * 253.0f) + 1);
                                    int dx = origVoxelX>>5;
                                    int dy = origVoxelY>>5;
                                    int dz = origVoxelZ>>5;
                                    int id = IDX((origVoxelX % 32) / origLod, (origVoxelY % 32) / origLod, (origVoxelZ % 32) / origLod, origSize);
                                    world->voxelChunks[dx][dy][dz].voxelLightValueR[id] = cachedValR;
                                    world->voxelChunks[dx][dy][dz].voxelLightValueG[id] = cachedValG;
                                    world->voxelChunks[dx][dy][dz].voxelLightValueB[id] = cachedValB;

                                break;    
                                }
                                
                            }
                        }

                    int lod = 1;
                        if (shadowT > LOD16_START) lod = 16;
                        else if (shadowT > LOD8_START) lod = 8;
                        else if (shadowT > LOD4_START) lod = 4;
                        else if (shadowT > LOD2_START) lod = 2;
                        else shadowT = 1;
                        TraversalChunk& chunk = world->traversalChunks[cx][cy][cz];
                        float jump = std::max({
                            STEP(chunk.distanceToClosestVoxel,  std::max(32,lod)),
                            STEP(chunk.distance16[IDX(lx >> 4, ly >> 4, lz >> 4, 2)], std::max(16,lod)),
                            STEP(chunk.GetDistance8(IDX(lx >> 3, ly >> 3, lz >> 3, 4)),  std::max(8,lod)),
                            STEP(chunk.GetDistance4(IDX(lx >> 2, ly >> 2, lz >> 2, 8)),  std::max(4,lod))
                        });
                        
                        if (jump > 0.0f) {
                            shadowT += jump;
                            shadowX += sunDirection.x * jump;
                            shadowY += sunDirection.y * jump;
                            shadowZ += sunDirection.z * jump;
                        } else {
                            float sx = copysignf(1.0f, sunDirection.x);
                            float sy = copysignf(1.0f, sunDirection.y);
                            float sz = copysignf(1.0f, sunDirection.z);
                            
                            int bx = ix & ~(31);
                            int by = iy & ~(31);
                            int bz = iz & ~(31);
                            
                            float tx = (bx + (sx > 0 ? 32.0f : 0.0f) - shadowX) / sunDirection.x;
                            float ty = (by + (sy > 0 ? 32.0f : 0.0f) - shadowY) / sunDirection.y;
                            float tz = (bz + (sz > 0 ? 32.0f : 0.0f) - shadowZ) / sunDirection.z;
                            
                            float step = std::min({tx, ty, tz});
                            if (step < 0.0001f) step = 1.0f;
                            
                            shadowT += step;
                            shadowX += sunDirection.x * step;
                            shadowY += sunDirection.y * step;
                            shadowZ += sunDirection.z * step;

                    }
                    }
                }
                
                if (voxelMetaData[type].reflective) {
                    
                }

            ((unsigned char *)imageBuffer.data)[idx] = colors[type].r * strengthR;
                ((unsigned char *)imageBuffer.data)[idx + 1] = colors[type].g * strengthG;
                ((unsigned char *)imageBuffer.data)[idx + 2] = colors[type].b * strengthB;
            }
        }
        auto lightEnd = Clock::now();
        
        prevFPS = GetFPS();
        double dirTime = ms(dirStart, dirEnd);
        double renderTime = ms(renderStart, renderEnd);
        double lightTime = ms(lightStart, lightEnd);
        
        double lowrenderTime = ms(lowrenderStart, lowrenderEnd);
        std::cout
        << "Dir: "       << dirTime       << " ms | "
        << "Render: "    << renderTime    << " ms | "
        << "LowRender: " << lowrenderTime << " ms | "
        << "Light: "     << lightTime     << " ms \n";
    }
    void Run() {
        
        int gui = 0;
               
        while (!WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(WHITE);


            frame++;
            if (worldFinished==2) {
                if (IsKeyPressed(KEY_E)) {
                    
                    if (gui==0) {
                        gui = 2;
                        EnableCursor();
                        SetTargetFPS(60);
                    }
                    else {
                        gui = 0;
                        DisableCursor(); 
                        SetTargetFPS(-1);  
                    }
                }
                if (gui==0) {    
                                
                    Vector3 oldCameraPos = camera.position;
                    Vector3 oldCameraTarget = camera.target;
                    UpdateCamera(&camera, CAMERA_FREE);
                    cameraMoved = false;
                    if (
                    oldCameraTarget.x!=camera.target.x ||
                    oldCameraTarget.y!=camera.target.y || 
                    oldCameraTarget.z!=camera.target.z) {
                        cameraMoved = true;
                    }
                    Render();
                }
                        
                UpdateTexture(displayBuffer, imageBuffer.data);
                        
                DrawTexturePro(displayBuffer, 
                    (Rectangle){0, 0, width, height},
                    (Rectangle){0, 0, width*SCALE, height*SCALE},
                    (Vector2){0, 0}, 0, WHITE);
                
                DrawFPS(0, 0);
                if (gui==2) {
                    uint8_t transparency = 200;
                    DrawRectangle(250,220,300,60,{WHITE.r,WHITE.g,WHITE.b,transparency});
                    DrawRectangleLinesEx({250.0f, 220.0f, 300.0f, 60.0f}, 3,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                    DrawText("Return", 275, 238, 24,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                    if (CheckCollisionRecs({250.0f, 220.0f, 300.0f, 60.0f},{(float)GetMouseX(),(float)GetMouseY(),1,1})) {
                        if (IsMouseButtonPressed(0)) {
                            gui = 0;
                            DisableCursor();
                            SetTargetFPS(-1);
                        }
                    }
                    else if (CheckCollisionRecs({250.0f, 220.0f+90.0f, 300.0f, 60.0f},{(float)GetMouseX(),(float)GetMouseY(),1,1})) {
                        if (IsMouseButtonPressed(0)) {
                            gui = 1;
                        }
                    }
                    else if (CheckCollisionRecs({250.0f, 220.0+180.0f, 300.0f, 60.0f},{(float)GetMouseX(),(float)GetMouseY(),1,1})) {
                        if (IsMouseButtonPressed(0)) {
                            CloseWindow();
                        }
                    }
                    DrawRectangle(250,220+90,300,60,{WHITE.r,WHITE.g,WHITE.b,transparency});
                    DrawRectangleLinesEx({250.0f, 220.0f + 90.0f, 300.0f, 60.0f}, 3,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                    DrawText("Graphics", 275, 328, 24,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                   
                    DrawRectangle(250,220+180,300,60,{WHITE.r,WHITE.g,WHITE.b,transparency});
                    DrawRectangleLinesEx({250.0f, 220.0f + 180.0f, 300.0f, 60.0f}, 3, {BLACK.r,BLACK.g,BLACK.b,transparency});
                    DrawText("Quit", 275, 418, 24,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                    
                }
                else if (gui==1) {
                    uint8_t transparency = 200;
                    DrawRectangle(250,220-90,300,60,{WHITE.r,WHITE.g,WHITE.b,transparency});
                    DrawRectangleLinesEx({250.0f, 220.0f-90, 300.0f, 60.0f}, 3,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                    DrawText("Return", 275, 238-90, 24,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                    if (CheckCollisionRecs({250.0f, 220.0f-90, 300.0f, 60.0f},{(float)GetMouseX(),(float)GetMouseY(),1,1})) {
                        if (IsMouseButtonPressed(0)) {
                            gui = 2;
                        }
                    }
                    DrawRectangle(250,220,300,60,{WHITE.r,WHITE.g,WHITE.b,transparency});
                    DrawRectangleLinesEx({250.0f, 220.0f, 300.0f, 60.0f}, 3,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                    DrawText("Native", 275, 238, 24,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                    if (CheckCollisionRecs({250.0f, 220.0f, 300.0f, 60.0f},{(float)GetMouseX(),(float)GetMouseY(),1,1})) {
                        if (IsMouseButtonPressed(0)) {
                            SCALE = 1;
                            width = 800/SCALE;
                            height = 800/SCALE;
                            cameraMoved = true;
                            Render();
                        }
                    }
                    DrawRectangle(250,220+90,300,60,{WHITE.r,WHITE.g,WHITE.b,transparency});
                    DrawRectangleLinesEx({250.0f, 220.0f + 90.0f, 300.0f, 60.0f}, 3,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                    DrawText("80%", 275, 328, 24,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                    if (CheckCollisionRecs({250.0f, 220.0f+90.0f, 300.0f, 60.0f},{(float)GetMouseX(),(float)GetMouseY(),1,1})) {
                        if (IsMouseButtonPressed(0)) {
                            SCALE = 1.3;
                            width = 800/SCALE;
                            height = 800/SCALE;
                            cameraMoved = true;
                            Render();
                        }
                    }
                    DrawRectangle(250,220+180,300,60,{WHITE.r,WHITE.g,WHITE.b,transparency});
                    DrawRectangleLinesEx({250.0f, 220.0f + 180.0f, 300.0f, 60.0f}, 3, {BLACK.r,BLACK.g,BLACK.b,transparency});
                    DrawText("66%", 275, 418, 24,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                    if (CheckCollisionRecs({250.0f, 220.0+180.0f, 300.0f, 60.0f},{(float)GetMouseX(),(float)GetMouseY(),1,1})) {
                        if (IsMouseButtonPressed(0)) {
                            SCALE = 1.5;
                            width = 800/SCALE;
                            height = 800/SCALE;
                            cameraMoved = true;
                            Render();
                        }
                    }
                }
                EndDrawing();
                
                
            }
            else if (worldFinished==1) {
                DisableCursor();
                DrawText("Generating the world, please wait!", 0,0,25,BLACK);
                EndDrawing();
            }
            else if (worldFinished==0) {
                auto Button = [&](float x, float y, int worldSize, const char* text) {
                    DrawRectangleLinesEx({x, y, 200.0f, 50.0f}, 3, BLACK);
                    DrawText(text, x, y, 20, BLACK);
                    if (CheckCollisionRecs({x, y, 200.0f, 50.0f},{(float)GetMouseX(),(float)GetMouseY(),1,1})) {
                        DrawRectangle(x,y, 200.0f, 50.0f, {GRAY.r,GRAY.g,GRAY.b,50});
                        WORLD_WIDTH = worldSize;
                        WORLD_DEPTH = worldSize;
                        
                        camera.position = (Vector3){ WORLD_WIDTH/2, 384, WORLD_DEPTH/2 };
                        if (IsMouseButtonDown(0)) {
                            worldFinished = 1;                     
                            worker = std::thread([=]() {
                                world = new World;
                                world->Init(camera.position);
                                worldFinished.store(2);
                            });
                        }
                    }
                };
                Button(0,100,512,"512x512");
                Button(0,160,1024,"1024x1024");
                Button(0,220,2048,"2048x2048");
                Button(0,280,3072,"3072x3072");
                Button(0,340,4096,"4096x4096");
                EndDrawing();
            }
            
    }
}


};

int main() {
    App *app = new App;
    app->Run();
}
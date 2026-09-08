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
#include <vector>
#include <utility>
#include <algorithm>
using Clock = std::chrono::steady_clock;
const Color colors[255] = {SKYBLUE,GREEN,{uint8_t(GREEN.r*0.9),uint8_t(GREEN.g*0.9),uint8_t(GREEN.b*0.9),255},BROWN,DARKGREEN,GRAY,YELLOW,BLUE, LIME,PINK, WHITE};

constexpr int BUFFER_WIDTH = 1920;
constexpr int BUFFER_HEIGHT = 1080;
constexpr int BUFFER_SIZE = BUFFER_WIDTH * BUFFER_HEIGHT;
int baseFPS = 100;
bool reproject = false;
bool renderClouds = true;
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
    float t;
    float x,y,z;
    uint8_t type;
    bool viable;
    bool traced;
    Vector3 direction;
};

class App {
    public:
    int prevFPS = baseFPS;
    Camera camera;
    Matrix matProj;
    Image imageBuffer;
    Image imageCloudBuffer;
    Texture displayBuffer;
    Texture cloudBuffer;
    Vector3 *directionStorage;
    World *world;
    Hit hits[BUFFER_SIZE];
    Hit hitRepr[BUFFER_SIZE];
    int *stepStorage;
    int *oldStep;
    float *oldDistance;
    std::atomic<int> worldFinished{0};
    std::thread worker;
    bool cameraMoved = true;
    int frame = 0;
    Vector3*ids;
    WorldType worldType = WORLD_PLAINS;
    std::vector<std::pair<int, int>> generationOrder;
    size_t nextColumnToGenerate = 0;
    size_t nextColumnToFinalize = 0;
    
    uint8_t *cloudNoise;       
    uint8_t *cloudHeight;       
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
        imageCloudBuffer = GenImageColor(width,height,BLACK);
        ImageFormat(&imageBuffer,PIXELFORMAT_UNCOMPRESSED_R8G8B8);
        displayBuffer = LoadTextureFromImage(imageBuffer);
        cloudBuffer = LoadTextureFromImage(imageCloudBuffer);
        directionStorage = (Vector3*)MemAlloc(BUFFER_SIZE*sizeof(Vector3));
        stepStorage = (int*)MemAlloc(BUFFER_SIZE*sizeof(int));
        oldDistance = (float*)MemAlloc(BUFFER_SIZE*sizeof(float));
        oldStep = (int*)MemAlloc(BUFFER_SIZE*sizeof(int));
        ids =  (Vector3*)MemAlloc(BUFFER_SIZE*sizeof(Vector3));
        world = new World;
         for (int i = 0; i < BUFFER_SIZE; i++) {
            oldStep[i] = 0;
            oldDistance[i] = 0;
        }
        cloudNoise =  GenImagePerlinNoiseOptimized(1024,1024,0,0,16);
        cloudHeight = GenImagePerlinNoiseOptimized(1024,1024,0,0,64);
        camera.position = {(float)WORLD_WIDTH/2,WORLD_HEIGHT/2,(float)WORLD_DEPTH/2};
    }
    void Render() {
        Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
        Matrix viewInv = MatrixInvert(matView);
        auto dirStart = Clock::now();
           
        if (cameraMoved) {
                    
            if (nextColumnToGenerate < generationOrder.size()) {
                int cameraChunkX = (int)camera.position.x / 32;
                int cameraChunkZ = (int)camera.position.z / 32;

                std::sort(
                    generationOrder.begin() + nextColumnToGenerate,
                    generationOrder.end(),
                    [cameraChunkX, cameraChunkZ](const std::pair<int, int>& a,
                                                const std::pair<int, int>& b) {
                        int adx = a.first - cameraChunkX;
                        int adz = a.second - cameraChunkZ;
                        int bdx = b.first - cameraChunkX;
                        int bdz = b.second - cameraChunkZ;

                        return adx * adx + adz * adz <
                            bdx * bdx + bdz * bdz;
                    }
                );
            }
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
                        if (!world->voxelChunks[ix>>5][iy>>5][iz>>5].generated) break;
                        const float jump = std::max({
                            STEP(chunk.distanceToClosestVoxel, 32),
                            STEP(chunk.distance16[IDX(lx >> 4, ly >> 4, lz >> 4, 2)], 16),
                            STEP(GET_DISTANCE8(chunk,IDX(lx >> 3, ly >> 3, lz >> 3, 4)), 8),
                            STEP(GET_DISTANCE4(chunk,IDX(lx >> 2, ly >> 2, lz >> 2, 8)), 4)
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
        
        auto reprBeg = Clock::now();
        if (reproject) {
            const float m0  = matView.m0;
            const float m1  = matView.m1;
            const float m2  = matView.m2;
            const float m4  = matView.m4;
            const float m5  = matView.m5;
            const float m6  = matView.m6;
            const float m8  = matView.m8;
            const float m9  = matView.m9;
            const float m10 = matView.m10;
            const float m12 = matView.m12;
            const float m13 = matView.m13;
            const float m14 = matView.m14;
            #pragma omp parallel for collapse(2)
            for (int x = 0; x < width; x+=1) {
                for (int y = 0; y < height; y+=1) {
                    int pixelIndex = x * BUFFER_HEIGHT + y;
                    if ((x + y + frame) % 2 == 0) continue;
                    if (hits[pixelIndex].viable) {
                        
                        const float px = hits[pixelIndex].x;
                        const float py = hits[pixelIndex].y;
                        const float pz = hits[pixelIndex].z;

                        float viewX =m0 * px +m4 * py +m8 * pz +m12;
                        float viewY =m1 * px +m5 * py +m9 * pz +m13;
                        float viewZ =m2 * px +m6 * py +m10 * pz +m14;

                        float invW = -1.0f / viewZ;

                        float projScale = 1.73205f;

                        float ndcX = viewX * projScale * invW;
                        float ndcY = -viewY * projScale * invW;

                        Vector2 pp = {
                            (ndcX + 1.0f) * 400,
                            (ndcY + 1.0f) * 400
                        };
                        if (pp.x>=0 && pp.y>=0 && pp.x<width && pp.y<height) {
                            hitRepr[(int)(int(pp.x) * BUFFER_HEIGHT + int(pp.y))] = hits[pixelIndex];
                            
                        }
                    } 
                    hits[pixelIndex].traced = false;
                    hits[pixelIndex].viable = false;
                    
                }
            }
            #pragma omp parallel for collapse(2)
            for (int x = 0; x < width; x+=1) {
                for (int y = 0; y < height; y+=1) {
                    int pixelIndex = x * BUFFER_HEIGHT + y;
                    if ((x + y + frame) % 2 == 0) continue;
                    if (hitRepr[pixelIndex].viable) {
                        hits[pixelIndex] = hitRepr[pixelIndex];
                        hitRepr[pixelIndex].viable = false;
                    }
                    
                }
            }
        }
        else {
            #pragma omp parallel for collapse(2)
            for (int x = 0; x < width; x+=1) {
                for (int y = 0; y < height; y+=1) {
                    int pixelIndex = x * BUFFER_HEIGHT + y;
                    if ((x + y + frame) % 2 == 0) continue;
                    hits[pixelIndex].traced = false;
                    hits[pixelIndex].viable = false;
            }
            }
        }
        auto renderStart = Clock::now();
        
        //std::cout<<"m0: "<<matView.m0<<" m4: "<<matView.m4<<"m8:"<<matView.m8<<" m12:"<<matView.m12<<"\n";
        //std::cout<<"m1: "<<matView.m1<<" m5: "<<matView.m5<<"m9:"<<matView.m9<<" m13:"<<matView.m13<<"\n";
        //std::cout<<"m2: "<<matView.m2<<" m6: "<<matView.m6<<"m0:"<<matView.m10<<" m14:"<<matView.m14<<"\n";
        #pragma omp parallel for collapse(2)
        for (int x = 0; x < width; x+=1) {
            for (int y = 0; y < height; y+=1) {
                int idx = (y * imageBuffer.width + x) * 3;
                int pixelIndex = x * BUFFER_HEIGHT + y;
                if (hits[pixelIndex].traced) continue;
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
                hits[pixelIndex].traced = true;
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
                    if (!world->voxelChunks[cx][cy][cz].generated) break;
                    if (world->voxelChunks[cx][cy][cz].containsBlocks) {
                        int lx = int(voxelX) % 32;
                        int ly = int(voxelY) % 32;
                        int lz = int(voxelZ) % 32;
                        int lodr = world->voxelChunks[cx][cy][cz].lod; 
                        int lodIndex = IDX(lx/lodr,ly/lodr,lz/lodr,world->voxelChunks[cx][cy][cz].size);
                        if (world->traversalChunks[cx][cy][cz].occupancy[lodIndex >> 6] & (1ull << (lodIndex & 63))) {
                            uint8_t type;
                            if (world->voxelChunks[cx][cy][cz].palletized==0) {
                                type = READ_VOXEL(world->voxelChunks[cx][cy][cz], lodIndex);
                            }
                            else type = world->voxelChunks[cx][cy][cz].palletized;
                            hits[pixelIndex].viable = true;
                            hits[pixelIndex].type = type;
                            hits[pixelIndex].x = voxelX;
                            hits[pixelIndex].y = voxelY;
                            hits[pixelIndex].z = voxelZ;
                            hits[pixelIndex].direction = direction;
                            hits[pixelIndex].t = t;
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
        if (renderClouds) {
            #pragma omp parallel for collapse(2)
            for (int x = 0; x < 800/4; x+=1) {
                for (int y = 0; y < 800/4; y+=1) {
                    
                    int idx = (y * imageBuffer.width + x) * 4;
                    if ((x + y + frame) % 2 == 0) continue;
                    ((unsigned char *)imageCloudBuffer.data)[idx] = 0;
                    ((unsigned char *)imageCloudBuffer.data)[idx + 1] = 0;
                    ((unsigned char *)imageCloudBuffer.data)[idx + 2] = 0;
                    ((unsigned char *)imageCloudBuffer.data)[idx + 3] = 0;
                    bool traceThisRay = true;
                    for (int fx = 0; fx < 4; fx++) {
                        for (int fy = 0; fy < 4; fy++) {
                            if (hits[(x*4+fx) * BUFFER_HEIGHT + (y*4+fy)].viable) traceThisRay = false;

                        }
                    }
                    if (!traceThisRay) continue;
                    float cloudScreenY = y * 4.0f + 2.0f;
                    float cloudScreenX = x * 4.0f + 2.0f;

                    alignas(32) float xs[8], ys[8], zs[8];

                    GetScreenToWorldRay8(
                        cloudScreenX,
                        cloudScreenY,
                        800,
                        800,
                        viewInv,
                        xs, ys, zs
                    );

                    Vector3 direction = { xs[0], ys[0], zs[0] };
                    if (direction.y<0 && camera.target.y<100) continue;
                    float voxelX = camera.position.x; 
                    float voxelY = camera.position.y; 
                    float voxelZ = camera.position.z; 
                    float cloudStrength = 0.0f;
                    const int cloudOffset = 1024;
                    if (direction.y*2048+voxelY<cloudOffset) continue;
                    int distanceSkipped = Vector3Distance(camera.position,{camera.position.x,cloudOffset,camera.position.z});
                    voxelX += direction.x*distanceSkipped;
                    voxelY += direction.y*distanceSkipped;
                    voxelZ += direction.z*distanceSkipped;
                    for (int i = distanceSkipped; i < 2048;) {
                        int lod = 1;
                        i+=lod;
                        voxelX += direction.x*lod;
                        voxelY += direction.y*lod;
                        voxelZ += direction.z*lod;
                        int nx = ((int)voxelX % 1024 + 1024+frame/4) % 1024;
                        int nz = ((int)voxelZ % 1024 + 1024) % 1024;
                        int noiseValue = cloudNoise[nx + nz * 1024];
                        int heightValue = cloudHeight[nx + nz * 1024];
                        int cloudHeight = 1 + (heightValue * 10) / 256;
                        const int cutoff = 140;
                        if (noiseValue > cutoff) {
                            if (voxelY > 100- cloudHeight+cloudOffset && voxelY < 100 + cloudHeight+cloudOffset) {
                                cloudStrength += (float(noiseValue)/1500.0f);
                            }
                        }
                    }
                    if (cloudStrength>1) {
                        cloudStrength = 1;
                    }
                    ((unsigned char *)imageCloudBuffer.data)[idx]     = 255*cloudStrength;
                    ((unsigned char *)imageCloudBuffer.data)[idx + 1] = 255*cloudStrength;
                    ((unsigned char *)imageCloudBuffer.data)[idx + 2] = 255*cloudStrength;
                    ((unsigned char *)imageCloudBuffer.data)[idx + 3] = 255*cloudStrength;
                }
            }
        }
        
        auto renderEnd = Clock::now();
        auto lightStart = Clock::now();
        if (IsKeyPressed(KEY_F)) reproject = !reproject;
        int r = 0;
        if (frame%3==0) {

            for (int x = 0; x < width; x+=2) {
                for (int y = 0; y < height; y+=2) {
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
                    if (!hits[pixelIndex].viable) continue;
                    if ((x + y + frame) % 2 == 0) continue;
                    uint8_t type = hits[pixelIndex].type;
                    if (type==0) continue;;
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
                    if (!world->voxelChunks[dx][dy][dz].containsLight) continue;
                    uint8_t lightValR = world->voxelChunks[dx][dy][dz].voxelLightValueR[id];
                    uint8_t lightValG = world->voxelChunks[dx][dy][dz].voxelLightValueG[id];
                    uint8_t lightValB = world->voxelChunks[dx][dy][dz].voxelLightValueB[id];

                    if (lightValR != 0) {
                        strengthR = float(lightValR - 1) / 253.0f;
                        strengthG = float(lightValG - 1) / 253.0f;
                        strengthB = float(lightValB - 1) / 253.0f;
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
                            if (!world->voxelChunks[cx][cy][cz].generated) break;
                            if (world->voxelChunks[cx][cy][cz].containsBlocks) {
                                int lodr = world->voxelChunks[cx][cy][cz].lod; 
                                int lodIndex = IDX(lx/lodr,ly/lodr,lz/lodr,world->voxelChunks[cx][cy][cz].size);
                                if (world->traversalChunks[cx][cy][cz].occupancy[lodIndex >> 6] & (1ull << (lodIndex & 63))) {
                                    uint8_t typer;
                                    if (world->voxelChunks[cx][cy][cz].palletized==0) {
                                        typer = READ_VOXEL(world->voxelChunks[cx][cy][cz], lodIndex);

                                    }
                                    else typer = world->voxelChunks[cx][cy][cz].palletized;
                                    if (voxelMetaData[typer].translucent) {
                                        strengthR *= voxelMetaData[typer].lightAbsorbR; 
                                        strengthG *= voxelMetaData[typer].lightAbsorbG; 
                                        strengthB *= voxelMetaData[typer].lightAbsorbB; 
                                    }
                                    else if (typer!=WATER) {
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
                                STEP(chunk.distanceToClosestVoxel, std::max(32, lod)),
                                STEP(chunk.distance16[IDX(lx >> 4, ly >> 4, lz >> 4, 2)], std::max(16, lod)),
                                STEP(GET_DISTANCE8(chunk,IDX(lx >> 3, ly >> 3, lz >> 3, 4)), std::max(8, lod)),
                                STEP(GET_DISTANCE4(chunk,IDX(lx >> 2, ly >> 2, lz >> 2, 8)), std::max(4, lod))
                            });
                            jump = std::max(jump,1.0f);
                            if (jump > 0.0f) {
                                shadowT += jump;
                                shadowX += sunDirection.x * jump;
                                shadowY += sunDirection.y * jump;
                                shadowZ += sunDirection.z * jump;
                            } 
                        }
                    }
                }
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
                if (!world->voxelChunks[dx][dy][dz].containsLight) continue;
                    
                uint8_t lightValR = world->voxelChunks[dx][dy][dz].voxelLightValueR[id];
                uint8_t lightValG = world->voxelChunks[dx][dy][dz].voxelLightValueG[id];
                uint8_t lightValB = world->voxelChunks[dx][dy][dz].voxelLightValueB[id];

                if (lightValR != 0) {
                    strengthR = float(lightValR - 1) / 253.0f;
                    strengthG = float(lightValG - 1) / 253.0f;
                    strengthB = float(lightValB - 1) / 253.0f;
                }
                Color colorToMix = SKYCOLOR;
                float mixStrength = 0;
                
                if (voxelMetaData[type].reflective) {
                    mixStrength = 0.5f;                        
                    Vector3 direction = hits[pixelIndex].direction;
                    Vector3 reflected = Vector3Normalize(Vector3Subtract(
                        direction,
                        Vector3Scale(
                            {0.0f, 1.0f, 0.0f},
                            2.0f * Vector3DotProduct(direction, {0.0f, 1.0f, 0.0f})
                        )
                    ));
                    float voxelX = hits[pixelIndex].x+reflected.x*1.5;
                    float voxelY = hits[pixelIndex].y+reflected.y*1.5;
                    float voxelZ = hits[pixelIndex].z+reflected.z*1.5;
                    float reflectionT = 0;
                    while (reflectionT < 512) {
                        if (voxelX < 0.0f || voxelY < 0.0f || voxelZ < 0.0f ||
                            voxelX >= WORLD_WIDTH || voxelY >= WORLD_HEIGHT || voxelZ >= WORLD_DEPTH) {
                            break;
                        }
                        int lod = 1;
                        if (reflectionT > LOD16_START) lod = 16;
                        else if (reflectionT > LOD8_START) lod = 8;
                        else if (reflectionT > LOD4_START) lod = 4;
                        else if (reflectionT > LOD2_START) lod = 2;
                        int ix = (int)voxelX;
                        int iy = (int)voxelY;
                        int iz = (int)voxelZ;
                        int cx = ix >> 5;
                        int cy = iy >> 5;
                        int cz = iz >> 5;
                        int lx = ix & 31;
                        int ly = iy & 31;
                        int lz = iz & 31;
                        TraversalChunk& chunk = world->traversalChunks[cx][cy][cz];
                        if (!world->voxelChunks[cx][cy][cz].generated) break;
                        if (world->voxelChunks[cx][cy][cz].containsBlocks) {
                            int lodr = world->voxelChunks[cx][cy][cz].lod; 
                            int lodIndex = IDX(lx/lodr,ly/lodr,lz/lodr,world->voxelChunks[cx][cy][cz].size);
                            if (world->traversalChunks[cx][cy][cz].occupancy[lodIndex >> 6] & (1ull << (lodIndex & 63))) {
                                uint8_t typer;
                                if (world->voxelChunks[cx][cy][cz].palletized==0) {
                                    typer = READ_VOXEL(world->voxelChunks[cx][cy][cz], lodIndex);
                                }
                                else typer = world->voxelChunks[cx][cy][cz].palletized;
                                mixStrength = 0.5f;
                                float R = 0.5;
                                float G = 0.5;
                                float B = 0.5;
                                
                                if (world->voxelChunks[cx][cy][cz].containsLight) {
                                    R = float(world->voxelChunks[dx][dy][dz].voxelLightValueR[lodIndex])/255.0f;
                                    G = float(world->voxelChunks[dx][dy][dz].voxelLightValueG[lodIndex])/255.0f;
                                    B = float(world->voxelChunks[dx][dy][dz].voxelLightValueB[lodIndex])/255.0f;
                                }
                                colorToMix = {uint8_t(colors[typer].r*R),uint8_t(colors[typer].g*G),uint8_t(colors[typer].b*B),255};
                                break;
                            }
                        }

                        float jump = std::max({
                            STEP(chunk.distanceToClosestVoxel, std::max(32, lod)),
                            STEP(chunk.distance16[IDX(lx >> 4, ly >> 4, lz >> 4, 2)], std::max(16, lod)),
                            STEP(GET_DISTANCE8(chunk,IDX(lx >> 3, ly >> 3, lz >> 3, 4)), std::max(8, lod)),
                            STEP(GET_DISTANCE4(chunk,IDX(lx >> 2, ly >> 2, lz >> 2, 8)), std::max(4, lod))
                        });
                        jump = std::max(jump,1.0f);
                        if (jump > 0.0f) {
                            reflectionT += jump;
                            voxelX += reflected.x * jump;
                            voxelY += reflected.y * jump;
                            voxelZ += reflected.z * jump;
                        }
                    
                    }
                }
                float baseR = colors[type].r * strengthR;
                float baseG = colors[type].g * strengthG;
                float baseB = colors[type].b * strengthB;

                float finalR = baseR * (1.0f - mixStrength)
                            + colorToMix.r * mixStrength;

                float finalG = baseG * (1.0f - mixStrength)
                            + colorToMix.g * mixStrength;

                float finalB = baseB * (1.0f - mixStrength)
                            + colorToMix.b * mixStrength;

                ((unsigned char*)imageBuffer.data)[idx] =Clamp(finalR, 0.0f, 255.0f);
                ((unsigned char*)imageBuffer.data)[idx + 1] =Clamp(finalG, 0.0f, 255.0f);
                ((unsigned char*)imageBuffer.data)[idx + 2] =Clamp(finalB, 0.0f, 255.0f);
            }
        }
        auto lightEnd = Clock::now();
        
        prevFPS = GetFPS();
        double dirTime = ms(dirStart, dirEnd);
        double renderTime = ms(renderStart, renderEnd);
        double reprTime = ms(reprBeg, renderStart);
        double lightTime = ms(lightStart, lightEnd);
        
        double lowrenderTime = ms(lowrenderStart, lowrenderEnd);
        std::cout
        << "Dir: "       << dirTime       << " ms | "
        << "Render: "    << renderTime    << " ms | "
        << "Repr: "    << reprTime    << " ms | "
         << "LowRender: " << lowrenderTime << " ms | "
         << "Light: "     << lightTime     << " ms \n";
    }
    void Run() {
        int dvdX = 0;
        int dvdY = 0;
        int dvdXChange = 1;
        int dvdYChange = 1;
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
                        
                UpdateTexture(cloudBuffer, imageCloudBuffer.data);
                        
                DrawTexturePro(displayBuffer, 
                    (Rectangle){0, 0, (float)width, (float)height},
                    (Rectangle){0, 0, width*SCALE, height*SCALE},
                    (Vector2){0, 0}, 0, WHITE);
                if (renderClouds) {
                    DrawTexturePro(cloudBuffer, 
                    (Rectangle){0, 0, (float)200, (float)200},
                    (Rectangle){0, 0, 800, 800},
                    (Vector2){0, 0}, 0, WHITE);
                
                }    
                        
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
                    else if (CheckCollisionRecs({250.0f, 220.0f+180.0f, 300.0f, 60.0f},{(float)GetMouseX(),(float)GetMouseY(),1,1})) {
                        if (IsMouseButtonPressed(0)) {
                            gui = 1;
                        }
                    }
                    else if (CheckCollisionRecs({250.0f, 220.0f+90.0f, 300.0f, 60.0f},{(float)GetMouseX(),(float)GetMouseY(),1,1})) {
                        if (IsMouseButtonPressed(0)) {
                            if (worker.joinable()) {
                                worker.join();
                            }
                            world->Reset();
                            generationOrder.clear();
                            nextColumnToGenerate = 0;
                            nextColumnToFinalize = 0;
                            worldFinished = 0;
                            gui = 0;
                        }
                    }
                    else if (CheckCollisionRecs({250.0f, 220.0+270.0f, 300.0f, 60.0f},{(float)GetMouseX(),(float)GetMouseY(),1,1})) {
                        if (IsMouseButtonPressed(0)) {
                            CloseWindow();
                        }
                    }
                    DrawRectangle(250,220+90,300,60,{WHITE.r,WHITE.g,WHITE.b,transparency});
                    DrawRectangleLinesEx({250.0f, 220.0f + 90, 300.0f, 60.0f}, 3,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                    DrawText("Main Menu", 275, 328, 24,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                   
                    DrawRectangle(250,220+180,300,60,{WHITE.r,WHITE.g,WHITE.b,transparency});
                    DrawRectangleLinesEx({250.0f, 220.0f + 180, 300.0f, 60.0f}, 3,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                    DrawText("Graphics", 275, 418, 24,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                   
                    DrawRectangle(250,220+270,300,60,{WHITE.r,WHITE.g,WHITE.b,transparency});
                    DrawRectangleLinesEx({250.0f, 220.0f + 270.0f, 300.0f, 60.0f}, 3, {BLACK.r,BLACK.g,BLACK.b,transparency});
                    DrawText("Quit", 275, 508, 24,  {BLACK.r,BLACK.g,BLACK.b,transparency});
                    
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
                if (nextColumnToGenerate < generationOrder.size()) {
                    const int x = generationOrder[nextColumnToGenerate].first;
                    const int z = generationOrder[nextColumnToGenerate].second;

                    float dx = x * 32.0f + 16.0f - camera.position.x;
                    float dz = z * 32.0f + 16.0f - camera.position.z;

                    if (dx * dx + dz * dz <= RENDERDISTANCE * RENDERDISTANCE) {
                        world->InitColumn(camera.position, x, z);
                        world->GenerateTerrain(world->chunkBiome[x][z], x, z);
                            
                        auto sphere = [&](int x, int y, int z, int size, uint8_t type) {
                            int middleX = x + size/2;
                            int middleY = y + size/2;
                            int middleZ = z + size/2;
                            for (int dx = 0; dx < size; dx++) {
                                for (int dy = 0; dy < size; dy++) {
                                    for (int dz = 0; dz < size; dz++) {
                                        if (Vector3Distance({(float)middleX,(float)middleY,(float)middleZ},{(float)dx+x,(float)dy+y,(float)dz+z})<size/2) {
                                            int cx = x+dx;
                                            int cy = y+dy;
                                            int cz = z+dz;
                                            if (!world->voxelChunks[cx/32][cy/32][cz/32].containsBlocks) {

                                                world->voxelChunks[cx/32][cy/32][cz/32].voxels = (uint8_t*)MemAlloc(32 * 32 * 32);
                                                memset(world->voxelChunks[cx/32][cy/32][cz/32].voxels,0,32*32*32);
                                                world->voxelChunks[cx/32][cy/32][cz/32].containsBlocks = true;
                                            }
                                            world->voxelChunks[cx/32][cy/32][cz/32].voxels[IDX(cx%32,cy%32,cz%32,32)] = type;
                                        }

                                    }
                                }   
                            }
                        }; 
                        auto setVoxel = [&](int x, int y, int z, uint8_t type)
                        {
                            if (x < 0 || x >= WORLD_WIDTH ||
                                y < 0 || y >= WORLD_HEIGHT ||
                                z < 0 || z >= WORLD_DEPTH)
                                return;
                            
                            VoxelChunk& chunk = world->voxelChunks[x / 32][y / 32][z / 32];
                            if (chunk.generated) return;
                            if (!chunk.containsBlocks)
                            {
                                chunk.voxels = (uint8_t*)MemAlloc(32 * 32 * 32);
                                memset(chunk.voxels, AIR, 32 * 32 * 32);
                                chunk.containsBlocks = true;
                            }

                            chunk.voxels[IDX(x % 32, y % 32, z % 32, 32)] = type;
                        };
                        auto branch = [&](Vector3 from, Vector3 to, float radius, uint8_t type)
                        {
                            Vector3 delta = Vector3Subtract(to, from);
                            int steps = (int)ceilf(Vector3Length(delta) * 2.0f);
                            steps = std::max(steps, 1);
                            for (int i = 0; i <= steps; ++i)
                            {
                                float t = i / (float)steps;
                                Vector3 p = Vector3Lerp(from, to, t);
                                int ir = std::max(radius * (1.0f - t * 0.65f),0.65f);
                                for (int dx = -ir; dx <= ir; ++dx)
                                    for (int dy = -ir; dy <= ir; ++dy)
                                        for (int dz = -ir; dz <= ir; ++dz)
                                            if (dx*dx + dy*dy + dz*dz <= ir*ir)
                                                setVoxel(p.x + dx,p.y + dy,p.z + dz,type);
                            }
                        };
                        auto leafCluster = [&](int cx, int cy, int cz, int radius)
                        {
                            for (int i = 0; i < GET_RANDOM_VALUE(5, 9); ++i)
                            {
                                int rx = GET_RANDOM_VALUE(radius / 2, radius);
                                int ry = GET_RANDOM_VALUE(2, std::max(3, radius / 2));
                                int rz = GET_RANDOM_VALUE(radius / 2, radius);

                                int ox = GET_RANDOM_VALUE(-radius / 2, radius / 2);
                                int oz = GET_RANDOM_VALUE(-radius / 2, radius / 2);
                                int oy = GET_RANDOM_VALUE(-radius, radius / 3);
                                for (int x = -rx; x <= rx; ++x) {
                                    for (int y = -ry; y <= ry; ++y) {
                                        for (int z = -rz; z <= rz; ++z) {
                                            float nx = x / (float)rx;
                                            float ny = y / (float)ry;
                                            float nz = z / (float)rz;

                                            float d = nx*nx + ny*ny + nz*nz;

                                            if (d > 1.0f) continue;
                                            if (GET_RANDOM_VALUE(0, 100) < 18) continue;

                                            if (d > 0.55f) {
                                                int edgeChance = (int)((d - 0.55f) * 110.0f);
                                                if (GET_RANDOM_VALUE(0, 100) < edgeChance) continue; 
                                            }

                                            setVoxel(cx + x+ox, cy + y+oy, cz + z+oz, LEAF);
                                        }
                                    }
                                }
                            }

                            int hanging = GET_RANDOM_VALUE(2, 5);

                            for (int i = 0; i < hanging; ++i)
                            {
                                int hx = cx + GET_RANDOM_VALUE(-radius, radius);
                                int hz = cz + GET_RANDOM_VALUE(-radius, radius);
                                for (int y = 0; y < GET_RANDOM_VALUE(2, 6); ++y)
                                    if (GET_RANDOM_VALUE(0, 100) < 75 - y * 8) setVoxel(hx + GET_RANDOM_VALUE(-1, 1),cy - radius / 2 - y,hz + GET_RANDOM_VALUE(-1, 1),LEAF);
                            }
                        };
                        auto cactusBudCluster = [&](int cx, int cy, int cz)
                        {
                            setVoxel(cx, cy, cz, CACTUS_BUD);

                            if (GET_RANDOM_VALUE(0, 100) < 70) setVoxel(cx + 1, cy, cz, CACTUS_BUD);
                            if (GET_RANDOM_VALUE(0, 100) < 70) setVoxel(cx - 1, cy, cz, CACTUS_BUD);
                            if (GET_RANDOM_VALUE(0, 100) < 70) setVoxel(cx, cy, cz + 1, CACTUS_BUD);
                            if (GET_RANDOM_VALUE(0, 100) < 70) setVoxel(cx, cy, cz - 1, CACTUS_BUD);

                            if (GET_RANDOM_VALUE(0, 100) < 50) setVoxel(cx, cy + 1, cz, CACTUS_BUD);
                        };
                        auto cactusPlant = [&](int x, int groundY, int z)
                        {
                            const int height = GET_RANDOM_VALUE(18, 34);
                            const float trunkRadius = GET_RANDOM_VALUE(12, 18) / 10.0f;

                            Vector3 base = { (float)x, (float)groundY, (float)z };
                            Vector3 mid = {(float)x + GET_RANDOM_VALUE(-1, 1),(float)groundY + height * 0.55f,(float)z + GET_RANDOM_VALUE(-1, 1)};
                            Vector3 top = {mid.x + GET_RANDOM_VALUE(-1, 1),(float)groundY + height,mid.z + GET_RANDOM_VALUE(-1, 1)};

                            branch(base, mid, trunkRadius, CACTUS);
                            branch(mid, top, trunkRadius * 0.9f, CACTUS);

                            if (GET_RANDOM_VALUE(0, 100) < 75)
                                cactusBudCluster((int)top.x, (int)top.y, (int)top.z);

                            int arms = GET_RANDOM_VALUE(0, 3);

                            for (int a = 0; a < arms; ++a)
                            {
                                float angle = GET_RANDOM_VALUE(0, 359) * DEG2RAD;

                                float startHeight = height * (GET_RANDOM_VALUE(40, 78) / 100.0f);
                                Vector3 start = Vector3Lerp(base, top, startHeight / height);
                                float armLength = GET_RANDOM_VALUE(8, 18);
                                Vector3 elbow = {start.x + cosf(angle) * (armLength * 0.45f),start.y + GET_RANDOM_VALUE(3, 7),start.z + sinf(angle) * (armLength * 0.45f)};
                                Vector3 end = {elbow.x,elbow.y + GET_RANDOM_VALUE(5, 10),elbow.z};

                                branch(start, elbow, trunkRadius * 0.75f, CACTUS);
                                branch(elbow, end, trunkRadius * 0.60f, CACTUS);

                                if (GET_RANDOM_VALUE(0, 100) < 85)
                                    cactusBudCluster((int)end.x, (int)end.y, (int)end.z);
                            }

                            int sideBuds = GET_RANDOM_VALUE(0, 4);
                            for (int i = 0; i < sideBuds; ++i)
                            {
                                float t = GET_RANDOM_VALUE(55, 95) / 100.0f;
                                Vector3 p = Vector3Lerp(base, top, t);

                                int ox = GET_RANDOM_VALUE(-1, 1);
                                int oz = GET_RANDOM_VALUE(-1, 1);
                                if (ox == 0 && oz == 0) ox = 1;

                                setVoxel((int)p.x + ox, (int)p.y, (int)p.z + oz, CACTUS_BUD);
                            }
                        };
                        auto oakTree = [&](int x, int groundY, int z)
                        {
                            const int height = GET_RANDOM_VALUE(52, 72);
                            const float trunkRadius = GET_RANDOM_VALUE(20, 28) / 10.0f;
                            Vector3 trunkBottom = {(float)x,(float)groundY,(float)z};
                            Vector3 trunkMiddle = {(float)x + GET_RANDOM_VALUE(-2, 2),(float)groundY + height * 0.55f,(float)z + GET_RANDOM_VALUE(-2, 2)};
                            Vector3 trunkTop = {trunkMiddle.x + GET_RANDOM_VALUE(-2, 2),(float)groundY + height,trunkMiddle.z + GET_RANDOM_VALUE(-2, 2)};
                            branch(trunkBottom,trunkMiddle,trunkRadius,TREE_BARK);
                            branch(trunkMiddle,trunkTop,trunkRadius * 0.75f,TREE_BARK);

                            const int mainBranches = GET_RANDOM_VALUE(7, 11);

                            for (int b = 0; b < mainBranches; ++b)
                            {
                                float angle = ((float)b / mainBranches) * PI * 2.0f+ GET_RANDOM_VALUE(-40, 40) * DEG2RAD;
                                float startHeight = height * (GET_RANDOM_VALUE(45, 76) /100.0f);
                                Vector3 start = {trunkBottom.x + (trunkTop.x - trunkBottom.x) * (startHeight / height),groundY + startHeight, trunkBottom.z + (trunkTop.z - trunkBottom.z) * (startHeight / height)};
                                float length =GET_RANDOM_VALUE(16, 30);
                                Vector3 end = {start.x + cosf(angle) * length,start.y + GET_RANDOM_VALUE(5, 15),start.z + sinf(angle) * length};
                                branch(start,end,GET_RANDOM_VALUE(13, 20) / 10.0f,TREE_BARK);

                                int childBranches = GET_RANDOM_VALUE(2, 4);

                                for (int c = 0; c < childBranches; ++c)
                                {
                                    float childAngle = angle + GET_RANDOM_VALUE(-55, 55) * DEG2RAD;
                                    Vector3 childStart = Vector3Lerp(start, end,  GET_RANDOM_VALUE(45, 90) / 100.0f);
                                    float childLength = GET_RANDOM_VALUE(7, 16);
                                    Vector3 childEnd = {childStart.x + cosf(childAngle) * childLength,childStart.y + GET_RANDOM_VALUE(2, 9), childStart.z + sinf(childAngle) * childLength};

                                    branch(childStart,childEnd,GET_RANDOM_VALUE(7, 12) / 10.0f,TREE_BARK);
                                    leafCluster((int)childEnd.x,(int)childEnd.y,(int)childEnd.z,GET_RANDOM_VALUE(5, 8));
                                }
                                leafCluster((int)end.x,(int)end.y,(int)end.z,GET_RANDOM_VALUE(6, 9));
                            }
                            for (int i = 0; i < 4; ++i) {
                                leafCluster((int)trunkTop.x + GET_RANDOM_VALUE(-8, 8),(int)trunkTop.y + GET_RANDOM_VALUE(-3, 8),(int)trunkTop.z + GET_RANDOM_VALUE(-8, 8),GET_RANDOM_VALUE(6, 9));
                            }
                        };
                        if (GetRandomValue(0,3)==0) {
                            int offx = GetRandomValue(-24,24);
                            int offy = GetRandomValue(-24,24);
                            for (int i = 0; i <WORLD_HEIGHT; i++) {
                                if (world->chunkBiome[x][z]==WORLD_DESERT) {
                                    if (world->GetVoxel(x*32+offx,i,z*32+offy)==SAND) {
                                        cactusPlant(x*32+offx,i,z*32+offy);
                                        break;
                                    }
                                }
                                else if (world->chunkBiome[x][z]==WORLD_MOUNTAINS) {
                                if (world->GetVoxel(x*32+offx,i,z*32+offy)==GRASS) {
                                    oakTree(x*32+offx,i,z*32+offy);
                                    break;
                                }}
                            }
                            
                        }
                        world->BuildDistanceToClosestVoxel(x, z);
                        world->BuildDistanceLayerBaseline(x, z);
                        world->BuildDistanceLayer(x, z, 8);
                        world->BuildDistanceLayer(x, z, 4);

                        for (int y = 0; y < WORLD_HEIGHT / 32; y++) {
                            world->voxelChunks[x][y][z].CheckOriginals(
                                world->traversalChunks[x][y][z].buildID
                            );
                        }

                        generatedChunks++;
                        world->GenerateOccupancyMasks(x, z);
                    }

                    nextColumnToGenerate++;
                }
                else if (nextColumnToFinalize < generationOrder.size()) {
                    const int x = generationOrder[nextColumnToFinalize].first;
                    const int z = generationOrder[nextColumnToFinalize].second;

                    for (int y = 0; y < WORLD_HEIGHT/32; y++) {
                        world->traversalChunks[x][y][z].CheckDelta(world->traversalChunks[x][y][z].buildID);
                    }

                    nextColumnToFinalize++;
                }
                EndDrawing();
                
                
            }
            else if (worldFinished==1) {
                //SetTargetFPS(60);
                if (dvdX>width || dvdX<0) dvdXChange *= -1;
                if (dvdY>height || dvdY<0) dvdYChange *= -1; 
                dvdX+=dvdXChange;
                dvdY+=dvdYChange;
                DrawRectangle(dvdX,dvdY,32,32,BLACK);
                DisableCursor();
                DrawText("Generating the world, please wait!", 0,0,25,BLACK);
                EndDrawing();
            }
            else if (worldFinished==0) {
                
                auto WorldTypeButton = [&](float x, float y, WorldType type, const char* text) {
                    Rectangle rect = {x,y,200.0f,50.0f};
                    if (worldType == type) {
                        DrawRectangle(
                            x, y,
                            200.0f, 50.0f,
                            {GRAY.r, GRAY.g, GRAY.b, 100}
                        );
                    }

                    DrawRectangleLinesEx(rect, 3, BLACK);
                    DrawText(text, x, y, 20, BLACK);

                    if (CheckCollisionRecs(rect,{(float)GetMouseX(), (float)GetMouseY(), 1, 1})) {
                        DrawRectangle(x, y,200.0f, 50.0f,{GRAY.r, GRAY.g, GRAY.b, 50});

                        if (IsMouseButtonPressed(0)) {
                            worldType = type;
                        }
                    }
                };
                
                DrawText("WORLD SIZE", 0, 65, 20, BLACK);

                DrawText("WORLD TYPE", 250, 65, 20, BLACK);

                WorldTypeButton(250, 100, WORLD_PLAINS,    "Plains");
                WorldTypeButton(250, 160, WORLD_MOUNTAINS, "Mountains");
                WorldTypeButton(250, 220, WORLD_DESERT,    "Desert");
                DrawRectangleLinesEx({0, 200, 200.0f, 50.0f}, 3, BLACK);
                DrawText("Create World", 0, 200, 20, BLACK);
                if (CheckCollisionRecs({0, 200, 200.0f, 50.0f},{(float)GetMouseX(),(float)GetMouseY(),1,1})) {
                    DrawRectangle(0, 200, 200.0f, 50.0f, {GRAY.r,GRAY.g,GRAY.b,50});
                        
                    if (IsMouseButtonDown(0)) {
                        generationOrder.clear();
                        nextColumnToGenerate = 0;
                        nextColumnToFinalize = 0;

                        const int chunkCountX = WORLD_WIDTH / 32;
                        const int chunkCountZ = WORLD_DEPTH / 32;
                        const int cameraChunkX = std::max(0, std::min(chunkCountX - 1, (int)camera.position.x / 32));
                        const int cameraChunkZ = std::max(0, std::min(chunkCountZ - 1, (int)camera.position.z / 32));

                        generationOrder.reserve(chunkCountX * chunkCountZ);
                        for (int x = 0; x < chunkCountX; x++) {
                            for (int z = 0; z < chunkCountZ; z++) {
                                generationOrder.push_back({x, z});
                            }
                        }

                        std::sort(generationOrder.begin(), generationOrder.end(),
                            [cameraChunkX, cameraChunkZ](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                                const long long adx = a.first - cameraChunkX;
                                const long long adz = a.second - cameraChunkZ;
                                const long long bdx = b.first - cameraChunkX;
                                const long long bdz = b.second - cameraChunkZ;
                                return adx * adx + adz * adz < bdx * bdx + bdz * bdz;
                            });

                        worldFinished = 1;                     
                        generatedChunks = 0;
                        worker = std::thread([=]() {
                            world->Init();
                            worldFinished.store(2);
                        });
                        DisableCursor();
                        SetTargetFPS(-1);
                    }
                }
                EndDrawing();
            }
        std::cout<<generatedChunks<<"\n";
    }
}


};

int main() {
    App *app = new App;
    app->Run();
}
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
#include <queue>
#include <limits>
#include <algorithm>
#include <immintrin.h>
enum VoxelTypes {
    AIR=0,
    GRASS=1,
    GRASS_VARIANT=2,
    TREE_BARK=3,
    LEAF=4
};
using Clock = std::chrono::steady_clock;
#define STEP(d, size) (((d) > 1 && (d) != 255) ? (((d) - 1) * (size)) : 0.0f)
#define DIRECTION_DELTA(d) ([&]() {               \
    const float dx = (d).x - direction.x;         \
    const float dy = (d).y - direction.y;         \
    const float dz = (d).z - direction.z;         \
    return sqrtf(dx * dx + dy * dy + dz * dz);    \
}())
#define GET_RANDOM_VALUE(min_, max_)                             \
({                                                                \
    int _min = (min_);                                            \
    int _max = (max_);                                            \
    unsigned long _range =                                        \
        (unsigned long)((long long)_max - (long long)_min + 1);   \
    const unsigned long _c = (unsigned long)RAND_MAX + 1UL;       \
    const unsigned long _t = _c - (_c % _range);                  \
    unsigned long _r;                                             \
                                                                  \
    do                                                            \
    {                                                             \
        _r = (unsigned long)rand();                               \
    } while (_r >= _t);                                           \
                                                                  \
    _min + (int)(_r % _range);                                   \
})
const float SCALE = 1.2;
const float FOVY = 120.0f;
auto ms = [](auto start, auto end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
};
const int width = 1000/SCALE;
const int height = 1000/SCALE;
const float PIXEL_WORLD_SLOPE = 2.0f * tanf(FOVY * 0.5f * DEG2RAD) / width;
const float LOD2_START  = 2.0f  / PIXEL_WORLD_SLOPE;
const float LOD4_START  = 4.0f  / PIXEL_WORLD_SLOPE;
const float LOD8_START  = 8.0f  / PIXEL_WORLD_SLOPE;
const float LOD16_START = 16.0f / PIXEL_WORLD_SLOPE;

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
const int WORLD_WIDTH = 2048;
const int WORLD_DEPTH = 2048;
const int WORLD_HEIGHT = 512;
const int RENDERDISTANCE = 2048;
struct VoxelChunk {
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
struct TraversalChunk {
    uint8_t distanceToClosestVoxel=0;
    uint64_t *occupancy;
    uint8_t distance16[2][2][2];
    uint8_t distance8[4][4][4];
    uint8_t distance4[8][8][8];
    uint64_t occupancy4[8] = {};
    uint64_t occupancy8 = 0;
    uint8_t occupancy16 = 0;  
    void BuildOccupancyMask(uint8_t *voxels) {
        occupancy = (uint64_t*)MemAlloc(512*sizeof(uint64_t));
        for (int i = 0; i < 512; i++) occupancy[i] = 0;
        
        for (int x = 0; x < 32; ++x) {
            for (int y = 0; y < 32; ++y) {
                for (int z = 0; z < 32; ++z) {
                    const int index = (x << 10) | (y << 5) | z;

                    if (voxels[index] != 0) {
                        occupancy[index >> 6] |= 1ull << (index & 63);
                        const int x4 = x >> 2;
                        const int y4 = y >> 2;
                        const int z4 = z >> 2;

                        const int index4 =
                            (x4 << 6) | (y4 << 3) | z4;

                        occupancy4[index4 >> 6] |=
                            1ull << (index4 & 63);
                        const int x8 = x >> 3;
                        const int y8 = y >> 3;
                        const int z8 = z >> 3;

                        const int index8 =
                            (x8 << 4) | (y8 << 2) | z8;

                        occupancy8 |=
                            1ull << index8;

                        const int x16 = x >> 4;
                        const int y16 = y >> 4;
                        const int z16 = z >> 4;

                        const int index16 =
                            (x16 << 2) | (y16 << 1) | z16;

                        occupancy16 |=
                            uint8_t(1u << index16);
                    }
                }
            }
        }
    }
    void Clear() {
        std::fill(&distance16[0][0][0], &distance16[0][0][0] + 8, 255);
        std::fill(&distance8[0][0][0], &distance8[0][0][0] + 64, 255);
        std::fill(&distance4[0][0][0], &distance4[0][0][0] + 512, 255);
    }
};
struct World {
    VoxelChunk voxelChunks[WORLD_WIDTH/32][WORLD_HEIGHT/32][WORLD_DEPTH/32];
    TraversalChunk traversalChunks[WORLD_WIDTH/32][WORLD_HEIGHT/32][WORLD_DEPTH/32];
    
    inline uint8_t GetVoxel(int x, int y, int z) const {
            
        int cx = x / 32;
        int cy = y / 32;
        int cz = z / 32;
        int lx = x % 32;
        int ly = y % 32;
        int lz = z % 32;
        if (!voxelChunks[cx][cy][cz].containsBlocks) return 0;
        return voxelChunks[cx][cy][cz].voxels[lx * 32 * 32 + ly * 32 + lz];
    }

    inline uint8_t GetLightValue(int x, int y, int z) const {
            
        int cx = x / 32;
        int cy = y / 32;
        int cz = z / 32;
        int lx = x % 32;
        int ly = y % 32;
        int lz = z % 32;
        if (!voxelChunks[cx][cy][cz].containsBlocks) return 0;
        return voxelChunks[cx][cy][cz].voxelLightValue[lx * 32 * 32 + ly * 32 + lz];
    }

    inline void SetLightValue(int x, int y, int z, uint8_t val) const {
            
        int cx = x / 32;
        int cy = y / 32;
        int cz = z / 32;
        int lx = x % 32;
        int ly = y % 32;
        int lz = z % 32;
        if (!voxelChunks[cx][cy][cz].containsBlocks) return;
        voxelChunks[cx][cy][cz].voxelLightValue[lx * 32 * 32 + ly * 32 + lz] = val;
    }
    void BuildDistanceToClosestVoxel() {
        constexpr int CHUNK_COUNT_X = WORLD_WIDTH / 32;
        constexpr int CHUNK_COUNT_Y = WORLD_HEIGHT / 32;
        constexpr int CHUNK_COUNT_Z = WORLD_DEPTH / 32;

        struct ChunkPos { int x, y, z; };
        std::queue<ChunkPos> q;

        for (int x = 0; x < CHUNK_COUNT_X; ++x) {
            for (int y = 0; y < CHUNK_COUNT_Y; ++y) {
                for (int z = 0; z < CHUNK_COUNT_Z; ++z) {
                    VoxelChunk &voxelChunk = voxelChunks[x][y][z];
                    TraversalChunk &traversalChunk = traversalChunks[x][y][z];
                    if (voxelChunk.containsBlocks) {
                        traversalChunk.distanceToClosestVoxel = 0;
                        q.push({x, y, z});
                    } else {
                        traversalChunk.distanceToClosestVoxel = 255;
                    }
                }
            }
        }

        while (!q.empty()) {
            ChunkPos p = q.front();
            q.pop();

            const uint8_t current = traversalChunks[p.x][p.y][p.z].distanceToClosestVoxel;
            if (current == 254) continue;
            const uint8_t nextDistance = current + 1;

            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        if (dx == 0 && dy == 0 && dz == 0) continue;

                        const int nx = p.x + dx;
                        const int ny = p.y + dy;
                        const int nz = p.z + dz;
                        if (nx < 0 || ny < 0 || nz < 0 ||
                            nx >= CHUNK_COUNT_X || ny >= CHUNK_COUNT_Y || nz >= CHUNK_COUNT_Z) {
                            continue;
                        }

                        TraversalChunk &neighbor = traversalChunks[nx][ny][nz];
                        if (nextDistance < neighbor.distanceToClosestVoxel) {
                            neighbor.distanceToClosestVoxel = nextDistance;
                            q.push({nx, ny, nz});
                        }
                    }
                }
            }
        }
    }
    uint8_t *cellPtr(TraversalChunk &chunk, int cellSize) {

        if (cellSize == 16) return &chunk.distance16[0][0][0];
        if (cellSize == 8) return &chunk.distance8[0][0][0];
        return &chunk.distance4[0][0][0];
    }
    void BuildDistanceLayer(int cellSize) {
        const int cellsPerChunk = 32 / cellSize;
        const int gridX = WORLD_WIDTH / cellSize;
        const int gridY = WORLD_HEIGHT / cellSize;
        const int gridZ = WORLD_DEPTH / cellSize;


        for (int cx = 0; cx < WORLD_WIDTH / 32; ++cx) {
            for (int cy = 0; cy < WORLD_HEIGHT / 32; ++cy) {
                for (int cz = 0; cz < WORLD_DEPTH / 32; ++cz) {
                    VoxelChunk &voxelChunk = voxelChunks[cx][cy][cz];
                    TraversalChunk &traversalChunk = traversalChunks[cx][cy][cz];
                    uint8_t *distance = cellPtr(traversalChunk,cellSize);
                    std::fill(distance, distance + cellsPerChunk * cellsPerChunk * cellsPerChunk, 255);
                    if (!voxelChunk.containsBlocks) continue;

                    for (int sx = 0; sx < cellsPerChunk; ++sx) {
                        for (int sy = 0; sy < cellsPerChunk; ++sy) {
                            for (int sz = 0; sz < cellsPerChunk; ++sz) {
                                bool occupied = false;
                                const int bx = sx * cellSize;
                                const int by = sy * cellSize;
                                const int bz = sz * cellSize;

                                for (int x = 0; x < cellSize && !occupied; ++x)
                                    for (int y = 0; y < cellSize && !occupied; ++y)
                                        for (int z = 0; z < cellSize; ++z)
                                            if (voxelChunk.voxels[(bx + x) * 32 * 32 + (by + y) * 32 + bz + z]) {
                                                occupied = true;
                                                break;
                                            }

                                if (occupied)
                                    distance[(sx * cellsPerChunk + sy) * cellsPerChunk + sz] = 0;
                            }
                        }
                    }
                }
            }
        }
        for (int x = 0; x < gridX; ++x) {
            for (int y = 0; y < gridY; ++y) {
                for (int z = 0; z < gridZ; ++z) {
                    uint8_t &cur = cellPtr(traversalChunks[x / cellsPerChunk][y / cellsPerChunk][z/cellsPerChunk],cellSize)[(( x % cellsPerChunk) * cellsPerChunk + ( y % cellsPerChunk)) * cellsPerChunk +( z % cellsPerChunk)];
                    for (int dx = -1; dx <= 1; ++dx) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dz = -1; dz <= 1; ++dz) {
                                if (!(dx < 0 || (dx == 0 && dy < 0) || (dx == 0 && dy == 0 && dz < 0))) continue;
                                const int nx = x + dx, ny = y + dy, nz = z + dz;
                                if (nx < 0 || ny < 0 || nz < 0 || nx >= gridX || ny >= gridY || nz >= gridZ) continue;
                                const uint8_t n = cellPtr(traversalChunks[nx / cellsPerChunk][ny / cellsPerChunk][nz/cellsPerChunk],cellSize)[(( nx % cellsPerChunk) * cellsPerChunk + ( ny % cellsPerChunk)) * cellsPerChunk +( nz % cellsPerChunk)];
                                if (n < 254) cur = std::min<uint8_t>(cur, static_cast<uint8_t>(n + 1));
                            }
                        }
                    }
                }
            }
        }

        for (int x = gridX - 1; x >= 0; --x) {
            for (int y = gridY - 1; y >= 0; --y) {
                for (int z = gridZ - 1; z >= 0; --z) {
                    uint8_t &cur = cellPtr(traversalChunks[x / cellsPerChunk][y / cellsPerChunk][z/cellsPerChunk],cellSize)[(( x % cellsPerChunk) * cellsPerChunk + ( y % cellsPerChunk)) * cellsPerChunk +( z % cellsPerChunk)];
                    for (int dx = -1; dx <= 1; ++dx) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dz = -1; dz <= 1; ++dz) {
                                if (!(dx > 0 || (dx == 0 && dy > 0) || (dx == 0 && dy == 0 && dz > 0))) continue;
                                const int nx = x + dx, ny = y + dy, nz = z + dz;
                                if (nx < 0 || ny < 0 || nz < 0 || nx >= gridX || ny >= gridY || nz >= gridZ) continue;
                                const uint8_t n = cellPtr(traversalChunks[nx / cellsPerChunk][ny / cellsPerChunk][nz/cellsPerChunk],cellSize)[(( nx % cellsPerChunk) * cellsPerChunk + ( ny % cellsPerChunk)) * cellsPerChunk +( nz % cellsPerChunk)];
                                if (n < 254) cur = std::min<uint8_t>(cur, static_cast<uint8_t>(n + 1));
                            }
                        }
                    }
                }
            }
        }
    }
    World() {
        for (int x = 0; x < WORLD_WIDTH/32; x++) {
            for (int y= 0 ; y < WORLD_HEIGHT/32; y++) {
                for (int z = 0; z < WORLD_DEPTH/32; z++) {
                    voxelChunks[x][y][z].voxels = (uint8_t*)MemAlloc(32*32*32);
                    voxelChunks[x][y][z].Clear();
                    traversalChunks[x][y][z].Clear();
                }
            }
        }
        Image noise = GenImagePerlinNoise(
            WORLD_WIDTH,
            WORLD_DEPTH,
            0.0f, 
            0.0f, 
            0.2f 
        );

#pragma omp parallel for collapse(2)
        for (int x = 0; x < WORLD_WIDTH; x++) {
            for (int z = 0; z < WORLD_DEPTH; z++) {
                int height = GetImageColor(noise,x,z).r;
                
                if (GET_RANDOM_VALUE(0,1000)==1) {
                    
                    int dx = 0; 
                    int dy = 0; 
                    int treeHeight = GET_RANDOM_VALUE(20,50);
                    for (int i = 0; i < treeHeight && height + i < WORLD_HEIGHT; i++) {
                            
                        int cx = (x+dx)/32;
                        int cy = (height+i)/32;
                        int cz = (z+dy)/32;
                        voxelChunks[cx][cy][cz].voxels[(x+dx)%32*32*32+((height+i)%32)*32+(z+dy)%32] = 3;
                        voxelChunks[cx][cy][cz].containsBlocks = true;
                        if (GET_RANDOM_VALUE(0,10)==1) {
                            int dxOff = GET_RANDOM_VALUE(-1,1);
                            dx+=dxOff;
                            if (dx+x>WORLD_WIDTH-1 || dx+x<0) dx-=dxOff;
                                
                        }
                        if (GET_RANDOM_VALUE(0,10)==1) {
                            int dyOff = GET_RANDOM_VALUE(-1,1);
                            dy+=dyOff;
                            if (dy+z>WORLD_DEPTH-1 || dy+z<0) dy-=dyOff;
                        }
                        
                            
                    }
                    for (int kx = -5; kx <= 5; kx++) {
                        for (int kz = -5; kz <= 5; kz++) {
                            for (int ky = -5; ky <= 5; ky++) {
                                int worldX = x+dx+kx;
                                int worldY = height+treeHeight+ky;
                                int worldZ = z+dy+kz;
                                if (worldX<0 || worldY<0 || worldZ<0 || worldX>=WORLD_WIDTH || worldZ>=WORLD_DEPTH) continue; 
                                int cx = (worldX)/32;
                                int cy = (worldY)/32;
                                int cz = (worldZ)/32;
                                voxelChunks[cx][cy][cz].voxels[(worldX)%32*32*32+(worldY%32)*32+(worldZ)%32] = 4;
                                voxelChunks[cx][cy][cz].containsBlocks = true;
                                
                            }
                        }   
                    }
                    
                }
                else {
                   //if ((x+z+GetRandomValue(0,1))%2==0) {
                   //    for (int i = 0; i < GetRandomValue(2,5); i++) {
                   //        int worldX = x;
                   //        int worldY = height+i;
                   //        int worldZ = z;
                   //        if (worldX<0 || worldY<0 || worldZ<0 || worldX>=WORLD_WIDTH || worldZ>=WORLD_DEPTH) continue; 
                   //        int cx = (worldX)/32;
                   //        int cy = (worldY)/32;
                   //        int cz = (worldZ)/32;
                   //        voxelChunks[cx][cy][cz].voxels[(worldX)%32*32*32+(worldY%32)*32+(worldZ)%32] = 2;
                   //        voxelChunks[cx][cy][cz].containsBlocks = true;
                   //        
                   //    }
                   //}
                }
                for (int y = std::max(0, height-2); y <= height; y++) {
                    int cx = x/32;
                    int cy = y/32;
                    int cz = z/32;
                    
                    int id = (x%32)*32*32+(y%32)*32+z%32;
                    if (height==y) {
                        voxelChunks[cx][cy][cz].voxels[id] = GET_RANDOM_VALUE(1,2);
                        voxelChunks[cx][cy][cz].containsBlocks = true;
                    }
                    else {
                        voxelChunks[cx][cy][cz].containsBlocks = true;
                        voxelChunks[cx][cy][cz].voxels[id] = 1;
                    }
                }
            }
        }
        std::cout<<"Generated world shape\n";
        BuildDistanceToClosestVoxel();
        BuildDistanceLayer(16);
        BuildDistanceLayer(8);
        BuildDistanceLayer(4);

#pragma omp parallel for collapse(3)
        for (int x = 0; x < WORLD_WIDTH/32; x++) {
            for (int y= 0 ; y < WORLD_HEIGHT/32; y++) {
                for (int z = 0; z < WORLD_DEPTH/32; z++) {
                    if (!voxelChunks[x][y][z].containsBlocks) {
                        MemFree(voxelChunks[x][y][z].voxels);
                        voxelChunks[x][y][z].voxels = nullptr;
                    }
                    else {
                        traversalChunks[x][y][z].BuildOccupancyMask(voxelChunks[x][y][z].voxels);
                        voxelChunks[x][y][z].voxelLightValue = (uint8_t*)MemAlloc(32*32*32); 
                        for (int i = 0; i < 32*32*32; i++) {
                            voxelChunks[x][y][z].voxelLightValue[i] = 0;
                        }
                    }
                }
            }
        }
        
        UnloadImage(noise);
    }
};
Vector3 sunDirection = Vector3Normalize((Vector3){ 0.8f, 0.2f, 0.2f });

float sunDirSX = copysignf(1.0f, sunDirection.x);
float sunDirSY = copysignf(1.0f, sunDirection.y);
float sunDirSZ = copysignf(1.0f, sunDirection.z);

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
        camera.position = (Vector3){ WORLD_WIDTH/2, 128, WORLD_DEPTH/2 };
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
                                STEP(chunk.distance16[lx >> 4][ly >> 4][lz >> 4], 16.0f),
                                STEP(chunk.distance8[lx >> 3][ly >> 3][lz >> 3], 8.0f),
                                STEP(chunk.distance4[lx >> 2][ly >> 2][lz >> 2], 4.0f)
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
                                uint8_t type = world.voxelChunks[cx][cy][cz].voxels[index];
                                
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
                                    uint8_t lightVal = world.voxelChunks[(int)voX/32][(int)voY/32][(int)voZ/32].voxelLightValue[(int(voX) % 32) * 32 * 32 + (int(voY) % 32) * 32 + (int(voZ) % 32)];
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
                                                        ((voX & 31) << 10) |
                                                        ((voY & 31) << 5) |
                                                        (voZ & 31)
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
                                                int index = (lx << 10) | (ly << 5) | lz;

                                                if (world.voxelChunks[cx][cy][cz].voxels[index] != 0) {
                                                    strength = 0.8f;

                                                    world.voxelChunks[voX >> 5][voY >> 5][voZ >> 5]
                                                        .voxelLightValue[
                                                            ((voX & 31) << 10) |
                                                            ((voY & 31) << 5) |
                                                            (voZ & 31)
                                                        ] = 2;

                                                    break;
                                                }
                                            }

                                            float jump = std::max({
                                                STEP(chunk.distanceToClosestVoxel,  std::max(32, lod)),
                                                STEP(chunk.distance16[lx >> 4][ly >> 4][lz >> 4],  std::max(16, lod)),
                                                STEP(chunk.distance8 [lx >> 3][ly >> 3][lz >> 3],   std::max(8, lod)),
                                                STEP(chunk.distance4 [lx >> 2][ly >> 2][lz >> 2],   std::max(4, lod))
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
                                                else if (chunk.distance16[lx >> 4][ly >> 4][lz >> 4] != 0 && lod<=16)
                                                    cellSize = 16;
                                                else if (chunk.distance8[lx >> 3][ly >> 3][lz >> 3] != 0 && lod<=8)
                                                    cellSize = 8;
                                                else if (chunk.distance4[lx >> 2][ly >> 2][lz >> 2] != 0 && lod<=4)
                                                    cellSize = 4;

                                                int bx = ix & ~(cellSize - 1);
                                                int by = iy & ~(cellSize - 1);
                                                int bz = iz & ~(cellSize - 1);

                                                float tx = ((sunDirSX > 0.0f ? bx + cellSize : bx) - voxelX)
                                                        / sampleDir.x;

                                                float ty = ((sunDirSY > 0.0f ? by + cellSize : by) - voxelY)
                                                        / sampleDir.y;

                                                float tz = ((sunDirSZ > 0.0f ? bz + cellSize : bz) - voxelZ)
                                                        / sampleDir.z;

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
                            STEP(chunk.distance16[lx >> 4][ly >> 4][lz >> 4], std::max(16, lod)),
                            STEP(chunk.distance8 [lx >> 3][ly >> 3][lz >> 3], std::max(8,  lod)),
                            STEP(chunk.distance4 [lx >> 2][ly >> 2][lz >> 2], std::max(4,  lod))
                        });

                        if (jump > 0.0f) {
                            t+=jump;
                        }
                        else {
                            int cellSize = 1;
                            
                            if (chunk.distanceToClosestVoxel != 0) {
                                cellSize = 32;
                            }
                            else if (chunk.distance16[lx >> 4][ly >> 4][lz >> 4] != 0 && lod<=16) {
                                cellSize = 16;
                            }
                            else if (chunk.distance8[lx >> 3][ly >> 3][lz >> 3] != 0 && lod<=8) {
                                cellSize = 8;
                            }
                            else if (chunk.distance4[lx >> 2][ly >> 2][lz >> 2] != 0 && lod<=4) {
                                cellSize = 4;
                            }

                            int bx = ix & ~(cellSize - 1);
                            int by = iy & ~(cellSize - 1);
                            int bz = iz & ~(cellSize - 1);

                            float tx = ((sx > 0.0f ? bx + cellSize : bx) - voxelX) / direction.x;
                            float ty = ((sy > 0.0f ? by + cellSize : by) - voxelY) / direction.y;
                            float tz = ((sz > 0.0f ? bz + cellSize : bz) - voxelZ) / direction.z;

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
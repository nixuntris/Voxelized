#pragma once
#include "math.hpp"
#include <cinttypes>
#include "raymath.h"
#include <queue>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <set>
#include <unordered_set>
using Clock = std::chrono::steady_clock;
auto ms = [](auto start, auto end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
};
#define CELL_PTR(chunk, cellSize) \
    ((cellSize) == 8 ? &(chunk).distance8[0] : &(chunk).distance4[0])
enum VoxelTypes {
    AIR=0,
    GRASS=1,
    GRASS_VARIANT=2,
    TREE_BARK=3,
    LEAF=4,
    STONE = 5,
    SAND = 6,
    WATER = 7,
    CACTUS = 8,
    CACTUS_BUD = 9
};
struct VoxelData {
    std::string name;
    float lightAbsorbR;
    float lightAbsorbG;
    float lightAbsorbB;
    bool translucent;
    bool reflective;

};
const float shadowQuality = 1;
const VoxelData voxelMetaData[10] = {
    { "air",            1.000f, 1.000f, 1.000f, true,  false },

    { "grass",          0.970f, 0.985f, 0.965f, true,  false },

    { "grass_variant",  0.965f, 0.982f, 0.960f, true,  false },

    { "tree_bark",      0.120f, 0.090f, 0.060f, false, false },

    { "leaf",           0.930f, 0.975f, 0.920f, false,  false },

    { "stone",          0.180f, 0.190f, 0.210f, false, false },

    { "sand",           0.320f, 0.290f, 0.210f, false, false },

    { "water",          0.992f, 0.996f, 0.999f, true,  true },

    { "Cactus",      0.160f, 0.180f, 0.120f, false, false },
 
    { "Cactus_bud",  0.650f, 0.720f, 0.580f, false, false }
};
const float FOVY = 120.0f;
float SCALE = 1;
int width = 800/SCALE;
int height = 800/SCALE;
const float PIXEL_WORLD_SLOPE = 2.0f * tanf(FOVY * 0.5f * DEG2RAD) / 1000;
const float LOD2_START  = 2.0f  / PIXEL_WORLD_SLOPE;
const float LOD4_START  = 4.0f  / PIXEL_WORLD_SLOPE;
const float LOD8_START  = 8.0f  / PIXEL_WORLD_SLOPE;
const float LOD16_START = 16.0f / PIXEL_WORLD_SLOPE;
const float LOD32_START = 32.0f / PIXEL_WORLD_SLOPE;

int WORLD_WIDTH = 2048;
int WORLD_DEPTH = 2048;
const int WORLD_HEIGHT = 512;
const int RENDERDISTANCE = 8192;
enum WorldType {
    WORLD_PLAINS = 0,
    WORLD_MOUNTAINS,
    WORLD_DESERT,
    WORLD_ISLANDS
};
struct VoxelChunk {
    uint8_t *voxels;
    uint8_t *voxelLightValueR;
    uint8_t *voxelLightValueG;
    uint8_t *voxelLightValueB;
    bool containsLight=false;
    bool containsBlocks;
    int palletized = 0;
    uint8_t *remap;
    int filledOut = 0;
    int lod = -1;
    int size;
    int Generate(uint8_t* heightMap,uint8_t* noiseXY,uint8_t* noiseXZ,uint8_t* noiseYZ,int chunkX, int chunkY, int chunkZ,WorldType worldType = WORLD_PLAINS) {
        containsBlocks = false;

        auto EnsureStorage = [&]() {
            if (!containsBlocks) {
                voxels = (uint8_t*)MemAlloc(32 * 32 * 32);
                Clear();
                containsBlocks = true;
            }
        };
        int highestY = 0;
        for (int mx = 0; mx < 32; mx++) {
            for (int mz = 0; mz < 32; mz++) {
                const int x = chunkX + mx;
                const int z = chunkZ + mz;

                const float hNoise = heightMap[mz * 32 + mx] / 255.0f;
                int terrainHeight = 0;
                int waterLevel = 50;
                uint8_t surfaceBlock = GRASS;
                uint8_t subsurfaceBlock = STONE;
                int surfaceDepth = 1;
                float caveThreshold = 145.0f;

                switch (worldType) {
                    case WORLD_PLAINS:
                        terrainHeight = 70 + (int)(hNoise * 50.0f);
                        waterLevel = 72;
                        surfaceBlock = GRASS;
                        subsurfaceBlock = STONE;
                        surfaceDepth = 2;
                        caveThreshold = 150.0f;
                        break;

                    case WORLD_MOUNTAINS: {
                        const float mountain = hNoise * hNoise;
                        terrainHeight = 45 + (int)(mountain * 205.0f);
                        waterLevel = 55;
                        surfaceBlock = (terrainHeight > 175) ? STONE : GRASS_VARIANT;
                        subsurfaceBlock = STONE;
                        surfaceDepth = 2;
                        caveThreshold = 142.0f;
                        break;
                    }

                    case WORLD_DESERT:
                        terrainHeight = 68 + (int)(hNoise * 42.0f);
                        waterLevel = 45;
                        surfaceBlock = SAND;
                        subsurfaceBlock = SAND;
                        surfaceDepth = 5;
                        caveThreshold = 155.0f;
                        break;

                    case WORLD_ISLANDS: {
                        const float nx = (x / (float)(32  - 1)) * 2.0f - 1.0f;
                        const float nz = (z / (float)(32 - 1)) * 2.0f - 1.0f;
                        const float radial = sqrtf(nx * nx + nz * nz);
                        const float falloff = std::max(0.0f, 1.0f - radial);
                        terrainHeight = 38 + (int)(hNoise * 115.0f * falloff);
                        waterLevel = 62;
                        surfaceBlock = (terrainHeight <= waterLevel + 3) ? SAND : GRASS;
                        subsurfaceBlock = STONE;
                        surfaceDepth = 3;
                        caveThreshold = 148.0f;
                        break;
                    }
                }

                terrainHeight = std::max(1, std::min(terrainHeight, WORLD_HEIGHT - 1));
                highestY = std::max(highestY,terrainHeight);
                        
                const float xz = noiseXZ[mz * 32 + mx];

                for (int my = 0; my < 32; my++) {
                    const int y = chunkY + my;
                    const int id = IDX(mx, my, mz, 32);

                    if (y > terrainHeight) {
                        if (y <= waterLevel) {
                            EnsureStorage();
                            voxels[id] = WATER;
                        }
                        continue;
                    }

                    const float xy = noiseXY[my * 32 + mx];
                    const float yz = noiseYZ[my * 32 + mz];
                    const float density = (xy + xz + yz) / 3.0f;

                    const bool cave = (y > 4) && (density > caveThreshold);
                    if (cave)
                        continue;

                    EnsureStorage();

                    const int depth = terrainHeight - y;
                    if (depth == 0) {
                        voxels[id] = surfaceBlock;
                    } else if (depth < surfaceDepth) {
                        voxels[id] = subsurfaceBlock;
                    } else {
                        voxels[id] = STONE;
                    }
                }
            }
        }
        return highestY;
    }
    bool CheckOriginals(int lod) {
        palletized = 0;
        this->lod = lod;
        size = 32/lod;
        if (containsBlocks) {
            uint8_t tt = voxels[IDX(0,0,0,32)];
            int commons[256];
            for (int i = 0; i < 256; i++) commons[i] = 0;
            if (lod!=1) {
                //just pick at random
                uint8_t *lodVer = (uint8_t*)MemAlloc(size*size*size);
                for (int x = 0; x < size; x++) {
                    for (int y= 0 ; y < size; y++) {
                        for (int z = 0; z < size; z++) {
                            
                            int counts[256] = { 0 };

                            for (int dx = 0; dx < lod; dx++) {
                                for (int dy = 0; dy < lod; dy++) {
                                    for (int dz = 0; dz < lod; dz++) {
                                        uint8_t voxel =voxels[IDX(x * lod + dx,y * lod + dy,z * lod + dz,32)];
                                        counts[voxel]+=voxel!=0;
                                    }
                                }
                            }
                            uint8_t mostCommon = 0;
                            int highestCount = 0;

                            for (int voxel = 1; voxel < 256; voxel++) {
                                if (counts[voxel] > highestCount) {
                                    highestCount = counts[voxel];
                                    mostCommon = (uint8_t)voxel;
                                }
                            }

                            lodVer[IDX(x, y, z, size)] = mostCommon;
                        }
                    }
                }
                MemFree(voxels);
                voxels = lodVer;
            }
            return true;
        }
        return false;
    }
    void Clear() {
        containsBlocks = false;

        for (int x = 0; x < 32; x++) {
            for (int y= 0 ; y < 32; y++) {
                for (int z = 0; z < 32; z++) {
                    voxels[IDX(x,y,z,32)] = 0;
                }
            }
        }
    }
};
struct TraversalChunk {
    uint8_t distanceToClosestVoxel=0;
    uint64_t *occupancy = nullptr;
    // Shared after dedupe; nullptr means this LOD does not allocate the field.
    uint8_t *distance16 = nullptr; //size 2
    uint8_t *distance8 = nullptr; //size 4
    uint8_t *distance4 = nullptr; //size 8
    uint8_t buildID = 0;
    uint8_t only;
    uint8_t quantized = 0;
    uint8_t distance4Bits = 0;
    bool containsData = false;
    void Init(int cellSize) {
        
        distance16 = (uint8_t*)MemAlloc(2*2*2);
        for (int i = 0; i < 8; i++)  distance16[i] = 0;
        if (cellSize<16) {
            distance8 = (uint8_t*)MemAlloc(4*4*4);
            for (int i = 0; i < 64; i++) distance8[i] = 0;
        }
        if (cellSize<8) {
            distance4 = (uint8_t*)MemAlloc(8*8*8);
            for (int i = 0; i < 512; i++) distance4[i] = 0;
        }
            
    }
     void QuantizeDistance4(uint8_t smallest, uint8_t bits) {
        const int valueCount = 512;
        const int packedSize = (valueCount * bits + 7) / 8;

        uint8_t* packed = (uint8_t*)MemAlloc(packedSize);
        memset(packed, 0, packedSize);

        const uint8_t mask = (1u << bits) - 1u;

        for (int i = 0; i < valueCount; ++i) {
            uint8_t delta = distance4[i] - smallest;
            delta &= mask;

            const int bitIndex  = i * bits;
            const int byteIndex = bitIndex >> 3;
            const int bitOffset = bitIndex & 7;

            packed[byteIndex] |= delta << bitOffset;
        }

        MemFree(distance4);
        distance4 = packed;
        quantized = smallest;
        distance4Bits = bits;
    }
    bool CheckDelta(int cellSize) {
        
        int smallest = 255;
        int biggest = 0;
        bool only255= true;
        bool onl0 = true;

        if (cellSize < 8) {
            int smallest = 255;
            int biggest = 0;

            for (int i = 0; i < 512; i++) {
                if (distance4[i]!=255) only255 = false;
                if (distance4[i]!=0) onl0 = false;
                if (distance4[i] < smallest) smallest = distance4[i];
                if (distance4[i] > biggest) biggest = distance4[i];
            }
            only = 3;
            if (only255 ) {
                only = 255;
                free(distance4);
                distance4 = nullptr;
            }
            else if (onl0) {
                only = 0;
                free(distance4);
                distance4 = nullptr;
            }
            if (smallest != 255 && biggest != 0) {

                if (biggest - smallest < 2) {
                    QuantizeDistance4(smallest, 1);
                }
                else if (biggest - smallest < 4) {

                    QuantizeDistance4(smallest, 2);
                }
                else if (biggest - smallest < 16) {
                    QuantizeDistance4(smallest, 4);
                }
            }
        }

    return true;
}
    inline uint8_t GetDistance8(int index) const {
        if (distance8 == nullptr)
            return 0;   
        return distance8[index];
    }

    //for reference in the traversal optimizations rather than actual usage
    inline uint8_t GetDistance4(int index) const {
        if (distance4 == nullptr) {
            return only;
        }
        if (distance4Bits == 0) {
            return distance4[index];
        }

        const int bitIndex  = index * distance4Bits;
        const int byteIndex = bitIndex >> 3;
        const int bitOffset = bitIndex & 7;

        const uint8_t mask =
            (1u << distance4Bits) - 1u;

        const uint8_t delta =
            (distance4[byteIndex] >> bitOffset) & mask;

        return quantized + delta;
    }
    
    inline void BuildOccupancyMask(const uint8_t *voxels) {
        const int side = 32 / buildID;
        const int voxelCount = side * side * side;
        const int wordCount = (voxelCount + 63) / 64;

        occupancy = (uint64_t*)MemAlloc(wordCount * sizeof(uint64_t));
        std::fill(occupancy, occupancy + wordCount, 0ull);


        for (int word = 0; word < wordCount; ++word) {
            uint64_t bits = 0;
            const int base = word * 64;
            const int remaining = voxelCount - base;
            const int bitCount = remaining < 64 ? remaining : 64;

            for (int i = 0; i < bitCount; ++i) {
                bits |= uint64_t(voxels[base + i] != AIR) << i;
            }

            occupancy[word] = bits;
        }
    }
};
struct World {
    VoxelChunk voxelChunks[8192/32][WORLD_HEIGHT/32][8192/32];
    TraversalChunk traversalChunks[8192/32][WORLD_HEIGHT/32][8192/32];
    void Reset()
{
    const int CHUNK_COUNT_X = WORLD_WIDTH  / 32;
    const int CHUNK_COUNT_Y = WORLD_HEIGHT / 32;
    const int CHUNK_COUNT_Z = WORLD_DEPTH  / 32;

    std::unordered_set<void*> freedPointers;

    auto SafeFree = [&](void*& ptr)
    {
        if (ptr != nullptr)
        {
            if (freedPointers.insert(ptr).second)
                MemFree(ptr);

            ptr = nullptr;
        }
    };

    for (int x = 0; x < CHUNK_COUNT_X; ++x)
    {
        for (int y = 0; y < CHUNK_COUNT_Y; ++y)
        {
            for (int z = 0; z < CHUNK_COUNT_Z; ++z)
            {
                VoxelChunk& v = voxelChunks[x][y][z];
                TraversalChunk& t = traversalChunks[x][y][z];

                SafeFree(reinterpret_cast<void*&>(v.voxels));
                SafeFree(reinterpret_cast<void*&>(v.voxelLightValueR));
                SafeFree(reinterpret_cast<void*&>(v.voxelLightValueG));
                SafeFree(reinterpret_cast<void*&>(v.voxelLightValueB));
                SafeFree(reinterpret_cast<void*&>(v.remap));

                SafeFree(reinterpret_cast<void*&>(t.occupancy));
                SafeFree(reinterpret_cast<void*&>(t.distance16));
                SafeFree(reinterpret_cast<void*&>(t.distance8));
                SafeFree(reinterpret_cast<void*&>(t.distance4));

                v.containsLight  = false;
                v.containsBlocks = false;
                v.palletized     = 0;
                v.filledOut      = 0;
                v.lod            = -1;
                v.size           = 0;

                t.distanceToClosestVoxel = 0;
                t.buildID                = 0;
                t.only                   = 0;
                t.quantized              = 0;
                t.distance4Bits          = 0;
                t.containsData           = false;
            }
        }
    }
}
    inline uint8_t GetVoxel(int x, int y, int z) const {
            
        int cx = x / 32;
        int cy = y / 32;
        int cz = z / 32;
        int lx = x % 32;
        int ly = y % 32;
        int lz = z % 32;
        if (!voxelChunks[cx][cy][cz].containsBlocks) return 0;
        return voxelChunks[cx][cy][cz].voxels[IDX(lx,ly,lz,32)];
    }

    void BuildDistanceToClosestVoxel() {
        int CHUNK_COUNT_X = WORLD_WIDTH / 32;
        int CHUNK_COUNT_Y = WORLD_HEIGHT / 32;
        int CHUNK_COUNT_Z = WORLD_DEPTH / 32;

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
    void BuildDistanceLayerBaseline() { //You already know it will always be 16
        const int cellsPerChunk = 32 / 16;
        const int gridX = WORLD_WIDTH / 16;
        const int gridY = WORLD_HEIGHT / 16;
        const int gridZ = WORLD_DEPTH / 16;

#pragma omp parallel for collapse(3)
        for (int cx = 0; cx < WORLD_WIDTH / 32; ++cx) {
            for (int cy = 0; cy < WORLD_HEIGHT / 32; ++cy) {
                for (int cz = 0; cz < WORLD_DEPTH / 32; ++cz) {
                    VoxelChunk &voxelChunk = voxelChunks[cx][cy][cz];
                    TraversalChunk &traversalChunk = traversalChunks[cx][cy][cz];
                                        
                    if (traversalChunk.buildID > 16)
                        continue;
                    uint8_t *distance = &traversalChunk.distance16[0];
                    std::fill(distance, distance + cellsPerChunk * cellsPerChunk * cellsPerChunk, 255);
                    if (!voxelChunk.containsBlocks) continue;

                    for (int sx = 0; sx < cellsPerChunk; ++sx) {
                        for (int sy = 0; sy < cellsPerChunk; ++sy) {
                            for (int sz = 0; sz < cellsPerChunk; ++sz) {
                                bool occupied = false;
                                const int bx = sx * 16;
                                const int by = sy * 16;
                                const int bz = sz * 16;

                                for (int x = 0; x < 16 && !occupied; ++x)
                                    for (int y = 0; y < 16 && !occupied; ++y)
                                        for (int z = 0; z < 16; ++z)
                                        
                                            if (voxelChunk.voxels[IDX(bx+x,by+y,bz+z,32)]) {
                                                occupied = true;
                                                break;
                                            }

                                if (occupied)
                                    distance[IDX(sx,sy,sz,cellsPerChunk)] = 0;
                            }
                        }
                    }
                }
            }
        }
        for (int x = 0; x < gridX; ++x) {
            for (int y = 0; y < gridY; ++y) {
                for (int z = 0; z < gridZ; ++z) {
                            
                    int cx = x / cellsPerChunk;
                    int cy = y / cellsPerChunk;
                    int cz = z / cellsPerChunk;

                    TraversalChunk &traversalChunk =
                        traversalChunks[cx][cy][cz];

                    if (traversalChunk.buildID > 16)
                        continue;
                    
                    uint8_t &cur = traversalChunk.distance16[IDX(x%cellsPerChunk,y%cellsPerChunk,z%cellsPerChunk,cellsPerChunk)];
                    for (int dx = -1; dx <= 1; ++dx) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dz = -1; dz <= 1; ++dz) {
                                if (!(dx < 0 || (dx == 0 && dy < 0) || (dx == 0 && dy == 0 && dz < 0))) continue;
                                const int nx = x + dx, ny = y + dy, nz = z + dz;
                                if (nx < 0 || ny < 0 || nz < 0 || nx >= gridX || ny >= gridY || nz >= gridZ) continue;
                                const uint8_t n = traversalChunks[nx / cellsPerChunk][ny / cellsPerChunk][nz/cellsPerChunk].distance16[IDX(nx%cellsPerChunk,ny%cellsPerChunk,nz%cellsPerChunk,cellsPerChunk)];
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
                    
                    int cx = x / cellsPerChunk;
                    int cy = y / cellsPerChunk;
                    int cz = z / cellsPerChunk;

                    TraversalChunk &traversalChunk =
                        traversalChunks[cx][cy][cz];

                    if (traversalChunk.buildID > 16)
                        continue;
                    uint8_t &cur = traversalChunk.distance16[IDX(x%cellsPerChunk,y%cellsPerChunk,z%cellsPerChunk,cellsPerChunk)];
                    for (int dx = -1; dx <= 1; ++dx) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dz = -1; dz <= 1; ++dz) {
                                if (!(dx > 0 || (dx == 0 && dy > 0) || (dx == 0 && dy == 0 && dz > 0))) continue;
                                const int nx = x + dx, ny = y + dy, nz = z + dz;
                                if (nx < 0 || ny < 0 || nz < 0 || nx >= gridX || ny >= gridY || nz >= gridZ) continue;
                                const uint8_t n = traversalChunks[nx / cellsPerChunk][ny / cellsPerChunk][nz/cellsPerChunk].distance16[IDX(nx%cellsPerChunk,ny%cellsPerChunk,nz%cellsPerChunk,cellsPerChunk)];
                                if (n < 254) cur = std::min<uint8_t>(cur, static_cast<uint8_t>(n + 1));
                            }
                        }
                    }
                }
            }
        }
    }
    void BuildDistanceLayer(int cellSize) {
        const int cellsPerChunk = 32 / cellSize;
        const int gridX = WORLD_WIDTH / cellSize;
        const int gridY = WORLD_HEIGHT / cellSize;
        const int gridZ = WORLD_DEPTH / cellSize;
        const IVector3 ForwardOffsets[13] = {
            {-1, -1, -1}, {-1, -1,  0}, {-1, -1,  1},
            {-1,  0, -1}, {-1,  0,  0}, {-1,  0,  1},
            {-1,  1, -1}, {-1,  1,  0}, {-1,  1,  1},
            { 0, -1, -1}, { 0, -1,  0}, { 0, -1,  1},
            { 0,  0, -1}
        };

        const IVector3 BackwardOffsets[13] = {
            { 1,  1,  1}, { 1,  1,  0}, { 1,  1, -1},
            { 1,  0,  1}, { 1,  0,  0}, { 1,  0, -1},
            { 1, -1,  1}, { 1, -1,  0}, { 1, -1, -1},
            { 0,  1,  1}, { 0,  1,  0}, { 0,  1, -1},
            { 0,  0,  1}
        };
        
#pragma omp parallel for collapse(3)
        for (int cx = 0; cx < WORLD_WIDTH / 32; ++cx) {
            for (int cy = 0; cy < WORLD_HEIGHT / 32; ++cy) {
                for (int cz = 0; cz < WORLD_DEPTH / 32; ++cz) {
                    VoxelChunk &voxelChunk = voxelChunks[cx][cy][cz];
                    TraversalChunk &traversalChunk = traversalChunks[cx][cy][cz];
                                        
                    if (traversalChunk.buildID > cellSize)
                        continue;
                    
                    uint8_t *distance = CELL_PTR(traversalChunk,cellSize);
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
                                        
                                            if (voxelChunk.voxels[IDX(bx+x,by+y,bz+z,32)]) {
                                                occupied = true;
                                                break;
                                            }

                                if (occupied)
                                    distance[IDX(sx,sy,sz,cellsPerChunk)] = 0;
                            }
                        }
                    }
                }
            }
        }
        for (int x = 0; x < gridX; ++x) {
            for (int y = 0; y < gridY; ++y) {
                for (int z = 0; z < gridZ; ++z) {
                            
                    int cx = x / cellsPerChunk;
                    int cy = y / cellsPerChunk;
                    int cz = z / cellsPerChunk;

                    TraversalChunk &traversalChunk =
                        traversalChunks[cx][cy][cz];

                    if (traversalChunk.buildID > cellSize)
                        continue;
                    uint8_t &cur = CELL_PTR(traversalChunk,cellSize)[IDX(x%cellsPerChunk,y%cellsPerChunk,z%cellsPerChunk,cellsPerChunk)];
                    for (int o = 0; o < 13; o++) {
                        const int nx = x + ForwardOffsets[o].x;
                        const int ny = y + ForwardOffsets[o].y;
                        const int nz = z + ForwardOffsets[o].z;

                        if (nx < 0 || ny < 0 || nz < 0 || nx >= gridX || ny >= gridY || nz >= gridZ)
                            continue;
                            
                        if (traversalChunks[nx / cellsPerChunk][ny / cellsPerChunk][nz / cellsPerChunk].buildID > cellSize)
                            continue;
                        const uint8_t n =
                            CELL_PTR(
                                traversalChunks[nx / cellsPerChunk][ny / cellsPerChunk][nz / cellsPerChunk],
                                cellSize
                            )[IDX(
                                nx % cellsPerChunk,
                                ny % cellsPerChunk,
                                nz % cellsPerChunk,
                                cellsPerChunk
                            )];

                        if (n < 254)
                            cur = std::min<uint8_t>(cur, n + 1);
                    }
                }
            }
        }

        for (int x = gridX - 1; x >= 0; --x) {
            for (int y = gridY - 1; y >= 0; --y) {
                for (int z = gridZ - 1; z >= 0; --z) {
                    
                    int cx = x / cellsPerChunk;
                    int cy = y / cellsPerChunk;
                    int cz = z / cellsPerChunk;

                    TraversalChunk &traversalChunk =
                        traversalChunks[cx][cy][cz];

                    if (traversalChunk.buildID > cellSize)
                        continue;
                    uint8_t &cur = CELL_PTR(traversalChunk,cellSize)[IDX(x%cellsPerChunk,y%cellsPerChunk,z%cellsPerChunk,cellsPerChunk)];
                    for (int o = 0; o < 13; o++) {
                        const int nx = x + BackwardOffsets[o].x;
                        const int ny = y + BackwardOffsets[o].y;
                        const int nz = z + BackwardOffsets[o].z;

                        if (nx < 0 || ny < 0 || nz < 0 || nx >= gridX || ny >= gridY || nz >= gridZ)
                            continue;  
                        
                        if (traversalChunks[nx / cellsPerChunk][ny / cellsPerChunk][nz / cellsPerChunk].buildID > cellSize)
                            continue;
                        const uint8_t n =
                            CELL_PTR(
                                traversalChunks[nx / cellsPerChunk][ny / cellsPerChunk][nz / cellsPerChunk],
                                cellSize
                            )[IDX(
                                nx % cellsPerChunk,
                                ny % cellsPerChunk,
                                nz % cellsPerChunk,
                                cellsPerChunk
                            )];

                        if (n < 254) 
                            cur = std::min<uint8_t>(cur, n + 1);
                    }
                }
            }
        }
    }
    void GenerateTerrain(WorldType worldType=WORLD_MOUNTAINS) {
        

        std::cout<<"World gen beg\n";
#pragma omp parallel for collapse(2) 
        for (int x = 0; x < WORLD_WIDTH/32; x++) {
            for (int z = 0; z < WORLD_DEPTH/32; z++) {
                uint8_t* heightMap = GenImagePerlinNoiseOptimized(32,32,x*32,z*32,5.0f/10.0f);
                for (int y = 0; y < WORLD_HEIGHT/32; y++) {
                    uint8_t* noiseXZ = GenImagePerlinNoiseOptimized(32,32,300 + x*32,700 + z*32,6.0f/10.0f);
                    uint8_t* noiseXY = GenImagePerlinNoiseOptimized(32,32,x*32,y*32,6.0f/10.0f);
                    uint8_t* noiseYZ = GenImagePerlinNoiseOptimized(32,32,900 + z*32,1300 + y*32,6.0f/10.0f);

                    int dy = voxelChunks[x][y][z].Generate(heightMap,noiseXY,noiseXZ,noiseYZ,x*32,y*32,z*32,worldType);
                    free(noiseXZ);
                    free(noiseXY);
                    free(noiseYZ);
                    if (y*32>dy) break;
                }
                free(heightMap);
            }
        }

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
                            if (!voxelChunks[cx/32][cy/32][cz/32].containsBlocks) {

                                voxelChunks[cx/32][cy/32][cz/32].voxels = (uint8_t*)MemAlloc(32 * 32 * 32);
                                memset(voxelChunks[cx/32][cy/32][cz/32].voxels,0,32*32*32);
                                voxelChunks[cx/32][cy/32][cz/32].containsBlocks = true;
                            }
                            voxelChunks[cx/32][cy/32][cz/32].voxels[IDX(cx%32,cy%32,cz%32,32)] = type;
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

            VoxelChunk& chunk = voxelChunks[x / 32][y / 32][z / 32];

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
        for (int i = 0; i < ((WORLD_WIDTH/32)*(WORLD_DEPTH/32))*float(GET_RANDOM_VALUE(2,10))/10.0f; ++i) {
            int x =GET_RANDOM_VALUE(40, WORLD_WIDTH - 40);
            int z =GET_RANDOM_VALUE(40, WORLD_DEPTH - 40);
            bool findAIRG = false;
            bool findAIRS = false;
            for (int y = 0; y < 300; y++) {
                if (GetVoxel(x,y,z)==GRASS) {
                    findAIRG = true;
                }
                if (GetVoxel(x,y,z)==SAND) {
                    findAIRS = true;
                }
                if (GetVoxel(x,y,z)==0 && findAIRG) {
                    oakTree(x, y, z);
                    break;
                }
                if (GetVoxel(x,y,z)==0 && findAIRS) {
                    std::cout<<"a\n";
                    cactusPlant(x, y, z);
                    break;
                }
            }
        }
    }
    void GenerateOccupancyMasks() {
#pragma omp parallel for collapse(3) schedule(static)
        for (int x = 0; x < WORLD_WIDTH/32; x++) {
            for (int y= 0 ; y < WORLD_HEIGHT/32; y++) {
                for (int z = 0; z < WORLD_DEPTH/32; z++) {
                    if (voxelChunks[x][y][z].containsBlocks) {
                        traversalChunks[x][y][z].BuildOccupancyMask(voxelChunks[x][y][z].voxels);
                        int size = 32/traversalChunks[x][y][z].buildID;
                        size/=shadowQuality;
                        voxelChunks[x][y][z].voxelLightValueR = (uint8_t*)MemAlloc(size*size*size); 
                        voxelChunks[x][y][z].voxelLightValueG = (uint8_t*)MemAlloc(size*size*size); 
                        voxelChunks[x][y][z].voxelLightValueB = (uint8_t*)MemAlloc(size*size*size); 
                        for (int i = 0; i < size*size*size; i++) {
                            voxelChunks[x][y][z].voxelLightValueR[i] = 0;
                            voxelChunks[x][y][z].voxelLightValueG[i] = 0;
                            voxelChunks[x][y][z].voxelLightValueB[i] = 0;
                        }
                        voxelChunks[x][y][z].containsLight = true;
                    }
                }
            }
        }
    }
    void Init(Vector3 cameraPosition, WorldType worldType) {
        for (int x = 0; x < WORLD_WIDTH/32; x++) {
            for (int y= 0 ; y < WORLD_HEIGHT/32; y++) {
                for (int z = 0; z < WORLD_DEPTH/32; z++) {
                    float dist = Vector3Distance(cameraPosition,{(float)x*32,(float)y*32,(float)z*32});
                    int lod = 1;
                    if (dist>LOD2_START) lod = 2;
                    if (dist>LOD4_START) lod = 4;
                    if (dist>LOD8_START) lod = 8;
                    if (dist>LOD16_START) lod = 16;
                    traversalChunks[x][y][z].buildID = lod; 
                    //voxelChunks[x][y][z].Clear();
                    traversalChunks[x][y][z].Init(lod);
                    
                }
            }
        }
        auto terrainBeg = Clock::now();
        GenerateTerrain(worldType);
        std::cout<<"World gen end\n";
        auto terrainEnd = Clock::now();
        
        auto distanceLayersBeg = Clock::now();
        BuildDistanceToClosestVoxel();
        std::cout<<"Closest\n";
        BuildDistanceLayerBaseline();
        std::cout<<"Base\n";
        BuildDistanceLayer(8); //slow function
        std::cout<<"8\n";
        BuildDistanceLayer(4);
        std::cout<<"4\n";
        auto distanceLayersEnd = Clock::now();
        #pragma omp parallel for collapse(3)
        for (int x = 0; x < WORLD_WIDTH/32; x++) {
            for (int y= 0 ; y < WORLD_HEIGHT/32; y++) {
                for (int z = 0; z < WORLD_DEPTH/32; z++) {
                    voxelChunks[x][y][z].CheckOriginals(traversalChunks[x][y][z].buildID);
                    traversalChunks[x][y][z].CheckDelta(traversalChunks[x][y][z].buildID);
                }
            }
        }
        auto occupancyBeg = Clock::now();
        GenerateOccupancyMasks(); //EASILY THE SLOWEST AND LEAST SCALABLE FUNC
        auto occupancyOld = Clock::now();
        
        GetMemoryUsageBytes();
        std::cout<<"terrain gen: "<<ms(terrainBeg,terrainEnd)<<" distance fields: "<<ms(distanceLayersBeg,distanceLayersEnd)<<" occupancy: "<<ms(occupancyBeg,occupancyOld)<<"\n";
    }
    uint64_t GetMemoryUsageBytes() const {

        uint64_t total = sizeof(World);
        uint64_t worldStructBytes = sizeof(World);

        uint64_t voxelBytesTotal = 0;

        uint64_t lightRBytesTotal = 0;
        uint64_t lightGBytesTotal = 0;
        uint64_t lightBBytesTotal = 0;

        uint64_t occupancyBytesTotal = 0;

        uint64_t distance16BytesTotal = 0;
        uint64_t distance8BytesTotal  = 0;
        uint64_t distance4BytesTotal  = 0;

        uint64_t distance4RawBytes = 0;
        uint64_t distance4QuantizedBytes = 0;

        uint64_t remapBytesTotal = 0;

        uint64_t voxelAllocations = 0;
        uint64_t lightAllocations = 0;
        uint64_t occupancyAllocations = 0;
        uint64_t distance16Allocations = 0;
        uint64_t distance8Allocations = 0;
        uint64_t distance4RawAllocations = 0;
        uint64_t distance4QuantizedAllocations = 0;
        uint64_t remapAllocations = 0;

        std::unordered_set<const void*> counted;

        const auto addAllocation = [&](
            const void* ptr,
            uint64_t bytes,
            uint64_t& categoryBytes,
            uint64_t& allocationCount
        ) {
            if (ptr != nullptr && counted.insert(ptr).second) {
                total += bytes;
                categoryBytes += bytes;
                allocationCount++;
            }
        };

        int CHUNK_COUNT_X = WORLD_WIDTH  / 32;
        int CHUNK_COUNT_Y = WORLD_HEIGHT / 32;
        int CHUNK_COUNT_Z = WORLD_DEPTH  / 32;

        uint64_t CHUNK_COUNT = uint64_t(CHUNK_COUNT_X) * uint64_t(CHUNK_COUNT_Y) * uint64_t(CHUNK_COUNT_Z);

        for (int x = 0; x < CHUNK_COUNT_X; ++x) {
            for (int y = 0; y < CHUNK_COUNT_Y; ++y) {
                for (int z = 0; z < CHUNK_COUNT_Z; ++z) {

                    const VoxelChunk& v = voxelChunks[x][y][z];
                    const TraversalChunk& t = traversalChunks[x][y][z];

                     if (v.voxels != nullptr) {
                        const uint64_t bytes = uint64_t(v.size) * uint64_t(v.size) * uint64_t(v.size);

                        addAllocation(v.voxels,bytes,voxelBytesTotal,voxelAllocations);
                    }
                     if (v.containsBlocks && t.buildID != 0) {

                        uint64_t lightSide = 32u / t.buildID;
                        lightSide/=shadowQuality;
                        const uint64_t lightBytes =lightSide *lightSide *lightSide;

                        addAllocation(v.voxelLightValueR,lightBytes,lightRBytesTotal,lightAllocations);

                        addAllocation(v.voxelLightValueG,lightBytes,lightGBytesTotal,lightAllocations);

                        addAllocation(v.voxelLightValueB,lightBytes,lightBBytesTotal,lightAllocations);
                    }

                    addAllocation(t.occupancy,512ull * sizeof(uint64_t),occupancyBytesTotal,occupancyAllocations);
                    addAllocation(t.distance16,2ull * 2ull * 2ull,distance16BytesTotal,distance16Allocations);

                    addAllocation(t.distance8,4ull * 4ull * 4ull,distance8BytesTotal,distance8Allocations);
                    if (t.distance4 != nullptr) {

                        uint64_t distance4Bytes;

                        if (t.distance4Bits == 0) {

                            distance4Bytes = 512ull;

                            addAllocation(t.distance4,distance4Bytes,distance4RawBytes,distance4RawAllocations);

                        } else {

                            distance4Bytes = (    512ull *    uint64_t(t.distance4Bits) +    7ull) / 8ull;

                            addAllocation(t.distance4,distance4Bytes,distance4QuantizedBytes,distance4QuantizedAllocations);
                        }
                    }
                    addAllocation(v.remap,256ull,remapBytesTotal,remapAllocations);
                }
            }
        }

        distance4BytesTotal = distance4RawBytes + distance4QuantizedBytes;

        const uint64_t lightBytesTotal = lightRBytesTotal +lightGBytesTotal +lightBBytesTotal;

        const uint64_t distanceBytesTotal = distance16BytesTotal + distance8BytesTotal + distance4BytesTotal;

        auto mib = [](uint64_t bytes) {
            return double(bytes) / (1024.0 * 1024.0);
        };

        auto percent = [&](uint64_t bytes) {
            if (total == 0)
                return 0.0;

            return (double(bytes) / double(total)) * 100.0;
        };

        std::cout << "\n";
        std::cout << "================ MEMORY USAGE ================\n";

        std::cout << "Chunks: "<< CHUNK_COUNT<< " ("<< CHUNK_COUNT_X << " x "<< CHUNK_COUNT_Y << " x "<< CHUNK_COUNT_Z << ")\n\n";

        auto printMemory = [&](const char* name, uint64_t bytes) {
            std::cout<< name<< ": "<< bytes<< " bytes | "<< mib(bytes)<< " MiB | "<< percent(bytes)<< "%\n";
        };

        printMemory("sizeof(World)", worldStructBytes);

        std::cout << "\n--- VOXELS ---\n";

        printMemory("Voxel data", voxelBytesTotal);

        std::cout<< "Voxel allocations: "<< voxelAllocations<< "\n";

        std::cout << "\n--- LIGHTING ---\n";

        printMemory("Light R", lightRBytesTotal);
        printMemory("Light G", lightGBytesTotal);
        printMemory("Light B", lightBBytesTotal);
        printMemory("Light TOTAL", lightBytesTotal);

        std::cout<< "Light allocations: "<< lightAllocations<< "\n";

        std::cout << "\n--- TRAVERSAL ---\n";

        printMemory("Occupancy", occupancyBytesTotal);

        std::cout<< "Occupancy allocations: "<< occupancyAllocations<< "\n";

        printMemory("Distance16", distance16BytesTotal);

        std::cout<< "Distance16 allocations: "<< distance16Allocations<< "\n";

        printMemory("Distance8", distance8BytesTotal);

        std::cout<< "Distance8 allocations: "<< distance8Allocations<< "\n";

        printMemory("Distance4 RAW", distance4RawBytes);

        std::cout<< "Distance4 raw allocations: "<< distance4RawAllocations<< "\n";

        printMemory("Distance4 QUANTIZED", distance4QuantizedBytes);

        std::cout<< "Distance4 quantized allocations: "<< distance4QuantizedAllocations<< "\n";

        printMemory("Distance4 TOTAL",distance4BytesTotal);

        printMemory("All distance fields",distanceBytesTotal);

        std::cout << "\n--- REMAP ---\n";

        printMemory("Remap", remapBytesTotal);

        std::cout<< "Remap allocations: "<< remapAllocations<< "\n";

        std::cout << "\n--- TOTAL ---\n";

        printMemory("TOTAL MEMORY", total);

        std::cout << "Average per chunk: " << double(total) / double(CHUNK_COUNT) << " bytes | " << mib(total) / double(CHUNK_COUNT) << " MiB\n";

        std::cout << "==============================================\n\n";

        return total;
    }


};
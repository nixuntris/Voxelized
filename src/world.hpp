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
const bool dedupe = false;
using Clock = std::chrono::steady_clock;
auto ms = [](auto start, auto end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
};
#define READ_VOXEL(chunk, index)                                             \
    (!(chunk).containsBlocks ? AIR :                                         \
     !(chunk).chunkedPallete ? (chunk).voxels[(index)] :                     \
     (chunk).remap[                                                          \
         ((chunk).voxels[(index) >> 1] >> (((index) & 1) * 4)) & 0x0F        \
     ])
#define GET_DISTANCE8(chunk, index) \
    ((chunk).distance8 == nullptr ? 0 : (chunk).distance8[(index)])

#define GET_DISTANCE4(chunk, index) \
    ((chunk).distance4 == nullptr ? (chunk).only : \
     (chunk).distance4Bits == 0 ? (chunk).distance4[(index)] : \
     (uint8_t)((chunk).quantized + \
        (((chunk).distance4[((index) * (chunk).distance4Bits) >> 3] >> \
          (((index) * (chunk).distance4Bits) & 7)) & \
         ((1u << (chunk).distance4Bits) - 1u))))
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
const float FOVY = 60.0f;
float SCALE = 1;
int width = 800/SCALE;
int height = 800/SCALE;
const float PIXEL_WORLD_SLOPE = 2.0f * tanf(FOVY * 0.5f * DEG2RAD) / 1000;
const float LOD2_START  = 2.0f  / PIXEL_WORLD_SLOPE;
const float LOD4_START  = 4.0f  / PIXEL_WORLD_SLOPE;
const float LOD8_START  = 8.0f  / PIXEL_WORLD_SLOPE;
const float LOD16_START = 16.0f / PIXEL_WORLD_SLOPE;
const float LOD32_START = 32.0f / PIXEL_WORLD_SLOPE;

const int WORLD_WIDTH = 8192*4;
const int WORLD_DEPTH = 8192*4;
const int WORLD_HEIGHT = 512;
const int RENDERDISTANCE = 2048;
enum WorldType {
    WORLD_PLAINS = 0,
    WORLD_MOUNTAINS,
    WORLD_DESERT,
    WORLD_ISLANDS,
    CLOUD
};
int generatedChunks = 0;
struct VoxelChunk {
    bool generated = false;
    uint8_t *voxels = nullptr;
    uint8_t *voxelLightValueR = nullptr;
    uint8_t *voxelLightValueG = nullptr;
    uint8_t *voxelLightValueB = nullptr;
    bool containsLight=false;
    bool containsBlocks = false;
    int palletized = 0;
    uint8_t *remap = nullptr;
    int filledOut = 0;
    int lod = -1;
    bool chunkedPallete = false;
    int size = 0;
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
        generated++;
        palletized = 0;
        this->lod = lod;
        size = 32/lod;
        generated = true;
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
            const int voxelCount = size * size * size;

            bool types[256] = { false };

            for (int i = 0; i < voxelCount; i++) {
                if (voxels[i] != 0) types[voxels[i]] = true;
            }
            int voxelTypes = 0;
            for (int i = 1; i < 256; i++)  
                voxelTypes+=types[i];

            if (voxelTypes <= 15) {
                chunkedPallete = true;
                const int packedSize = (voxelCount + 1) / 2;
                uint8_t* palletVoxels = (uint8_t*)MemAlloc(packedSize);
                memset(palletVoxels, 0, packedSize);
                remap = (uint8_t*)MemAlloc(16);
                memset(remap, 0, 16);
                uint8_t toPalette[256] = { 0 };
                remap[0] = 0;
                uint8_t paletteIndex = 1;

                for (int voxel = 1; voxel < 256; voxel++) {
                    if (types[voxel]) {
                        toPalette[voxel] = paletteIndex;
                        remap[paletteIndex] = (uint8_t)voxel;
                        paletteIndex++;
                    }
                }
                for (int i = 0; i < voxelCount; i++) {
                    uint8_t paletteVoxel = toPalette[voxels[i]];

                    if ((i & 1) == 0) {
                        palletVoxels[i >> 1] |= paletteVoxel;
                    } else {
                        palletVoxels[i >> 1] |= (uint8_t)(paletteVoxel << 4);
                    }
                }
                MemFree(voxels);
                voxels = palletVoxels;

            }


            return true;
        }
        return false;
    }
    uint8_t inline ReadVoxel(int index) const {

        if (!chunkedPallete)
            return voxels[index];

        uint8_t packed = voxels[index >> 1];

        uint8_t paletteIndex;

        if ((index & 1) == 0)
            paletteIndex = packed & 0x0F;
        else
            paletteIndex = (packed >> 4) & 0x0F;

        return remap[paletteIndex];
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
        if (containsData) return;

        buildID = cellSize;
        distanceToClosestVoxel = 255;
        only = 0;
        quantized = 0;
        distance4Bits = 0;

        distance16 = (uint8_t*)MemAlloc(2*2*2);
        for (int i = 0; i < 8; i++) distance16[i] = 255;

        if (cellSize < 16) {
            distance8 = (uint8_t*)MemAlloc(4*4*4);
            for (int i = 0; i < 64; i++) distance8[i] = 255;
        }

        if (cellSize < 8) {
            distance4 = (uint8_t*)MemAlloc(8*8*8);
            for (int i = 0; i < 512; i++) distance4[i] = 255;
        }

        containsData = true;
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
                MemFree(distance4);
                distance4 = nullptr;
            }
            else if (onl0) {
                only = 0;
                MemFree(distance4);
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
    
    inline void BuildOccupancyMask(const VoxelChunk& voxelChunk) {
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
                bits |= uint64_t(voxelChunk.ReadVoxel(base + i) != AIR) << i;
            }

            occupancy[word] = bits;
        }
    }
};
struct World {
    VoxelChunk voxelChunks[WORLD_WIDTH/32][WORLD_HEIGHT/32][WORLD_DEPTH/32];
    TraversalChunk traversalChunks[WORLD_WIDTH/32][WORLD_HEIGHT/32][WORLD_DEPTH/32];
    WorldType chunkBiome[WORLD_WIDTH/32][WORLD_DEPTH/32];

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

                v.generated      = false;
                v.containsLight  = false;
                v.containsBlocks = false;
                v.palletized     = 0;
                v.filledOut      = 0;
                v.lod            = -1;
                v.size           = 0;
                v.chunkedPallete = false;

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

    void BuildDistanceToClosestVoxel(int x, int z) {
        int CHUNK_COUNT_X = WORLD_WIDTH / 32;
        int CHUNK_COUNT_Y = WORLD_HEIGHT / 32;
        int CHUNK_COUNT_Z = WORLD_DEPTH / 32;

        struct ChunkPos { int x, y, z; };
        std::queue<ChunkPos> q;

        for (int y = 0; y < CHUNK_COUNT_Y; ++y) {
            VoxelChunk &voxelChunk = voxelChunks[x][y][z];
            TraversalChunk &traversalChunk = traversalChunks[x][y][z];
            if (voxelChunk.containsBlocks) {
                traversalChunk.distanceToClosestVoxel = 0;
                q.push({x, y, z});
            } else {
                traversalChunk.distanceToClosestVoxel = 255;
            }
        }

        for (int y = 0; y < CHUNK_COUNT_Y; ++y) {
            TraversalChunk &current = traversalChunks[x][y][z];
            if (current.distanceToClosestVoxel == 0) continue;

            int best = current.distanceToClosestVoxel;
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        if (dx == 0 && dz == 0) continue;
                        const int nx = x + dx;
                        const int ny = y + dy;
                        const int nz = z + dz;
                        if (nx < 0 || ny < 0 || nz < 0 ||
                            nx >= CHUNK_COUNT_X || ny >= CHUNK_COUNT_Y || nz >= CHUNK_COUNT_Z) continue;

                        const TraversalChunk &neighbor = traversalChunks[nx][ny][nz];
                        if (!neighbor.containsData || neighbor.distanceToClosestVoxel == 255) continue;
                        best = std::min(best, (int)neighbor.distanceToClosestVoxel + 1);
                    }
                }
            }

            if (best < current.distanceToClosestVoxel) {
                current.distanceToClosestVoxel = (uint8_t)best;
                q.push({x, y, z});
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
                        if (!neighbor.containsData) continue;
                        if (nextDistance < neighbor.distanceToClosestVoxel) {
                            neighbor.distanceToClosestVoxel = nextDistance;
                            q.push({nx, ny, nz});
                        }
                    }
                }
            }
        }
    }
    void BuildDistanceLayerBaseline(int x, int z) {
        constexpr int cellSize = 16;
        constexpr int cellsPerChunk = 2;
        const int gridX = WORLD_WIDTH / cellSize;
        const int gridY = WORLD_HEIGHT / cellSize;
        const int gridZ = WORLD_DEPTH / cellSize;
        const int chunkCountY = WORLD_HEIGHT / 32;

        struct CellPos { int x, y, z; };
        std::queue<CellPos> q;

        for (int cy = 0; cy < chunkCountY; ++cy) {
            VoxelChunk &voxelChunk = voxelChunks[x][cy][z];
            TraversalChunk &traversalChunk = traversalChunks[x][cy][z];

            if (!traversalChunk.containsData || traversalChunk.buildID > cellSize || !voxelChunk.containsBlocks)
                continue;

            uint8_t *distance = traversalChunk.distance16;

            for (int sx = 0; sx < cellsPerChunk; ++sx) {
                for (int sy = 0; sy < cellsPerChunk; ++sy) {
                    for (int sz = 0; sz < cellsPerChunk; ++sz) {
                        bool occupied = false;
                        const int bx = sx * cellSize;
                        const int by = sy * cellSize;
                        const int bz = sz * cellSize;

                        for (int vx = 0; vx < cellSize && !occupied; ++vx) {
                            for (int vy = 0; vy < cellSize && !occupied; ++vy) {
                                for (int vz = 0; vz < cellSize; ++vz) {
                                    if (voxelChunk.voxels[IDX(bx + vx, by + vy, bz + vz, 32)] != AIR) {
                                        occupied = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if (!occupied)
                            continue;

                        const int localIndex = IDX(sx, sy, sz, cellsPerChunk);
                        if (distance[localIndex] != 0) {
                            distance[localIndex] = 0;
                            q.push({
                                x * cellsPerChunk + sx,
                                cy * cellsPerChunk + sy,
                                z * cellsPerChunk + sz
                            });
                        }
                    }
                }
            }
        }

        for (int cy = 0; cy < chunkCountY; ++cy) {
            TraversalChunk &traversalChunk = traversalChunks[x][cy][z];
            if (!traversalChunk.containsData || traversalChunk.buildID > cellSize) continue;

            for (int sx = 0; sx < cellsPerChunk; ++sx) {
                for (int sy = 0; sy < cellsPerChunk; ++sy) {
                    for (int sz = 0; sz < cellsPerChunk; ++sz) {
                        uint8_t &value = traversalChunk.distance16[IDX(sx, sy, sz, cellsPerChunk)];
                        if (value == 0) continue;

                        const int gx = x * cellsPerChunk + sx;
                        const int gy = cy * cellsPerChunk + sy;
                        const int gz = z * cellsPerChunk + sz;
                        int best = value;

                        for (int dx = -1; dx <= 1; ++dx) {
                            for (int dy = -1; dy <= 1; ++dy) {
                                for (int dz = -1; dz <= 1; ++dz) {
                                    if (dx == 0 && dy == 0 && dz == 0) continue;
                                    const int nx = gx + dx;
                                    const int ny = gy + dy;
                                    const int nz = gz + dz;
                                    if (nx < 0 || ny < 0 || nz < 0 || nx >= gridX || ny >= gridY || nz >= gridZ) continue;

                                    const TraversalChunk &neighborChunk = traversalChunks[nx / cellsPerChunk][ny / cellsPerChunk][nz / cellsPerChunk];
                                    if (!neighborChunk.containsData || neighborChunk.buildID > cellSize) continue;
                                    const uint8_t neighbor = neighborChunk.distance16[IDX(nx % cellsPerChunk, ny % cellsPerChunk, nz % cellsPerChunk, cellsPerChunk)];
                                    if (neighbor == 255) continue;
                                    best = std::min(best, (int)neighbor + 1);
                                }
                            }
                        }

                        if (best < value) {
                            value = (uint8_t)best;
                            q.push({gx, gy, gz});
                        }
                    }
                }
            }
        }

        while (!q.empty()) {
            const CellPos p = q.front();
            q.pop();

            TraversalChunk &currentChunk =
                traversalChunks[p.x / cellsPerChunk][p.y / cellsPerChunk][p.z / cellsPerChunk];
            const uint8_t current = currentChunk.distance16[
                IDX(p.x % cellsPerChunk, p.y % cellsPerChunk, p.z % cellsPerChunk, cellsPerChunk)
            ];

            if (current == 254)
                continue;

            const uint8_t nextDistance = static_cast<uint8_t>(current + 1);

            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        if (dx == 0 && dy == 0 && dz == 0)
                            continue;

                        const int nx = p.x + dx;
                        const int ny = p.y + dy;
                        const int nz = p.z + dz;

                        if (nx < 0 || ny < 0 || nz < 0 ||
                            nx >= gridX || ny >= gridY || nz >= gridZ)
                            continue;

                        TraversalChunk &neighborChunk =
                            traversalChunks[nx / cellsPerChunk][ny / cellsPerChunk][nz / cellsPerChunk];

                        if (!neighborChunk.containsData || neighborChunk.buildID > cellSize)
                            continue;

                        uint8_t &neighbor = neighborChunk.distance16[
                            IDX(nx % cellsPerChunk, ny % cellsPerChunk, nz % cellsPerChunk, cellsPerChunk)
                        ];

                        if (nextDistance < neighbor) {
                            neighbor = nextDistance;
                            q.push({nx, ny, nz});
                        }
                    }
                }
            }
        }
    }

    void BuildDistanceLayer(int x, int z, int cellSize) {
        const int cellsPerChunk = 32 / cellSize;
        const int gridX = WORLD_WIDTH / cellSize;
        const int gridY = WORLD_HEIGHT / cellSize;
        const int gridZ = WORLD_DEPTH / cellSize;
        const int chunkCountY = WORLD_HEIGHT / 32;

        struct CellPos { int x, y, z; };
        std::queue<CellPos> q;

        for (int cy = 0; cy < chunkCountY; ++cy) {
            VoxelChunk &voxelChunk = voxelChunks[x][cy][z];
            TraversalChunk &traversalChunk = traversalChunks[x][cy][z];

            if (!traversalChunk.containsData || traversalChunk.buildID > cellSize || !voxelChunk.containsBlocks)
                continue;

            uint8_t *distance = CELL_PTR(traversalChunk, cellSize);

            for (int sx = 0; sx < cellsPerChunk; ++sx) {
                for (int sy = 0; sy < cellsPerChunk; ++sy) {
                    for (int sz = 0; sz < cellsPerChunk; ++sz) {
                        bool occupied = false;
                        const int bx = sx * cellSize;
                        const int by = sy * cellSize;
                        const int bz = sz * cellSize;

                        for (int vx = 0; vx < cellSize && !occupied; ++vx) {
                            for (int vy = 0; vy < cellSize && !occupied; ++vy) {
                                for (int vz = 0; vz < cellSize; ++vz) {
                                    if (voxelChunk.voxels[IDX(bx + vx, by + vy, bz + vz, 32)] != AIR) {
                                        occupied = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if (!occupied)
                            continue;

                        const int localIndex = IDX(sx, sy, sz, cellsPerChunk);
                        if (distance[localIndex] != 0) {
                            distance[localIndex] = 0;
                            q.push({
                                x * cellsPerChunk + sx,
                                cy * cellsPerChunk + sy,
                                z * cellsPerChunk + sz
                            });
                        }
                    }
                }
            }
        }

        for (int cy = 0; cy < chunkCountY; ++cy) {
            TraversalChunk &traversalChunk = traversalChunks[x][cy][z];
            if (!traversalChunk.containsData || traversalChunk.buildID > cellSize) continue;
            uint8_t *distance = CELL_PTR(traversalChunk, cellSize);

            for (int sx = 0; sx < cellsPerChunk; ++sx) {
                for (int sy = 0; sy < cellsPerChunk; ++sy) {
                    for (int sz = 0; sz < cellsPerChunk; ++sz) {
                        uint8_t &value = distance[IDX(sx, sy, sz, cellsPerChunk)];
                        if (value == 0) continue;

                        const int gx = x * cellsPerChunk + sx;
                        const int gy = cy * cellsPerChunk + sy;
                        const int gz = z * cellsPerChunk + sz;
                        int best = value;

                        for (int dx = -1; dx <= 1; ++dx) {
                            for (int dy = -1; dy <= 1; ++dy) {
                                for (int dz = -1; dz <= 1; ++dz) {
                                    if (dx == 0 && dy == 0 && dz == 0) continue;
                                    const int nx = gx + dx;
                                    const int ny = gy + dy;
                                    const int nz = gz + dz;
                                    if (nx < 0 || ny < 0 || nz < 0 || nx >= gridX || ny >= gridY || nz >= gridZ) continue;

                                    const TraversalChunk &neighborChunk = traversalChunks[nx / cellsPerChunk][ny / cellsPerChunk][nz / cellsPerChunk];
                                    if (!neighborChunk.containsData || neighborChunk.buildID > cellSize) continue;
                                    const uint8_t *neighborDistance = CELL_PTR(neighborChunk, cellSize);
                                    const uint8_t neighbor = neighborDistance[IDX(nx % cellsPerChunk, ny % cellsPerChunk, nz % cellsPerChunk, cellsPerChunk)];
                                    if (neighbor == 255) continue;
                                    best = std::min(best, (int)neighbor + 1);
                                }
                            }
                        }

                        if (best < value) {
                            value = (uint8_t)best;
                            q.push({gx, gy, gz});
                        }
                    }
                }
            }
        }

        while (!q.empty()) {
            const CellPos p = q.front();
            q.pop();

            TraversalChunk &currentChunk =
                traversalChunks[p.x / cellsPerChunk][p.y / cellsPerChunk][p.z / cellsPerChunk];
            uint8_t *currentDistance = CELL_PTR(currentChunk, cellSize);
            const uint8_t current = currentDistance[
                IDX(p.x % cellsPerChunk, p.y % cellsPerChunk, p.z % cellsPerChunk, cellsPerChunk)
            ];

            if (current == 254)
                continue;

            const uint8_t nextDistance = static_cast<uint8_t>(current + 1);

            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        if (dx == 0 && dy == 0 && dz == 0)
                            continue;

                        const int nx = p.x + dx;
                        const int ny = p.y + dy;
                        const int nz = p.z + dz;

                        if (nx < 0 || ny < 0 || nz < 0 ||
                            nx >= gridX || ny >= gridY || nz >= gridZ)
                            continue;

                        TraversalChunk &neighborChunk =
                            traversalChunks[nx / cellsPerChunk][ny / cellsPerChunk][nz / cellsPerChunk];

                        if (!neighborChunk.containsData || neighborChunk.buildID > cellSize)
                            continue;

                        uint8_t *neighborDistance = CELL_PTR(neighborChunk, cellSize);
                        uint8_t &neighbor = neighborDistance[
                            IDX(nx % cellsPerChunk, ny % cellsPerChunk, nz % cellsPerChunk, cellsPerChunk)
                        ];

                        if (nextDistance < neighbor) {
                            neighbor = nextDistance;
                            q.push({nx, ny, nz});
                        }
                    }
                }
            }
        }
    }
    void GenerateTerrain(WorldType worldType=WORLD_MOUNTAINS, int x=0, int z=0) {
        

        std::cout<<"World gen beg\n";
        uint8_t* heightMap = GenImagePerlinNoiseOptimized(32,32,x*32,z*32,5.0f/10.0f);
        #pragma omp parallel for 
        for (int y = 0; y < WORLD_HEIGHT/32; y++) {
            uint8_t* noiseXZ = GenImagePerlinNoiseOptimized(32,32,300 + x*32,700 + z*32,6.0f/10.0f);
            uint8_t* noiseXY = GenImagePerlinNoiseOptimized(32,32,x*32,y*32,6.0f/10.0f);
            uint8_t* noiseYZ = GenImagePerlinNoiseOptimized(32,32,900 + z*32,1300 + y*32,6.0f/10.0f);

            int dy = voxelChunks[x][y][z].Generate(heightMap,noiseXY,noiseXZ,noiseYZ,x*32,y*32,z*32,worldType);
            free(noiseXZ);
            free(noiseXY);
            free(noiseYZ);
            
            
        }
        free(heightMap);
        
    }
    void GenerateOccupancyMasks(int x, int z) {
#pragma omp parallel for 
        for (int y= 0 ; y < WORLD_HEIGHT/32; y++) {
            if (voxelChunks[x][y][z].containsBlocks) {
                traversalChunks[x][y][z].BuildOccupancyMask(voxelChunks[x][y][z]);
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
    void InitColumn(Vector3 cameraPosition, int x, int z) {
        for (int y = 0; y < WORLD_HEIGHT/32; y++) {
            float dist = Vector3Distance(
                cameraPosition,
                {(float)x * 32.0f + 16.0f, (float)y * 32.0f + 16.0f, (float)z * 32.0f + 16.0f}
            );

            int lod = 1;
            if (dist > LOD2_START) lod = 2;
            if (dist > LOD4_START) lod = 4;
            if (dist > LOD8_START) lod = 8;
            if (dist > LOD16_START) lod = 16;

            traversalChunks[x][y][z].Init(lod);
        }
    }

    void Init() {
        const int chunksX = WORLD_WIDTH / 32;
        const int chunksZ = WORLD_DEPTH / 32;
        float scale = 8;
        uint8_t* heatMap = GenImagePerlinNoiseOptimized(
            chunksX,
            chunksZ,
            5000,
            12000,
            0.025f*scale
        );

        uint8_t* elevationMap = GenImagePerlinNoiseOptimized(
            chunksX,
            chunksZ,
            18000,
            3000,
            0.025f*scale
        );

        for (int x = 0; x < chunksX; x++) {
            for (int z = 0; z < chunksZ; z++) {

                float heat =
                    heatMap[z * chunksX + x] / 255.0f;

                float elevation =
                    elevationMap[z * chunksX + x] / 255.0f;

                if (elevation < 0.35f) {
                    chunkBiome[x][z] = WORLD_ISLANDS;
                }
                else if (elevation > 0.68f) {
                    chunkBiome[x][z] = WORLD_MOUNTAINS;
                }
                else if (heat > 0.62f) {
                    chunkBiome[x][z] = WORLD_DESERT;
                }
                else {
                    chunkBiome[x][z] = WORLD_PLAINS;
                }
            }
        }

        free(heatMap);
        free(elevationMap);
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
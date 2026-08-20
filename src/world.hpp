#pragma once
#include "math.hpp"
#include <cinttypes>
#include "raymath.h"
#include <queue>
const float FOVY = 120.0f;
const float SCALE = 1;
const int width = 1000/SCALE;
const int height = 1000/SCALE;
const float PIXEL_WORLD_SLOPE = 2.0f * tanf(FOVY * 0.5f * DEG2RAD) / width;
const float LOD2_START  = 2.0f  / PIXEL_WORLD_SLOPE;
const float LOD4_START  = 4.0f  / PIXEL_WORLD_SLOPE;
const float LOD8_START  = 8.0f  / PIXEL_WORLD_SLOPE;
const float LOD16_START = 16.0f / PIXEL_WORLD_SLOPE;

const int WORLD_WIDTH = 2048;
const int WORLD_DEPTH = 2048;
const int WORLD_HEIGHT = 512;
const int RENDERDISTANCE = 2048;
struct VoxelChunk {
    uint8_t *voxels;
    uint8_t *voxelLightValue;
    bool containsBlocks;
    int palletized = 0;
    uint8_t *remap;
    int filledOut = 0;
    int lod;
    int size;
    bool CheckOriginals(int lod) {
        palletized = 0;
        this->lod = lod;
        size = 32/lod;
        if (containsBlocks) {
            uint8_t tt = voxels[IDX(0,0,0,32)];
            int commons[256];
            for (int i = 0; i < 256; i++) commons[i] = 0;
            if (lod==32) {
                //count the most common block
                    
                for (int x = 0; x < 32; x++) {
                    for (int y= 0 ; y < 32; y++) {
                        for (int z = 0; z < 32; z++) {
                            commons[voxels[IDX(x,y,z,32)]]++;
                        }
                    }
                }
                int mostCommon = 1;
                for (int i = 1; i < 256; i++) {
                    if (commons[i]>commons[mostCommon]) {
                        mostCommon = i;
                    }
                }
                free(voxels);
                filledOut = mostCommon;
            }
            else if (lod!=1) {
                //just pick at random
                uint8_t *lodVer = (uint8_t*)MemAlloc(size*size*size);
                for (int x = 0; x < size; x++) {
                    for (int y= 0 ; y < size; y++) {
                        for (int z = 0; z < size; z++) {
                            bool continueForThisChunk = true;
                            for (int dx = 0; dx < lod && continueForThisChunk; dx++) {
                                for (int dy = 0; dy < lod && continueForThisChunk; dy++) {
                                    for (int dz = 0; dz < lod && continueForThisChunk; dz++) {
                                        if (voxels[IDX(x*lod+dx,y*lod+dy,z*lod+dz,32)]!=0) {
                                            lodVer[IDX(x,y,z,size)] = voxels[IDX(x*lod+dx,y*lod+dy,z*lod+dz,32)];
                                            continueForThisChunk = false;
                                            break;
                                        }       
                                    }
                                }   
                            }
                        }
                    }
                }
                free(voxels);
                voxels = lodVer;
            }
            
            for (int x = 0; x < 32; x++) {
                for (int y= 0 ; y < 32; y++) {
                    for (int z = 0; z < 32; z++) {
                        if (tt!=voxels[IDX(x,y,z,32)]&& voxels[IDX(x,y,z,32)]!=0) {
                            return false;
                        }
                    }
                }
            }
            free(voxels);
            palletized = tt;
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
    uint64_t *occupancy;
    //this adds a lot of memory, for empty chunks or far away chunks they shouldn't even be allocated or calculated so they should be switched to being heap based instead 
    //And it probably makes the memory access slower causing slower traversal
    uint8_t *distance16; //size 2
    uint8_t *distance8; //size 4
    uint8_t *distance4; //size 8
    uint8_t buildID = 0;
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
    void BuildOccupancyMask(uint8_t *voxels) {
        occupancy = (uint64_t*)MemAlloc(512*sizeof(uint64_t));
        for (int i = 0; i < 512; i++) occupancy[i] = 0;
        
        for (int x = 0; x < 32; ++x) {
            for (int y = 0; y < 32; ++y) {
                for (int z = 0; z < 32; ++z) {
                    const int index = (x << 10) | (y << 5) | z;

                    if (voxels[index] != 0) {
                        occupancy[index >> 6] |= 1ull << (index & 63);
                    }
                }
            }
        }
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
        return voxelChunks[cx][cy][cz].voxels[IDX(lx,ly,lz,32)];
    }

    inline uint8_t GetLightValue(int x, int y, int z) const {
            
        int cx = x / 32;
        int cy = y / 32;
        int cz = z / 32;
        int lx = x % 32;
        int ly = y % 32;
        int lz = z % 32;
        if (!voxelChunks[cx][cy][cz].containsBlocks) return 0;
        return voxelChunks[cx][cy][cz].voxelLightValue[IDX(lx,ly,lz,32)];
    }

    inline void SetLightValue(int x, int y, int z, uint8_t val) const {
            
        int cx = x / 32;
        int cy = y / 32;
        int cz = z / 32;
        int lx = x % 32;
        int ly = y % 32;
        int lz = z % 32;
        if (!voxelChunks[cx][cy][cz].containsBlocks) return;
        voxelChunks[cx][cy][cz].voxelLightValue[IDX(lx,ly,lz,32)] = val;
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

        if (cellSize == 16) return &chunk.distance16[0];
        if (cellSize == 8) return &chunk.distance8[0];
        return &chunk.distance4[0];
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
                                        
                    if (traversalChunk.buildID > cellSize)
                        continue;
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
                    uint8_t &cur = cellPtr(traversalChunks[x / cellsPerChunk][y / cellsPerChunk][z/cellsPerChunk],cellSize)[IDX(x%cellsPerChunk,y%cellsPerChunk,z%cellsPerChunk,cellsPerChunk)];
                    for (int dx = -1; dx <= 1; ++dx) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dz = -1; dz <= 1; ++dz) {
                                if (!(dx < 0 || (dx == 0 && dy < 0) || (dx == 0 && dy == 0 && dz < 0))) continue;
                                const int nx = x + dx, ny = y + dy, nz = z + dz;
                                if (nx < 0 || ny < 0 || nz < 0 || nx >= gridX || ny >= gridY || nz >= gridZ) continue;
                                const uint8_t n = cellPtr(traversalChunks[nx / cellsPerChunk][ny / cellsPerChunk][nz/cellsPerChunk],cellSize)[IDX(nx%cellsPerChunk,ny%cellsPerChunk,nz%cellsPerChunk,cellsPerChunk)];
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

                    if (traversalChunk.buildID > cellSize)
                        continue;
                    uint8_t &cur = cellPtr(traversalChunks[x / cellsPerChunk][y / cellsPerChunk][z/cellsPerChunk],cellSize)[IDX(x%cellsPerChunk,y%cellsPerChunk,z%cellsPerChunk,cellsPerChunk)];
                    for (int dx = -1; dx <= 1; ++dx) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dz = -1; dz <= 1; ++dz) {
                                if (!(dx > 0 || (dx == 0 && dy > 0) || (dx == 0 && dy == 0 && dz > 0))) continue;
                                const int nx = x + dx, ny = y + dy, nz = z + dz;
                                if (nx < 0 || ny < 0 || nz < 0 || nx >= gridX || ny >= gridY || nz >= gridZ) continue;
                                const uint8_t n = cellPtr(traversalChunks[nx / cellsPerChunk][ny / cellsPerChunk][nz/cellsPerChunk],cellSize)[IDX(nx%cellsPerChunk,ny%cellsPerChunk,nz%cellsPerChunk,cellsPerChunk)];
                                if (n < 254) cur = std::min<uint8_t>(cur, static_cast<uint8_t>(n + 1));
                            }
                        }
                    }
                }
            }
        }
    }
    void Init(Vector3 cameraPosition) {
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
                    voxelChunks[x][y][z].voxels = (uint8_t*)MemAlloc(32*32*32);
                    voxelChunks[x][y][z].Clear();
                    traversalChunks[x][y][z].Init(lod);
                }
            }
        }
        Image noise = GenImagePerlinNoise(
            WORLD_WIDTH,
            WORLD_DEPTH,
            0.0f, 
            0.0f, 
            5.0f 
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
                        
                        voxelChunks[cx][cy][cz].voxels[IDX((x+dx)%32,(height+i)%32,(z+dy)%32,32)] = 3;
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
                                
                                voxelChunks[cx][cy][cz].voxels[IDX((worldX)%32,(worldY)%32,(worldZ)%32,32)] = 4;
                                voxelChunks[cx][cy][cz].containsBlocks = true;
                                
                            }
                        }   
                    }
                    
                }
                for (int y = std::max(0, height-2); y <= height; y++) {
                    int cx = x/32;
                    int cy = y/32;
                    int cz = z/32;
                    
                    int id = IDX(x%32,y%32,z%32,32);
                    if (height==y) {
                        voxelChunks[cx][cy][cz].voxels[id] = 1;
                        voxelChunks[cx][cy][cz].containsBlocks = true;
                    }
                    else {
                        voxelChunks[cx][cy][cz].containsBlocks = true;
                        voxelChunks[cx][cy][cz].voxels[id] = 1;
                    }
                }
            }
        }
        BuildDistanceToClosestVoxel();
        BuildDistanceLayer(16);
        BuildDistanceLayer(8);
        BuildDistanceLayer(4);
        int chunksWidthData = 0;
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
                        chunksWidthData+=1;
                    }
                }
            }
        }
        int id = 0;
        for (int x = 0; x < WORLD_WIDTH/32; x++) {
            for (int y= 0 ; y < WORLD_HEIGHT/32; y++) {
                for (int z = 0; z < WORLD_DEPTH/32; z++) {
                    if (voxelChunks[x][y][z].CheckOriginals(traversalChunks[x][y][z].buildID)) {
                        id+=1;
                    }

                }
            }
        }
        std::cout<<"Chunks with one voxel: " << id<<" chunks with data: "<<chunksWidthData<<" \n";
        UnloadImage(noise);
    }
};
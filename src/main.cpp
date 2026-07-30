#include "raylib.h"
#include <cinttypes>
#include <cstdint>
#include <bit>
#include "rlgl.h"
#include <unordered_map>
#include "raymath.h"
class Chunk {
    
    public:
    struct VoxelFace {
        uint8_t x;
        uint8_t y;
        uint8_t z;
        uint16_t data;
        uint8_t face;
    };
    int chunkX;
    int chunkY;
    int chunkZ;
    Mesh mesh;
    Model model;
    uint16_t data[32][32][32];
    bool generatedTerrain = false;
    bool meshed = false;
    bool uploaded = false;
    Chunk* neighbors[6];
    inline static float GetCaveNoise(int worldX, int worldY, int worldZ) {
        float freq1 = 0.05f;
        float freq2 = 0.1f;
        float freq3 = 0.02f;
        
        float noise1 = sinf(worldX * freq1 + worldY * freq1 * 1.7f + worldZ * freq1 * 0.9f) * 
                      cosf(worldX * freq1 * 1.3f - worldY * freq1 * 0.8f + worldZ * freq1 * 1.1f);
        float noise2 = sinf(worldX * freq2 * 0.7f + worldY * freq2 * 1.1f + worldZ * freq2 * 1.3f) * 
                      cosf(worldX * freq2 * 1.2f - worldY * freq2 * 0.9f + worldZ * freq2 * 0.7f);
        float noise3 = sinf(worldX * freq3 + worldY * freq3 * 2.1f + worldZ * freq3 * 1.7f);
        
        float combined = (noise1 * 0.5f + noise2 * 0.3f + noise3 * 0.2f) * 0.5f + 0.5f;
        return combined;
    }
    
    void Init(Vector3 chunkPos, int x, int y, int z, Color *pixels) {
        chunkX = x;
        chunkY = y;
        chunkZ = z;
        
        for (int x = 0; x < 32; x++) {
            for (int y = 0; y < 32; y++) {
                for (int z = 0; z < 32; z++) {
                    data[x][y][z] = 0;
                }
            }
        }
        
        for (int x = 0; x < 32; x++) {
            for (int y = 0; y < 32; y++) {
                for (int z = 0; z < 32; z++) {
                    int worldX = x + chunkPos.x;
                    int worldY = y + chunkPos.y;
                    int worldZ = z + chunkPos.z;
                    float caveNoise = GetCaveNoise(worldX, worldY, worldZ);
                    bool isCave = false;
                    if (caveNoise > 0.35f && caveNoise < 0.55f) {
                        isCave = true;
                    }
                    if (isCave) {
                        data[x][y][z] = 0; 
                    }
                    else {
                        data[x][y][z] = 1;
                    }
                }
            }
        }
        
        generatedTerrain = true;
    }
    void MeshChunk() {
        int voxelCount = 0;
        for (int x = 0; x < 32; x++) {
            for (int y = 0; y < 32; y++) {
                for (int z = 0; z < 32; z++) {
                    if (data[x][y][z]!=0) voxelCount++;
                }
            }
        }
        VoxelFace* faces = (VoxelFace*)MemAlloc(voxelCount*sizeof(VoxelFace));
        int i = 0;      
        int faceQuadCount = 0;  
        for (int x = 0; x < 32; x++) {
            for (int y = 0; y < 32; y++) {
                for (int z = 0; z < 32; z++) {
                    if (data[x][y][z]!=0) {
                        faces[i].face = 0;
                            
                        faces[i].face |= (getVoxel(x + 1, y, z) == 0) ? (1 << 0) : 0;
                        faces[i].face |= (getVoxel(x - 1, y, z) == 0) ? (1 << 1) : 0;
                        faces[i].face |= (getVoxel(x, y + 1, z) == 0) ? (1 << 2) : 0;
                        faces[i].face |= (getVoxel(x, y - 1, z) == 0) ? (1 << 3) : 0;
                        faces[i].face |= (getVoxel(x, y, z + 1) == 0) ? (1 << 4) : 0;
                        faces[i].face |= (getVoxel(x, y, z - 1) == 0) ? (1 << 5) : 0;
                        if (faces[i].face!=0) {
                                
                            faceQuadCount+=std::__popcount(faces[i].face);
                            faces[i].data = data[x][y][z];
                            faces[i].x = x;
                            faces[i].y = y;
                            faces[i].z = z;
                            i++;
                        }
                    }
                }
            }
        }
        mesh = {0};
        mesh.triangleCount = faceQuadCount*2;
        mesh.vertexCount = mesh.triangleCount*3;
        mesh.vertices = (float*)MemAlloc(mesh.vertexCount*3*sizeof(float));
        mesh.colors = (uint8_t*)MemAlloc(mesh.vertexCount*4*sizeof(uint8_t));
        Vector3 cubeVerts[8] = {
            {0.0f,0.0f,0.0f},
            {1.0f, 0.0f, 0.0f},
            {1.0f, 1.0f, 0.0f},
            {0.0f,1.0f,0.0f},
            {0.0f,0.0f,1.0f},
            {1.0f, 0.0f,1.0f},
            {1.0f,1.0f,1.0f},
            {0.0f,1.0f,1.0f}
        };
        int faceIndices[6][4] = {
            {1,5,6,2},
            {0,3,7,4},
            {3,2,6,7},
            {0,4,5,1},
            {4,7,6,5},
            {0,1,2,3}
        };
        int vertexIndex = 0;    
        
        Color colors[9] = {
            BLANK,      // 0 - Air (shouldn't be used)
            GREEN,      // 1 - Grass
            YELLOW,     // 2 - Sand  
            GRAY,       // 3 - Stone
            WHITE,      // 4 - Snow
            DARKGRAY,   // 5 - Bedrock
            RED,        // 6 - Ore 1
            BLUE,       // 7 - Ore 2
            PURPLE      // 8 - Ore 3
        };
        Vector3 faceDirs[6] = {
            {1,0,0},   // Right
            {-1,0,0},  // Left
            {0,1,0},   // Top
            {0,-1,0},  // Bottom
            {0,0,1},   // Front
            {0,0,-1}   // Back
        };
        for (int r = 0; r < i; r++) {
            Vector3 pos = {(float)faces[r].x,(float)faces[r].y,(float)faces[r].z};
            for (int f = 0; f < 6; f++) {
                if (faces[r].face & (1<<f)) {
                        
                    float ao = 1.0f;
                    Vector3 dir = faceDirs[f];
                    
                    Vector3 perp1, perp2;
                    if (f == 0 || f == 1) {
                        perp1 = {0, 1, 0};
                        perp2 = {0, 0, 1};
                    } else if (f == 2 || f == 3) {
                        perp1 = {1, 0, 0};
                        perp2 = {0, 0, 1};
                    } else {
                        perp1 = {1, 0, 0};
                        perp2 = {0, 1, 0};
                    }
                    int side1 = getVoxel(pos.x + perp1.x, pos.y + perp1.y, pos.z + perp1.z);
                    int side2 = getVoxel(pos.x + perp2.x, pos.y + perp2.y, pos.z + perp2.z);
                    int side3 = getVoxel(pos.x - perp1.x, pos.y - perp1.y, pos.z - perp1.z);
                    int side4 = getVoxel(pos.x - perp2.x, pos.y - perp2.y, pos.z - perp2.z);
                    
                    int corner1 = getVoxel(pos.x + perp1.x + perp2.x, pos.y + perp1.y + perp2.y, pos.z + perp1.z + perp2.z);
                    int corner2 = getVoxel(pos.x + perp1.x - perp2.x, pos.y + perp1.y - perp2.y, pos.z + perp1.z - perp2.z);
                    int corner3 = getVoxel(pos.x - perp1.x + perp2.x, pos.y - perp1.y + perp2.y, pos.z - perp1.z + perp2.z);
                    int corner4 = getVoxel(pos.x - perp1.x - perp2.x, pos.y - perp1.y - perp2.y, pos.z - perp1.z - perp2.z);
                    
                    ao -= (side1 != 0) * 0.10f;
                    ao -= (side2 != 0) * 0.10f;
                    ao -= (side3 != 0) * 0.10f;
                    ao -= (side4 != 0) * 0.10f;
                    
                    if (side1 && side2) ao -= (corner1 != 0) * 0.05f;
                    if (side1 && side4) ao -= (corner2 != 0) * 0.05f;
                    if (side3 && side2) ao -= (corner3 != 0) * 0.05f;
                    if (side3 && side4) ao -= (corner4 != 0) * 0.05f;
                    if (ao < 0.2f) ao = 0.2f;
                    if (ao > 1.0f) ao = 1.0f;
                    int v1 = faceIndices[f][0];
                    int v2 = faceIndices[f][1];
                    int v3 = faceIndices[f][2];
                    int v4 = faceIndices[f][3];
                    int triVerts1[3] = {v1,v2,v3};
                    for (int v : triVerts1) {
                        mesh.vertices[vertexIndex * 3 + 0] = cubeVerts[v].x + pos.x;
                        mesh.vertices[vertexIndex * 3 + 1] = cubeVerts[v].y + pos.y;
                        mesh.vertices[vertexIndex * 3 + 2] = cubeVerts[v].z + pos.z;
                        mesh.colors[vertexIndex * 4 + 0] = colors[faces[r].data].r*ao;
                        mesh.colors[vertexIndex * 4 + 1] = colors[faces[r].data].g*ao;
                        mesh.colors[vertexIndex * 4 + 2] = colors[faces[r].data].b*ao;
                        mesh.colors[vertexIndex * 4 + 3] = 255;
                        vertexIndex++;
                    }
                   int triVerts2[3] = {v1,v3,v4};
                     
                    for (int v : triVerts2) {
                        mesh.vertices[vertexIndex * 3 + 0] = cubeVerts[v].x + pos.x;
                        mesh.vertices[vertexIndex * 3 + 1] = cubeVerts[v].y + pos.y;
                        mesh.vertices[vertexIndex * 3 + 2] = cubeVerts[v].z + pos.z;
                        mesh.colors[vertexIndex * 4 + 0] = colors[faces[r].data].r*ao;
                        mesh.colors[vertexIndex * 4 + 1] = colors[faces[r].data].g*ao;
                        mesh.colors[vertexIndex * 4 + 2] = colors[faces[r].data].b*ao;
                        mesh.colors[vertexIndex * 4 + 3] = 255;
                        vertexIndex++;
                    }
                }
            }
            
        }
        meshed = true;
        MemFree(faces);
    }
    void UploadData() {
        if (uploaded) {
            UnloadModel(model);
        }
        UploadMesh(&mesh,false);
        model = LoadModelFromMesh(mesh);
        uploaded = true;
    }
    inline uint16_t getVoxel(int x, int y, int z) {
    if (x >= 0 && x < 32 && y >= 0 && y < 32 && z >= 0 && z < 32) {
        return this->data[x][y][z];
    }
        if (x < 0 && this->neighbors[1] != nullptr) {
            return this->neighbors[1]->data[x + 32][y][z];
        }
        if (x >= 32 && this->neighbors[0] != nullptr) {
            return this->neighbors[0]->data[x - 32][y][z];
        }
        if (y < 0 && this->neighbors[3] != nullptr) {
            return this->neighbors[3]->data[x][y + 32][z];
        }
        if (y >= 32 && this->neighbors[2] != nullptr) {
            return this->neighbors[2]->data[x][y - 32][z];
        }
        if (z < 0 && this->neighbors[5] != nullptr) {
            return this->neighbors[5]->data[x][y][z + 32];
        }
        if (z >= 32 && this->neighbors[4] != nullptr) {
            return this->neighbors[4]->data[x][y][z - 32];
        }
        
        return 0;
    }
    void UnloadData() {
        if (uploaded) {
            UnloadModel(model);
        }
        uploaded = false;
    }
};
struct NoiseChunk {
    Image noise;
    bool generated = false;
};


class Map {
public:
    std::unordered_map<uint64_t, Chunk> chunks;
    std::unordered_map<uint64_t, NoiseChunk> noiseWorld;
    
    static inline uint64_t getKey(int x, int y, int z) {
        uint64_t key = 0;
        key |= (uint64_t)(x + 1024) << 40;
        key |= (uint64_t)(y + 1024) << 20;
        key |= (uint64_t)(z + 1024);
        return key;
    }
    
    static inline uint64_t getKey2D(int x, int z) {
        uint64_t key = 0;
        key |= (uint64_t)(x + 1024) << 32;
        key |= (uint64_t)(z + 1024);
        return key;
    }
    
    void GenerateChunk(int cx, int cy, int cz, int depth = 0, bool first=false) {
        if (depth > 2) return; 
        
        uint64_t key = getKey(cx, cy, cz);
        
        if (chunks.find(key) != chunks.end()) {
            return;
        }
        
        uint64_t noiseKey = getKey2D(cx, cz);
        if (noiseWorld.find(noiseKey) == noiseWorld.end()) {
            NoiseChunk noise;
            noise.noise = GenImagePerlinNoise(32, 32, cx * 32, cz * 32, 1.0f);
            noise.generated = true;
            noiseWorld[noiseKey] = noise;
        }
        
        auto& chunk = chunks[key];
        Color* pixels = LoadImageColors(noiseWorld[noiseKey].noise);
        chunk.Init({cx * 32.0f, cy * 32.0f, cz * 32.0f}, cx, cy, cz, pixels);
        UnloadImageColors(pixels);
                
        GenerateChunk(cx + 1, cy, cz, depth + 1);
        GenerateChunk(cx - 1, cy, cz, depth + 1);
        GenerateChunk(cx, cy + 1, cz, depth + 1);
        GenerateChunk(cx, cy - 1, cz, depth + 1);
        GenerateChunk(cx, cy, cz + 1, depth + 1);
        GenerateChunk(cx, cy, cz - 1, depth + 1);
    }
    
    void SetNeigboursChunk(int x, int y, int z) {
        Chunk& chunk = chunks[getKey(x,y,z)];
        chunk.neighbors[0] = &chunks[getKey(chunk.chunkX + 1, chunk.chunkY, chunk.chunkZ)];
        chunk.neighbors[1] = &chunks[getKey(chunk.chunkX - 1, chunk.chunkY, chunk.chunkZ)];
        chunk.neighbors[2] = &chunks[getKey(chunk.chunkX, chunk.chunkY + 1, chunk.chunkZ)];
        chunk.neighbors[3] = &chunks[getKey(chunk.chunkX, chunk.chunkY - 1, chunk.chunkZ)];
        chunk.neighbors[4] = &chunks[getKey(chunk.chunkX, chunk.chunkY, chunk.chunkZ + 1)];
        chunk.neighbors[5] = &chunks[getKey(chunk.chunkX, chunk.chunkY, chunk.chunkZ - 1)];
    }
    
    void MeshAllChunks() {
        for (auto& pair : chunks) {
            if (!pair.second.meshed) {
                pair.second.MeshChunk();
            }
        }
    }
    
    void UploadAllChunks() {
        for (auto& pair : chunks) {
            if (pair.second.meshed && !pair.second.uploaded) {
                pair.second.UploadData();
            }
        }
    }
};
class App {
    public:
    Camera camera; 
    Map world;
    App() {
        InitWindow(1920,1080,"Voxelized");    
        rlDisableBackfaceCulling();
        camera.position = (Vector3){ 0.0f, 2.0f, 4.0f }; 
        camera.target = (Vector3){ 0.0f, 2.0f, 0.0f };
        camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        camera.fovy = 60.0f;
        camera.projection = CAMERA_PERSPECTIVE;
        DisableCursor();
        for (int x = 0; x < 10; x++) {
            for (int z = 0; z < 10; z++) {
                for (int y = 0; y < 5; y++) {
                    world.GenerateChunk(x, y, z,0,true);
                }
            }
        }
        for (int x = 0; x < 10; x++) {
            for (int z = 0; z < 10; z++) {
                for (int y = 0; y < 5; y++) {

                     world.SetNeigboursChunk(x,y,z);
                }
            }
        }
    }
    void Run() {
        while (!WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(SKYBLUE);
            UpdateCamera(&camera,CAMERA_FREE);
            BeginMode3D(camera);
                
            for (int x = 0; x < 10; x++) {
                for (int y = 0; y < 5; y++) {
                    for (int z = 0; z < 10; z++) {
                        if (!world.chunks[world.getKey(x,y,z)].meshed) {
                            world.chunks[world.getKey(x,y,z)].MeshChunk();
                        }
                        if (!world.chunks[world.getKey(x,y,z)].generatedTerrain) {
                            if (!world.noiseWorld[world.getKey2D(x,z)].generated) {
                                NoiseChunk noise;
                                noise.noise = GenImagePerlinNoise(32, 32, x * 32, z * 32, 1.0f);
                                noise.generated = true;
                                world.noiseWorld[world.getKey2D(x,z)] = noise;
                            }
                            world.chunks[world.getKey(x,y,z)].Init({x*32.0f,y*32.0f,z*32.0f}, x,y,z,LoadImageColors(world.noiseWorld[world.getKey2D(x,z)].noise));
                            world.GenerateChunk(x,y,z,0,true);
                            world.SetNeigboursChunk(x,y,z);
                        }
                    }
                }
            }
            for (int x = 0; x < 10; x++) {
                for (int y = 0; y < 5; y++) {
                    for (int z = 0; z < 10; z++) {
                        
                        if (world.chunks[world.getKey(x,y,z)].uploaded) {
                            DrawModel(world.chunks[world.getKey(x,y,z)].model,{(float)x*32,(float)y*32,(float)z*32},1,WHITE);
                        }
                        else {
                            if (world.chunks[world.getKey(x,y,z)].generatedTerrain) {
                                
                                world.chunks[world.getKey(x,y,z)].UploadData();
                            }
                        }
                    }
                }
            }
            EndMode3D();
            DrawFPS(0,0);
            EndDrawing();
        }
    }
};

int main() {
    App app;
    app.Run();
}
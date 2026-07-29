#include "raylib.h"
#include <cinttypes>
#include <cstdint>
#include <bit>
#include "rlgl.h"
class Chunk {
    
    public:
    struct VoxelFace {
        uint8_t x;
        uint8_t y;
        uint8_t z;
        uint16_t data;
        uint8_t face;
    };
    Mesh mesh;
    Model model;
    uint16_t data[32][32][32];
    void Init() {
        for (int x = 0; x < 32; x++) {
            for (int y = 0; y < 32; y++) {
                for (int z = 0; z < 32; z++) {
                    data[x][y][z] = (y<30)*GetRandomValue(0,5);
                }
            }
        }
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
                            
                        faces[i].face |= (x < 31 && data[x+1][y][z] == 0) ? (1 << 0) : 0;
                        faces[i].face |= (x > 0 && data[x-1][y][z] == 0) ? (1 << 1) : 0;
                        faces[i].face |= (y < 31 && data[x][y+1][z] == 0) ? (1 << 2) : 0;
                        faces[i].face |= (y > 0 && data[x][y-1][z] == 0) ? (1 << 3) : 0;
                        faces[i].face |= (z < 31 && data[x][y][z+1] == 0) ? (1 << 4) : 0;
                        faces[i].face |= (z > 0 && data[x][y][z-1] == 0) ? (1 << 5) : 0;
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
        Mesh mesh = {0};
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

        Vector3 faceNormals[6] = {
            {1.0f, 0.0f, 0.0f},
            {-1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, -1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, -1.0f}
        };
        int vertexIndex = 0;    
        Color colors[5] = {RED,BLUE,GREEN,GRAY,BROWN};
        for (int r = 0; r < i; r++) {
            uint16_t faceMask = faces[r].face;
            Vector3 pos = {(float)faces[r].x,(float)faces[r].y,(float)faces[r].z};
            for (int f = 0; f < 6; f++) {
                if (faces[r].face & (1<<f)) {
                    int v1 = faceIndices[f][0];
                    int v2 = faceIndices[f][1];
                    int v3 = faceIndices[f][2];
                    int v4 = faceIndices[f][3];
                    int triVerts1[3] = {v1,v2,v3};
                    for (int v : triVerts1) {
                        mesh.vertices[vertexIndex * 3 + 0] = cubeVerts[v].x + pos.x;
                        mesh.vertices[vertexIndex * 3 + 1] = cubeVerts[v].y + pos.y;
                        mesh.vertices[vertexIndex * 3 + 2] = cubeVerts[v].z + pos.z;
                        mesh.colors[vertexIndex * 4 + 0] = colors[faces[r].data].r;
                        mesh.colors[vertexIndex * 4 + 1] = colors[faces[r].data].g;
                        mesh.colors[vertexIndex * 4 + 2] = colors[faces[r].data].b;
                        mesh.colors[vertexIndex * 4 + 3] = 255;
                        vertexIndex++;
                    }
                   int triVerts2[3] = {v1,v3,v4};
                     
                    for (int v : triVerts2) {
                        mesh.vertices[vertexIndex * 3 + 0] = cubeVerts[v].x + pos.x;
                        mesh.vertices[vertexIndex * 3 + 1] = cubeVerts[v].y + pos.y;
                        mesh.vertices[vertexIndex * 3 + 2] = cubeVerts[v].z + pos.z;
                        mesh.colors[vertexIndex * 4 + 0] = colors[faces[r].data].r;
                        mesh.colors[vertexIndex * 4 + 1] = colors[faces[r].data].g;
                        mesh.colors[vertexIndex * 4 + 2] = colors[faces[r].data].b;
                        mesh.colors[vertexIndex * 4 + 3] = 255;
                        vertexIndex++;
                    }
                }
            }
            
        }
        UploadMesh(&mesh,false);
        model = LoadModelFromMesh(mesh);
        MemFree(faces);
    }
};

class App {
    public:
    Camera camera; 
    Chunk chunk;
    App() {
        InitWindow(1920,1080,"Voxelized");    
        rlDisableBackfaceCulling();
        camera.position = (Vector3){ 0.0f, 2.0f, 4.0f }; 
        camera.target = (Vector3){ 0.0f, 2.0f, 0.0f };
        camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        camera.fovy = 60.0f;
        camera.projection = CAMERA_PERSPECTIVE;
        DisableCursor();
        chunk.Init();
        chunk.MeshChunk();
    }
    void Run() {
        while (!WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(SKYBLUE);
            UpdateCamera(&camera,CAMERA_FREE);
            BeginMode3D(camera);
            DrawModel(chunk.model,{0,0,0},1,WHITE);
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
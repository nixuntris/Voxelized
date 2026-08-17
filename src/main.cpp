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
using Clock = std::chrono::steady_clock;
const float SCALE = 1;
auto ms = [](auto start, auto end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
};
const int width = 800;
const int height = 800;

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
const int RENDERDISTANCE = 1024;
struct Chunk {
    uint8_t *voxels;
    uint8_t *voxelLightValue;
    bool containsBlocks;
    uint8_t distanceToClosestVoxel=0;

    uint8_t distance16[2][2][2];
    uint8_t distance8[4][4][4];
    uint8_t distance4[8][8][8];
    void Clear() {
        containsBlocks = false;
        std::fill(&distance16[0][0][0], &distance16[0][0][0] + 8, 255);
        std::fill(&distance8[0][0][0], &distance8[0][0][0] + 64, 255);
        std::fill(&distance4[0][0][0], &distance4[0][0][0] + 512, 255);

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
    inline uint8_t GetVoxel(int x, int y, int z) const {
            
        int cx = x / 32;
        int cy = y / 32;
        int cz = z / 32;
        int lx = x % 32;
        int ly = y % 32;
        int lz = z % 32;
        if (!chunks[cx][cy][cz].containsBlocks) return 0;
        return chunks[cx][cy][cz].voxels[lx * 32 * 32 + ly * 32 + lz];
    }

    inline uint8_t GetLightValue(int x, int y, int z) const {
            
        int cx = x / 32;
        int cy = y / 32;
        int cz = z / 32;
        int lx = x % 32;
        int ly = y % 32;
        int lz = z % 32;
        if (!chunks[cx][cy][cz].containsBlocks) return 0;
        return chunks[cx][cy][cz].voxelLightValue[lx * 32 * 32 + ly * 32 + lz];
    }

    inline void SetLightValue(int x, int y, int z, uint8_t val) const {
            
        int cx = x / 32;
        int cy = y / 32;
        int cz = z / 32;
        int lx = x % 32;
        int ly = y % 32;
        int lz = z % 32;
        if (!chunks[cx][cy][cz].containsBlocks) return;
        chunks[cx][cy][cz].voxelLightValue[lx * 32 * 32 + ly * 32 + lz] = val;
    }
    void BuildDistanceToClosestVoxel() {
        constexpr int CHUNK_COUNT = SIZE / 32;

        struct ChunkPos { int x, y, z; };
        std::queue<ChunkPos> q;

        for (int x = 0; x < CHUNK_COUNT; ++x) {
            for (int y = 0; y < CHUNK_COUNT; ++y) {
                for (int z = 0; z < CHUNK_COUNT; ++z) {
                    Chunk &chunk = chunks[x][y][z];
                    if (chunk.containsBlocks) {
                        chunk.distanceToClosestVoxel = 0;
                        q.push({x, y, z});
                    } else {
                        chunk.distanceToClosestVoxel = 255;
                    }
                }
            }
        }

        while (!q.empty()) {
            ChunkPos p = q.front();
            q.pop();

            const uint8_t current = chunks[p.x][p.y][p.z].distanceToClosestVoxel;
            if (current == 254) continue;
            const uint8_t nextDistance = static_cast<uint8_t>(current + 1);

            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        if (dx == 0 && dy == 0 && dz == 0) continue;

                        const int nx = p.x + dx;
                        const int ny = p.y + dy;
                        const int nz = p.z + dz;
                        if (nx < 0 || ny < 0 || nz < 0 ||
                            nx >= CHUNK_COUNT || ny >= CHUNK_COUNT || nz >= CHUNK_COUNT) {
                            continue;
                        }

                        Chunk &neighbor = chunks[nx][ny][nz];
                        if (nextDistance < neighbor.distanceToClosestVoxel) {
                            neighbor.distanceToClosestVoxel = nextDistance;
                            q.push({nx, ny, nz});
                        }
                    }
                }
            }
        }
    }

    void BuildDistanceLayer(int cellSize) {
        const int cellsPerChunk = 32 / cellSize;
        const int grid = SIZE / cellSize;

        auto cellPtr = [&](Chunk &chunk) -> uint8_t* {
            if (cellSize == 16) return &chunk.distance16[0][0][0];
            if (cellSize == 8) return &chunk.distance8[0][0][0];
            return &chunk.distance4[0][0][0];
        };

        auto distanceRef = [&](int gx, int gy, int gz) -> uint8_t& {
            Chunk &chunk = chunks[gx / cellsPerChunk][gy / cellsPerChunk][gz / cellsPerChunk];
            const int x = gx % cellsPerChunk;
            const int y = gy % cellsPerChunk;
            const int z = gz % cellsPerChunk;
            return cellPtr(chunk)[(x * cellsPerChunk + y) * cellsPerChunk + z];
        };

        for (int cx = 0; cx < SIZE / 32; ++cx) {
            for (int cy = 0; cy < SIZE / 32; ++cy) {
                for (int cz = 0; cz < SIZE / 32; ++cz) {
                    Chunk &chunk = chunks[cx][cy][cz];
                    uint8_t *distance = cellPtr(chunk);
                    std::fill(distance, distance + cellsPerChunk * cellsPerChunk * cellsPerChunk, 255);
                    if (!chunk.containsBlocks) continue;

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
                                            if (chunk.voxels[(bx + x) * 32 * 32 + (by + y) * 32 + bz + z]) {
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

        for (int x = 0; x < grid; ++x) {
            for (int y = 0; y < grid; ++y) {
                for (int z = 0; z < grid; ++z) {
                    uint8_t &cur = distanceRef(x, y, z);
                    for (int dx = -1; dx <= 1; ++dx) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dz = -1; dz <= 1; ++dz) {
                                if (!(dx < 0 || (dx == 0 && dy < 0) || (dx == 0 && dy == 0 && dz < 0))) continue;
                                const int nx = x + dx, ny = y + dy, nz = z + dz;
                                if (nx < 0 || ny < 0 || nz < 0 || nx >= grid || ny >= grid || nz >= grid) continue;
                                const uint8_t n = distanceRef(nx, ny, nz);
                                if (n < 254) cur = std::min<uint8_t>(cur, static_cast<uint8_t>(n + 1));
                            }
                        }
                    }
                }
            }
        }

        for (int x = grid - 1; x >= 0; --x) {
            for (int y = grid - 1; y >= 0; --y) {
                for (int z = grid - 1; z >= 0; --z) {
                    uint8_t &cur = distanceRef(x, y, z);
                    for (int dx = -1; dx <= 1; ++dx) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dz = -1; dz <= 1; ++dz) {
                                if (!(dx > 0 || (dx == 0 && dy > 0) || (dx == 0 && dy == 0 && dz > 0))) continue;
                                const int nx = x + dx, ny = y + dy, nz = z + dz;
                                if (nx < 0 || ny < 0 || nz < 0 || nx >= grid || ny >= grid || nz >= grid) continue;
                                const uint8_t n = distanceRef(nx, ny, nz);
                                if (n < 254) cur = std::min<uint8_t>(cur, static_cast<uint8_t>(n + 1));
                            }
                        }
                    }
                }
            }
        }
    }

    inline float distanceToClosestVoxel(float x, float y, float z, const Vector3 &dir) const {
        if (x < 0 || y < 0 || z < 0 || x >= SIZE || y >= SIZE || z >= SIZE) return 0.0f;

        const int ix = (int)x, iy = (int)y, iz = (int)z;
        const Chunk &chunk = chunks[ix >> 5][iy >> 5][iz >> 5];
        const int lx = ix & 31, ly = iy & 31, lz = iz & 31;

        auto step = [](uint8_t d, float size) {
            return d > 1 && d != 255 ? (d - 1) * size : 0.0f;
        };

        const float jump = std::max({
            step(chunk.distanceToClosestVoxel, 32.0f),
            step(chunk.distance16[lx >> 4][ly >> 4][lz >> 4], 16.0f),
            step(chunk.distance8[lx >> 3][ly >> 3][lz >> 3], 8.0f),
            step(chunk.distance4[lx >> 2][ly >> 2][lz >> 2], 4.0f)
        });

        if (jump > 0.0f) return jump;

        const float inf = std::numeric_limits<float>::infinity();
        const float tx = dir.x > 0.0f ? (ix + 1.0f - x) / dir.x : dir.x < 0.0f ? (x - ix) / -dir.x : inf;
        const float ty = dir.y > 0.0f ? (iy + 1.0f - y) / dir.y : dir.y < 0.0f ? (y - iy) / -dir.y : inf;
        const float tz = dir.z > 0.0f ? (iz + 1.0f - z) / dir.z : dir.z < 0.0f ? (z - iz) / -dir.z : inf;

        return std::min({tx, ty, tz}) + 0.0001f;
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

                if (GetRandomValue(0,1000)==1) {
                    for (int i = 0; i < 50; i++) {
                        chunks[x/32][(height+i)/32][z/32].voxels[(x%32)*32*32+((height+i)%32)*32+z%32] = 3;
                        chunks[x/32][(height+i)/32][z/32].containsBlocks = true;
                            
                        }
                    }
            
                for (int y = height-2; y <= height; y++) {
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
        BuildDistanceToClosestVoxel();
        BuildDistanceLayer(16);
        BuildDistanceLayer(8);
        BuildDistanceLayer(4);

        for (int x = 0; x < SIZE/32; x++) {
            for (int y= 0 ; y < SIZE/32; y++) {
                for (int z = 0; z < SIZE/32; z++) {
                    if (!chunks[x][y][z].containsBlocks) {
                        MemFree(chunks[x][y][z].voxels);
                        chunks[x][y][z].voxels = nullptr;
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
        InitWindow(width*SCALE,height*SCALE,"Voxelized");
        camera.position = (Vector3){ SIZE/2, 128, SIZE/2 };
        camera.target = (Vector3){ 0.0f, 2.0f, 0.0f };
        camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        camera.fovy = 60.0f;
        camera.projection = CAMERA_PERSPECTIVE;
        matProj = MatrixIdentity();
        matProj = MatrixPerspective(camera.fovy*DEG2RAD, ((double)width/(double)height), 0.01f, 10000.0f);
        imageBuffer = GenImageColor(width,height,BLACK);
        ImageFormat(&imageBuffer,PIXELFORMAT_UNCOMPRESSED_R8G8B8);
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

    const Color colors[10] = {SKYBLUE,GREEN,{uint8_t(GREEN.r*0.9),uint8_t(GREEN.g*0.9),uint8_t(GREEN.b*0.9),255},BROWN};
    
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
                    directionStorage[px + y * width] = { xs[i], ys[i], zs[i] };
                }
            }
        }
        auto dirEnd = Clock::now();

        auto renderStart = Clock::now();
        #pragma omp parallel for collapse(2)
        for (int x = 0; x < width; x++) {
            for (int y = 0; y < height; y++) {
                int idx = (y * imageBuffer.width + x) * 3;
                int pixelIndex = x + y * width;

                if ((x + y + frame) % 3 == 0) continue;
                
                ((unsigned char *)imageBuffer.data)[idx] = SKYBLUE.r;
                ((unsigned char *)imageBuffer.data)[idx + 1] = SKYBLUE.g;
                ((unsigned char *)imageBuffer.data)[idx + 2] = SKYBLUE.b;

                Vector3 direction = directionStorage[pixelIndex];
                float t = 0.0f;
                int steps = 0;

                while (t < RENDERDISTANCE ) {
                    const float voxelX = camera.position.x + direction.x * t;
                    const float voxelY = camera.position.y + direction.y * t;
                    const float voxelZ = camera.position.z + direction.z * t;

                    if (voxelX < 0.0f || voxelY < 0.0f || voxelZ < 0.0f ||
                        voxelX >= SIZE || voxelY >= SIZE || voxelZ >= SIZE) {
                        break;
                    }

                    const uint8_t type = world.GetVoxel(voxelX, voxelY, voxelZ);
                    if (type != 0) {
                        ((unsigned char *)imageBuffer.data)[idx] = colors[type].r;
                        ((unsigned char *)imageBuffer.data)[idx + 1] = colors[type].g;
                        ((unsigned char *)imageBuffer.data)[idx + 2] = colors[type].b;
                        break;
                    }

                    t += world.distanceToClosestVoxel(voxelX, voxelY, voxelZ, direction);
                    ++steps;
                }
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
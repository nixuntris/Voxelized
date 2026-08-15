#include "raylib.h"
#include <cinttypes>
#include <cstdint>
#include <bit>
#include "rlgl.h"
#include <unordered_map>
#include "raymath.h"
#include <iostream>
#include <cmath>
#include <immintrin.h> 

const int width = 800;
const int height = 800;

inline static void GetScreenToWorldRayOptimized_SIMD(__m256 positions_x, __m256 positions_y, 
                                                      Camera camera, int width, int height,
                                                      Matrix matView, Matrix viewInv, 
                                                      Matrix matProj, __m256* out_dirs) {
    __m256 w = _mm256_set1_ps((float)width);
    __m256 h = _mm256_set1_ps((float)height);
    __m256 two = _mm256_set1_ps(2.0f);
    __m256 one = _mm256_set1_ps(1.0f);
    
    __m256 x = _mm256_sub_ps(_mm256_mul_ps(_mm256_div_ps(positions_x, w), two), one);
    __m256 y = _mm256_sub_ps(one, _mm256_mul_ps(_mm256_div_ps(positions_y, h), two));
    
    const float p0 = 1.73205f;
    const float p5 = 1.73205f;
    __m256 p0v = _mm256_set1_ps(p0);
    __m256 p5v = _mm256_set1_ps(p5);
    
    __m256 viewX = _mm256_div_ps(x, p0v);
    __m256 viewY = _mm256_div_ps(y, p5v);
    
    __m256 viewInv_m0 = _mm256_set1_ps(viewInv.m0);
    __m256 viewInv_m4 = _mm256_set1_ps(viewInv.m4);
    __m256 viewInv_m8 = _mm256_set1_ps(viewInv.m8);
    __m256 viewInv_m1 = _mm256_set1_ps(viewInv.m1);
    __m256 viewInv_m5 = _mm256_set1_ps(viewInv.m5);
    __m256 viewInv_m9 = _mm256_set1_ps(viewInv.m9);
    __m256 viewInv_m2 = _mm256_set1_ps(viewInv.m2);
    __m256 viewInv_m6 = _mm256_set1_ps(viewInv.m6);
    __m256 viewInv_m10 = _mm256_set1_ps(viewInv.m10);
    
    __m256 dir_x = _mm256_add_ps(
        _mm256_add_ps(_mm256_mul_ps(viewInv_m0, viewX), _mm256_mul_ps(viewInv_m4, viewY)),
        viewInv_m8
    );
    
    __m256 dir_y = _mm256_add_ps(
        _mm256_add_ps(_mm256_mul_ps(viewInv_m1, viewX), _mm256_mul_ps(viewInv_m5, viewY)),
        viewInv_m9
    );
    
    __m256 dir_z = _mm256_add_ps(
        _mm256_add_ps(_mm256_mul_ps(viewInv_m2, viewX), _mm256_mul_ps(viewInv_m6, viewY)),
        viewInv_m10
    );
    
    __m256 len_sq = _mm256_add_ps(
        _mm256_add_ps(_mm256_mul_ps(dir_x, dir_x), _mm256_mul_ps(dir_y, dir_y)),
        _mm256_mul_ps(dir_z, dir_z)
    );
    __m256 len = _mm256_sqrt_ps(len_sq);
    __m256 len_mask = _mm256_cmp_ps(len, _mm256_setzero_ps(), _CMP_GT_OS);
    
    __m256 inv_len = _mm256_div_ps(_mm256_set1_ps(1.0f), len);
    __m256 result_x = _mm256_and_ps(len_mask, _mm256_mul_ps(dir_x, inv_len));
    __m256 result_y = _mm256_and_ps(len_mask, _mm256_mul_ps(dir_y, inv_len));
    __m256 result_z = _mm256_and_ps(len_mask, _mm256_mul_ps(dir_z, inv_len));
    
    out_dirs[0] = result_x;
    out_dirs[1] = result_y;
    out_dirs[2] = result_z;
}

class App {
public:
    Camera camera;
    Matrix matProj;
    Image imageBuffer;
    Texture displayBuffer;
    
    App() {
        InitWindow(width, height, "Voxelized - SIMD");
        camera.position = (Vector3){ 0.0f, 2.0f, 4.0f };
        camera.target = (Vector3){ 0.0f, 2.0f, 0.0f };
        camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        camera.fovy = 60.0f;
        camera.projection = CAMERA_PERSPECTIVE;
        matProj = MatrixIdentity();
        matProj = MatrixPerspective(camera.fovy * DEG2RAD, ((double)width / (double)height), 0.01f, 10000.0f);
        imageBuffer = GenImageColor(width, height, BLACK);
        displayBuffer = LoadTextureFromImage(imageBuffer);
        DisableCursor();
    }
    
    void Run() {
        int frame = 0;
        const int RAYS_PER_BATCH = 8;
        
        while (!WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(SKYBLUE);
            Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
            Matrix viewInv = MatrixInvert(matView);
            frame++;
            
            __m256 cam_pos_x = _mm256_set1_ps(camera.position.x);
            __m256 cam_pos_y = _mm256_set1_ps(camera.position.y);
            __m256 cam_pos_z = _mm256_set1_ps(camera.position.z);
            
            __m256i even_check = _mm256_set1_epi32(0);
            #pragma omp parallel for
            for (int y = 0; y < height; y++) {
                float y_pos[8];
                for (int b = 0; b < 8; b++) {
                    y_pos[b] = (float)y;
                }
                __m256 y_pos_v = _mm256_loadu_ps(y_pos);
                
                for (int x = 0; x < width; x += RAYS_PER_BATCH) {
                    if ((x + y + frame) % 2 == 0) {
                        if (x + 1 < width && (x + 1 + y + frame) % 2 == 0) {
                            x++; 
                        }
                        continue;
                    }
                    
                    float x_pos[8];
                    for (int i = 0; i < RAYS_PER_BATCH && x + i < width; i++) {
                        x_pos[i] = (float)(x + i);
                    }
                    __m256 x_pos_v = _mm256_loadu_ps(x_pos);
                    
                    __m256 dirs[3];
                    GetScreenToWorldRayOptimized_SIMD(x_pos_v, y_pos_v, camera, width, height, 
                                                       matView, viewInv, matProj, dirs);
                    
                    for (int i = 0; i < RAYS_PER_BATCH && x + i < width; i++) {
                        float dir_x = ((float*)&dirs[0])[i];
                        float dir_y = ((float*)&dirs[1])[i];
                        float dir_z = ((float*)&dirs[2])[i];
                        
                        int idx = (y * imageBuffer.width + (x + i)) * 4;
                        
                        ((unsigned char*)imageBuffer.data)[idx] = SKYBLUE.r;
                        ((unsigned char*)imageBuffer.data)[idx + 1] = SKYBLUE.g;
                        ((unsigned char*)imageBuffer.data)[idx + 2] = SKYBLUE.b;
                        
                        if (dir_y < 0.0f) {
                            int voxelX = (int)floorf(camera.position.x);
                            int voxelY = (int)floorf(camera.position.y);
                            int voxelZ = (int)floorf(camera.position.z);
                            
                            int stepX = (dir_x > 0.0f) - (dir_x < 0.0f);
                            int stepZ = (dir_z > 0.0f) - (dir_z < 0.0f);
                            
                            float tDeltaX = (dir_x != 0.0f) ? fabsf(1.0f / dir_x) : INFINITY;
                            float tDeltaY = fabsf(1.0f / dir_y);
                            float tDeltaZ = (dir_z != 0.0f) ? fabsf(1.0f / dir_z) : INFINITY;
                            
                            float tMaxX = (dir_x > 0.0f) ? (((voxelX + 1) - camera.position.x) / dir_x) :
                                          (dir_x < 0.0f) ? ((voxelX - camera.position.x) / dir_x) : INFINITY;
                            float tMaxY = (voxelY - camera.position.y) / dir_y;
                            float tMaxZ = (dir_z > 0.0f) ? (((voxelZ + 1) - camera.position.z) / dir_z) :
                                          (dir_z < 0.0f) ? ((voxelZ - camera.position.z) / dir_z) : INFINITY;
                            
                            int steps = 0;
                            while (steps < 1000) {
                                if (tMaxX < tMaxY && tMaxX < tMaxZ) {
                                    voxelX += stepX;
                                    tMaxX += tDeltaX;
                                } else if (tMaxZ < tMaxY) {
                                    voxelZ += stepZ;
                                    tMaxZ += tDeltaZ;
                                } else {
                                    voxelY--;
                                    tMaxY += tDeltaY;
                                }
                                
                                if (voxelY <= 0) {
                                    if ((voxelX & 1) == 0 || (voxelZ & 1) == 0) {
                                        ((unsigned char*)imageBuffer.data)[idx] = WHITE.r;
                                        ((unsigned char*)imageBuffer.data)[idx + 1] = WHITE.g;
                                        ((unsigned char*)imageBuffer.data)[idx + 2] = WHITE.b;
                                    } else {
                                        ((unsigned char*)imageBuffer.data)[idx] = BLACK.r;
                                        ((unsigned char*)imageBuffer.data)[idx + 1] = BLACK.g;
                                        ((unsigned char*)imageBuffer.data)[idx + 2] = BLACK.b;
                                    }
                                    break;
                                }
                                steps++;
                            }
                        }
                    }
                }
            }
            
            UpdateTexture(displayBuffer, imageBuffer.data);
            DrawTexture(displayBuffer, 0, 0, WHITE);
            UpdateCamera(&camera, CAMERA_FREE);
            DrawFPS(0, 0);
            EndDrawing();
        }
    }
};

int main() {
    App app;
    app.Run();
}
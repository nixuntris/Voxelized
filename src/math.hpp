#pragma once
#include <algorithm>
#include <immintrin.h>
#include <raymath.h>
#include "raylib.h"
#define STEP(d, size) (((d) > 1 && (d) != 255) ? (((d) - 1) * (size)) : 0.0f)
#define DIRECTION_DELTA(d) ([&]() {               \
    const float dx = (d).x - direction.x;         \
    const float dy = (d).y - direction.y;         \
    const float dz = (d).z - direction.z;         \
    return float(dx * dx + dy * dy + dz * dz);    \
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
#define FAST_RANDOM_3(x_, y_, frame_)                         \
({                                                            \
    uint32_t _h = (uint32_t)(x_) ^                            \
                  ((uint32_t)(y_) * 0x9e3779b9u) ^            \
                  ((uint32_t)(frame_) * 0x85ebca6bu);         \
    _h ^= _h >> 16;                                           \
    _h *= 0x45d9f3bu;                                        \
    _h ^= _h >> 16;                                           \
    _h % 3u;                                                  \
})
#define RAND01(s) (((s = s * 1664525u + 1013904223u) >> 8) * (1.0f / 16777215.0f))
#define IDX(x, y, z, size) \
    ((int)(x) * (int)(size) * (int)(size) + \
     (int)(y) * (int)(size) + (int)(z)) 
#define WIDX(x, y, z, size, height) \
    ((int)(x) * (int)(height) * (int)(size) + \
     (int)(y) * (int)(size) + \
     (int)(z))
struct IVector3 {
    int x,y,z;
};

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
    __m256 wx = _mm256_fmadd_ps(m4, viewY,_mm256_fmadd_ps(m0, viewX, _mm256_sub_ps(_mm256_setzero_ps(), m8)));
    __m256 wy = _mm256_fmadd_ps(m5, viewY,_mm256_fmadd_ps(m1, viewX, _mm256_sub_ps(_mm256_setzero_ps(), m9)));
    __m256 wz = _mm256_fmadd_ps(m6, viewY,_mm256_fmadd_ps(m2, viewX, _mm256_sub_ps(_mm256_setzero_ps(), m10)));
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
inline static Vector2 GetWorldToScreenOptimized(
    Vector3 p,
    const Matrix& view)
{
    float viewX =view.m0 * p.x +view.m4 * p.y +view.m8 * p.z +view.m12;
    float viewY =view.m1 * p.x +view.m5 * p.y +view.m9 * p.z +view.m13;
    float viewZ =view.m2 * p.x +view.m6 * p.y +view.m10 * p.z +view.m14;

    float invW = -1.0f / viewZ;

    float projScale = 1.73205f;

    float ndcX = viewX * projScale * invW;
    float ndcY = -viewY * projScale * invW;

    return {
        (ndcX + 1.0f) * 400,
        (ndcY + 1.0f) * 400
    };
}

static unsigned char stb__perlin_randtab[512] =
{
   23, 125, 161, 52, 103, 117, 70, 37, 247, 101, 203, 169, 124, 126, 44, 123,
   152, 238, 145, 45, 171, 114, 253, 10, 192, 136, 4, 157, 249, 30, 35, 72,
   175, 63, 77, 90, 181, 16, 96, 111, 133, 104, 75, 162, 93, 56, 66, 240,
   8, 50, 84, 229, 49, 210, 173, 239, 141, 1, 87, 18, 2, 198, 143, 57,
   225, 160, 58, 217, 168, 206, 245, 204, 199, 6, 73, 60, 20, 230, 211, 233,
   94, 200, 88, 9, 74, 155, 33, 15, 219, 130, 226, 202, 83, 236, 42, 172,
   165, 218, 55, 222, 46, 107, 98, 154, 109, 67, 196, 178, 127, 158, 13, 243,
   65, 79, 166, 248, 25, 224, 115, 80, 68, 51, 184, 128, 232, 208, 151, 122,
   26, 212, 105, 43, 179, 213, 235, 148, 146, 89, 14, 195, 28, 78, 112, 76,
   250, 47, 24, 251, 140, 108, 186, 190, 228, 170, 183, 139, 39, 188, 244, 246,
   132, 48, 119, 144, 180, 138, 134, 193, 82, 182, 120, 121, 86, 220, 209, 3,
   91, 241, 149, 85, 205, 150, 113, 216, 31, 100, 41, 164, 177, 214, 153, 231,
   38, 71, 185, 174, 97, 201, 29, 95, 7, 92, 54, 254, 191, 118, 34, 221,
   131, 11, 163, 99, 234, 81, 227, 147, 156, 176, 17, 142, 69, 12, 110, 62,
   27, 255, 0, 194, 59, 116, 242, 252, 19, 21, 187, 53, 207, 129, 64, 135,
   61, 40, 167, 237, 102, 223, 106, 159, 197, 189, 215, 137, 36, 32, 22, 5,

   23, 125, 161, 52, 103, 117, 70, 37, 247, 101, 203, 169, 124, 126, 44, 123,
   152, 238, 145, 45, 171, 114, 253, 10, 192, 136, 4, 157, 249, 30, 35, 72,
   175, 63, 77, 90, 181, 16, 96, 111, 133, 104, 75, 162, 93, 56, 66, 240,
   8, 50, 84, 229, 49, 210, 173, 239, 141, 1, 87, 18, 2, 198, 143, 57,
   225, 160, 58, 217, 168, 206, 245, 204, 199, 6, 73, 60, 20, 230, 211, 233,
   94, 200, 88, 9, 74, 155, 33, 15, 219, 130, 226, 202, 83, 236, 42, 172,
   165, 218, 55, 222, 46, 107, 98, 154, 109, 67, 196, 178, 127, 158, 13, 243,
   65, 79, 166, 248, 25, 224, 115, 80, 68, 51, 184, 128, 232, 208, 151, 122,
   26, 212, 105, 43, 179, 213, 235, 148, 146, 89, 14, 195, 28, 78, 112, 76,
   250, 47, 24, 251, 140, 108, 186, 190, 228, 170, 183, 139, 39, 188, 244, 246,
   132, 48, 119, 144, 180, 138, 134, 193, 82, 182, 120, 121, 86, 220, 209, 3,
   91, 241, 149, 85, 205, 150, 113, 216, 31, 100, 41, 164, 177, 214, 153, 231,
   38, 71, 185, 174, 97, 201, 29, 95, 7, 92, 54, 254, 191, 118, 34, 221,
   131, 11, 163, 99, 234, 81, 227, 147, 156, 176, 17, 142, 69, 12, 110, 62,
   27, 255, 0, 194, 59, 116, 242, 252, 19, 21, 187, 53, 207, 129, 64, 135,
   61, 40, 167, 237, 102, 223, 106, 159, 197, 189, 215, 137, 36, 32, 22, 5,
};


static unsigned char stb__perlin_randtab_grad_idx[512] =
{
    7, 9, 5, 0, 11, 1, 6, 9, 3, 9, 11, 1, 8, 10, 4, 7,
    8, 6, 1, 5, 3, 10, 9, 10, 0, 8, 4, 1, 5, 2, 7, 8,
    7, 11, 9, 10, 1, 0, 4, 7, 5, 0, 11, 6, 1, 4, 2, 8,
    8, 10, 4, 9, 9, 2, 5, 7, 9, 1, 7, 2, 2, 6, 11, 5,
    5, 4, 6, 9, 0, 1, 1, 0, 7, 6, 9, 8, 4, 10, 3, 1,
    2, 8, 8, 9, 10, 11, 5, 11, 11, 2, 6, 10, 3, 4, 2, 4,
    9, 10, 3, 2, 6, 3, 6, 10, 5, 3, 4, 10, 11, 2, 9, 11,
    1, 11, 10, 4, 9, 4, 11, 0, 4, 11, 4, 0, 0, 0, 7, 6,
    10, 4, 1, 3, 11, 5, 3, 4, 2, 9, 1, 3, 0, 1, 8, 0,
    6, 7, 8, 7, 0, 4, 6, 10, 8, 2, 3, 11, 11, 8, 0, 2,
    4, 8, 3, 0, 0, 10, 6, 1, 2, 2, 4, 5, 6, 0, 1, 3,
    11, 9, 5, 5, 9, 6, 9, 8, 3, 8, 1, 8, 9, 6, 9, 11,
    10, 7, 5, 6, 5, 9, 1, 3, 7, 0, 2, 10, 11, 2, 6, 1,
    3, 11, 7, 7, 2, 1, 7, 3, 0, 8, 1, 1, 5, 0, 6, 10,
    11, 11, 0, 2, 7, 0, 10, 8, 3, 5, 7, 1, 11, 1, 0, 7,
    9, 0, 11, 5, 10, 3, 2, 3, 5, 9, 7, 9, 8, 4, 6, 5,

    7, 9, 5, 0, 11, 1, 6, 9, 3, 9, 11, 1, 8, 10, 4, 7,
    8, 6, 1, 5, 3, 10, 9, 10, 0, 8, 4, 1, 5, 2, 7, 8,
    7, 11, 9, 10, 1, 0, 4, 7, 5, 0, 11, 6, 1, 4, 2, 8,
    8, 10, 4, 9, 9, 2, 5, 7, 9, 1, 7, 2, 2, 6, 11, 5,
    5, 4, 6, 9, 0, 1, 1, 0, 7, 6, 9, 8, 4, 10, 3, 1,
    2, 8, 8, 9, 10, 11, 5, 11, 11, 2, 6, 10, 3, 4, 2, 4,
    9, 10, 3, 2, 6, 3, 6, 10, 5, 3, 4, 10, 11, 2, 9, 11,
    1, 11, 10, 4, 9, 4, 11, 0, 4, 11, 4, 0, 0, 0, 7, 6,
    10, 4, 1, 3, 11, 5, 3, 4, 2, 9, 1, 3, 0, 1, 8, 0,
    6, 7, 8, 7, 0, 4, 6, 10, 8, 2, 3, 11, 11, 8, 0, 2,
    4, 8, 3, 0, 0, 10, 6, 1, 2, 2, 4, 5, 6, 0, 1, 3,
    11, 9, 5, 5, 9, 6, 9, 8, 3, 8, 1, 8, 9, 6, 9, 11,
    10, 7, 5, 6, 5, 9, 1, 3, 7, 0, 2, 10, 11, 2, 6, 1,
    3, 11, 7, 7, 2, 1, 7, 3, 0, 8, 1, 1, 5, 0, 6, 10,
    11, 11, 0, 2, 7, 0, 10, 8, 3, 5, 7, 1, 11, 1, 0, 7,
    9, 0, 11, 5, 10, 3, 2, 3, 5, 9, 7, 9, 8, 4, 6, 5,
};

#define LERP(a, b, t) (a + (b-a) * t) 
#define FASTFLOOR(a) ((int)(a) - ((a) < (int)(a) ? 1 : 0))

const float basis[12][4] =
{
   {  1, 1, 0 },
   { -1, 1, 0 },
   {  1,-1, 0 },
   { -1,-1, 0 },
   {  1, 0, 1 },
   { -1, 0, 1 },
   {  1, 0,-1 },
   { -1, 0,-1 },
   {  0, 1, 1 },
   {  0,-1, 1 },
   {  0, 1,-1 },
   {  0,-1,-1 },
};
#define GRAD(grad_idx, x, y, z) \
    (basis[grad_idx][0] * (x) + basis[grad_idx][1] * (y) + basis[grad_idx][2] * (z))

inline float stb_perlin_noise3_internal(float x, float y, float z, unsigned char seed)
{
   float u,v,w;
   float n000,n001,n010,n011,n100,n101,n110,n111;
   float n00,n01,n10,n11;
   float n0,n1;

   int px = FASTFLOOR(x);
   int py = FASTFLOOR(y);
   int pz = FASTFLOOR(z);
   int x0 = px & 255, x1 = (px+1) & 255;
   int y0 = py & 255, y1 = (py+1) & 255;
   int z0 = pz & 255, z1 = (pz+1) & 255;
   int r0,r1, r00,r01,r10,r11;

   #define stb__perlin_ease(a)   (((a*6-15)*a + 10) * a * a * a)

   x -= px; u = stb__perlin_ease(x);
   y -= py; v = stb__perlin_ease(y);
   z -= pz; w = stb__perlin_ease(z);

   r0 = stb__perlin_randtab[x0+seed];
   r1 = stb__perlin_randtab[x1+seed];

   r00 = stb__perlin_randtab[r0+y0];
   r01 = stb__perlin_randtab[r0+y1];
   r10 = stb__perlin_randtab[r1+y0];
   r11 = stb__perlin_randtab[r1+y1];

   n000 = GRAD(stb__perlin_randtab_grad_idx[r00+z0], x  , y  , z   );
   n001 = GRAD(stb__perlin_randtab_grad_idx[r00+z1], x  , y  , z-1 );
   n010 = GRAD(stb__perlin_randtab_grad_idx[r01+z0], x  , y-1, z   );
   n011 = GRAD(stb__perlin_randtab_grad_idx[r01+z1], x  , y-1, z-1 );
   n100 = GRAD(stb__perlin_randtab_grad_idx[r10+z0], x-1, y  , z   );
   n101 = GRAD(stb__perlin_randtab_grad_idx[r10+z1], x-1, y  , z-1 );
   n110 = GRAD(stb__perlin_randtab_grad_idx[r11+z0], x-1, y-1, z   );
   n111 = GRAD(stb__perlin_randtab_grad_idx[r11+z1], x-1, y-1, z-1 );

   n00 = LERP(n000,n001,w);
   n01 = LERP(n010,n011,w);
   n10 = LERP(n100,n101,w);
   n11 = LERP(n110,n111,w);

   n0 = LERP(n00,n01,v);
   n1 = LERP(n10,n11,v);

   return LERP(n0,n1,u);
}
inline uint8_t* GenImagePerlinNoiseOptimized(int width, int height, int offsetX, int offsetY, float scale)
{
    uint8_t *pixels = (uint8_t*)RL_MALLOC(width*height*sizeof(uint8_t));

    float aspectRatio = (float)width/(float)height;
    for (int y = 0; y < height; y+=2)
    {
        for (int x = 0; x < width; x+=2)
        {
            float nx = (float)(x + offsetX)*(scale/(float)width);
            float ny = (float)(y + offsetY)*(scale/(float)height);
            if (width > height) nx *= aspectRatio;
            else ny /= aspectRatio;
                        
            int i;
            float frequency = 1.0f;
            float amplitude = 1.0f;
            float sum = 0.0f;

            for (i = 0; i < 6; i++) {
                sum += stb_perlin_noise3_internal(nx*frequency,ny*frequency,frequency,(unsigned char)i)*amplitude;
                frequency *= 2;
                amplitude *= 0.5;
            }
            float p = sum;
            if (p < -1.0f) p = -1.0f;
            if (p > 1.0f) p = 1.0f;
            float np = (p + 1.0f)/2.0f;

            unsigned char intensity = (unsigned char)(np*255.0f);
            pixels[y*width + x] = intensity;
        }
    }

    #pragma omp parallel for
    for (int y = 0; y < height; y += 2)
    {
        for (int x = 1; x < width - 1; x += 2)
        {
            pixels[y*width + x] =
                ((int)pixels[y*width + x - 1] +
                (int)pixels[y*width + x + 1]) / 2;
        }

        if ((width - 1)%2 == 1)
        {
            int x = width - 1;

            float nx = (float)(x + 1 + offsetX)*(scale/(float)width);
            float ny = (float)(y + offsetY)*(scale/(float)height);
            if (width > height) nx *= aspectRatio;
            else ny /= aspectRatio;

            float frequency = 1.0f;
            float amplitude = 1.0f;
            float sum = 0.0f;

            for (int i = 0; i < 6; i++) {
                sum += stb_perlin_noise3_internal(nx*frequency,ny*frequency,frequency,(unsigned char)i)*amplitude;
                frequency *= 2;
                amplitude *= 0.5;
            }

            float p = sum;
            if (p < -1.0f) p = -1.0f;
            if (p > 1.0f) p = 1.0f;
            float np = (p + 1.0f)/2.0f;

            unsigned char nextIntensity = (unsigned char)(np*255.0f);

            pixels[y*width + x] =
                ((int)pixels[y*width + x - 1] +
                (int)nextIntensity) / 2;
        }
    }

    #pragma omp parallel for
    for (int y = 1; y < height - 1; y += 2)
    {
        for (int x = 0; x < width; x++)
        {
            pixels[y*width + x] =
                ((int)pixels[(y - 1)*width + x] +
                (int)pixels[(y + 1)*width + x]) / 2;
        }
    }

    if ((height - 1)%2 == 1)
    {
        int y = height - 1;

        #pragma omp parallel for
        for (int x = 0; x < width; x++)
        {
            float nx = (float)(x + offsetX)*(scale/(float)width);
            float ny = (float)(y + 1 + offsetY)*(scale/(float)height);
            if (width > height) nx *= aspectRatio;
            else ny /= aspectRatio;

            float frequency = 1.0f;
            float amplitude = 1.0f;
            float sum = 0.0f;

            for (int i = 0; i < 6; i++) {
                sum += stb_perlin_noise3_internal(nx*frequency,ny*frequency,frequency,(unsigned char)i)*amplitude;
                frequency *= 2;
                amplitude *= 0.5;
            }

            float p = sum;
            if (p < -1.0f) p = -1.0f;
            if (p > 1.0f) p = 1.0f;
            float np = (p + 1.0f)/2.0f;

            unsigned char nextIntensity = (unsigned char)(np*255.0f);

            pixels[y*width + x] =
                ((int)pixels[(y - 1)*width + x] +
                (int)nextIntensity) / 2;
        }
    }

    return pixels;
}

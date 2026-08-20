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
#define IDX(x, y, z, size) \
    ((int)(x) * (int)(size) * (int)(size) + \
     (int)(y) * (int)(size) + (int)(z)) 
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
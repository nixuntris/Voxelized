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
#include <limits>
#include "world.hpp"
#include "math.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <utility>
#include "gui.hpp"
#include <algorithm>
using Clock = std::chrono::steady_clock;
#include "render.hpp"
int baseFPS = 100;
bool has_avx2()
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2");
#else
    return false;
#endif
}

class App {
    public:
    int prevFPS = baseFPS;
    Camera camera;
    Matrix matProj;
    World *world;
    std::atomic<int> worldFinished{0};
    std::atomic<int> chunkFinished{0};
    std::atomic<int> generatingColumnX{-1};
    std::atomic<int> generatingColumnZ{-1};
    std::thread worker;
    std::thread chunkWorker;
    Viewport renderPort;
    int frame = 0;
    WorldType worldType = WORLD_PLAINS;
    std::vector<std::pair<int, int>> generationOrder;
    size_t nextColumnToGenerate = 0;
    size_t nextColumnToFinalize = 0;
    Matrix matView;
    App() {
        InitWindow(width*SCALE,height*SCALE,"Voxelized");
        std::cout<<LOD4_START<<" "<<LOD8_START<<" "<<LOD16_START<<" "<<LOD32_START<<"\n";
        camera.target = (Vector3){ 0.0f, 2.0f, 0.0f };
        camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
        camera.fovy = FOVY;
        camera.projection = CAMERA_PERSPECTIVE;
        matProj = MatrixIdentity();
        matProj = MatrixPerspective(camera.fovy*DEG2RAD, ((double)width/(double)height), 0.01f, 10000.0f);
        world = new World;
        renderPort.Init(world);
        world->cloudNoise =  GenImagePerlinNoiseOptimized(1024,1024,0,0,16);
        world->cloudHeight = GenImagePerlinNoiseOptimized(1024,1024,0,0,64);
        camera.position = {(float)WORLD_WIDTH/2,WORLD_HEIGHT/2,(float)WORLD_DEPTH/2};

    }
    
    void Run() {
        int dvdX = 0;
        int dvdY = 0;
        int dvdXChange = 1;
        int dvdYChange = 1;
        int gui = 0;
        const uint8_t transparency = 200;
        const Color BLACK_GUI_COLOR = {0,0,0,transparency};
        const Color HOVER_COLOR = {GRAY.r,GRAY.g,GRAY.b,50};
        VX_GUI::Button returnButton(250,220,300,60,BLACK_GUI_COLOR,3,BLACK_GUI_COLOR,HOVER_COLOR);
        VX_GUI::Label returnLabel(133.5f,115,150,30,"Return",12,TRANSPARENT,BLACK_GUI_COLOR);
        VX_GUI::Button mainMenuButton(250,220+90,300,60,BLACK_GUI_COLOR,3,BLACK_GUI_COLOR,HOVER_COLOR);
        VX_GUI::Label mainMenuLabel(133.5f,160,150,30,"Main Menu",12,TRANSPARENT,BLACK_GUI_COLOR);
        VX_GUI::Button graphicsButton(250,220+180,300,60,BLACK_GUI_COLOR,3,BLACK_GUI_COLOR,HOVER_COLOR);
        VX_GUI::Label graphicsLabel(133.5f,205,150,30,"Graphics",12,TRANSPARENT,BLACK_GUI_COLOR);
        VX_GUI::Button quitButton(250,220+270,300,60,BLACK_GUI_COLOR,3,BLACK_GUI_COLOR,HOVER_COLOR);
        VX_GUI::Label quitLabel(133.5f,250,150,30,"Quit",12,TRANSPARENT,BLACK_GUI_COLOR);
        VX_GUI::Button graphicsReturnButton(250,220-90,300,60,BLACK_GUI_COLOR,3,BLACK_GUI_COLOR,HOVER_COLOR);
        VX_GUI::Label graphicsReturnLabel(133.5f,70,150,30,"Return",12,TRANSPARENT,BLACK_GUI_COLOR);
        VX_GUI::Button nativeButton(250,220,300,60,BLACK_GUI_COLOR,3,BLACK_GUI_COLOR,HOVER_COLOR);
        VX_GUI::Label nativeLabel(133.5f,115,150,30,"Native",12,TRANSPARENT,BLACK_GUI_COLOR);
        VX_GUI::Button eightyButton(250,220+90,300,60,BLACK_GUI_COLOR,3,BLACK_GUI_COLOR,HOVER_COLOR);
        VX_GUI::Label eightyLabel(133.5f,160,150,30,"80%",12,TRANSPARENT,BLACK_GUI_COLOR);
        VX_GUI::Button sixtySixButton(250,220+180,300,60,BLACK_GUI_COLOR,3,BLACK_GUI_COLOR,HOVER_COLOR);
        VX_GUI::Label sixtySixLabel(133.5f,205,150,30,"66%",12,TRANSPARENT,BLACK_GUI_COLOR);
        VX_GUI::Button createWorldButton(0,200,200,50,WHITE,3,BLACK,HOVER_COLOR);
        VX_GUI::Label createWorldLabel(-4,96,100,25,"Create World",12,TRANSPARENT,BLACK_GUI_COLOR);
        while (!WindowShouldClose()) {
            world->lightSources[0].position.x += sinf((float)frame/100.0f)*0.5f;
            world->lightSources[0].position.z += cosf((float)frame/100.0f)*0.5f;

            world->lightSources[1].position.x += sinf((float)frame/100.0f)*0.5f;
            world->lightSources[1].position.z += cosf((float)frame/100.0f)*0.5f;
            
            BeginDrawing();
            ClearBackground(WHITE);
            frame++;
            if (worldFinished==2) {
                if (IsKeyPressed(KEY_E)) {
                    if (gui==0) {
                        gui = 2;
                        EnableCursor();
                        SetTargetFPS(60);
                    }
                    else {
                        gui = 0;
                        DisableCursor(); 
                        SetTargetFPS(-1);  
                    }
                }
                if (gui==0) {    
                                
                    Vector3 oldCameraTarget = camera.target;
                    UpdateCamera(&camera, CAMERA_FREE);
                    renderPort.cameraMoved = false;
                    if (
                    oldCameraTarget.x!=camera.target.x ||
                    oldCameraTarget.y!=camera.target.y || 
                    oldCameraTarget.z!=camera.target.z) {
                        renderPort.cameraMoved = true;
                    }
                    for (int i = 0; i<world->lightSourceCount; i++) {
                        int cx = world->lightSources[i].position.x/32;
                        int cy = world->lightSources[i].position.y/32;
                        int cz = world->lightSources[i].position.z/32;
                        int radius = 50;
                        int chunkMin = -radius/32;
                        int chunkMax = radius/32;
                        for (int x = chunkMin; x <= chunkMax; x++) {
                            for (int y = chunkMin; y <= chunkMax; y++) {
                                for (int z = chunkMin; z <= chunkMax; z++) {          
                                    if (cx+x<0 || cy+y<0 || cz+z<0 || cx+x>=WORLD_WIDTH/32 || cy+y>=WORLD_HEIGHT/32 || cz+z>=WORLD_DEPTH/32) continue;
                                    world->voxelChunks[cx+x][cy+y][cz+z].lightSources[world->voxelChunks[cx+x][cy+y][cz+z].lightIdCount++] = &world->lightSources[i];
                                }
                            }
                        }
                    }
                    renderPort.Render(camera,&generatingColumnX,&generatingColumnZ);
                    for (int i = 0; i<world->lightSourceCount; i++) {
                        int cx = world->lightSources[i].position.x/32;
                        int cy = world->lightSources[i].position.y/32;
                        int cz = world->lightSources[i].position.z/32;
                        int radius = 50;
                        int chunkMin = -radius/32;
                        int chunkMax = radius/32;
                        for (int x = chunkMin; x <= chunkMax; x++) {
                            for (int y = chunkMin; y <= chunkMax; y++) {
                                for (int z = chunkMin; z <= chunkMax; z++) {

                                    if (cx+x<0 || cy+y<0 || cz+z<0 || cx+x>=WORLD_WIDTH/32 || cy+y>=WORLD_HEIGHT/32 || cz+z>=WORLD_DEPTH/32) continue;
                                    world->voxelChunks[cx+x][cy+y][cz+z].lightIdCount = 0;
                                }
                            }
                        }
                    }

                }
                        
                UpdateTexture(renderPort.displayBuffer, renderPort.imageBuffer.data);
                        
                UpdateTexture(renderPort.cloudBuffer, renderPort.imageCloudBuffer.data);
                        
                DrawTexturePro(renderPort.displayBuffer, 
                    (Rectangle){0, 0, (float)width, (float)height},
                    (Rectangle){0, 0, width*SCALE, height*SCALE},
                    (Vector2){0, 0}, 0, WHITE);
                if (renderClouds) {
                    DrawTexturePro(renderPort.cloudBuffer, 
                    (Rectangle){0, 0, (float)200, (float)200},
                    (Rectangle){0, 0, 800, 800},
                    (Vector2){0, 0}, 0, WHITE);
                
                }    
                     
                BeginMode3D(camera);
                for (int i = 0; i < world->lightSourceCount; i++) {
                    DrawSphere(world->lightSources[i].position, 1.0f, YELLOW);
                }
                EndMode3D();   
                DrawFPS(0, 0);
                if (gui==2) {
                    if (returnButton.Update(0,0,1,1)) {
                        gui = 0;
                        DisableCursor();
                        SetTargetFPS(-1);
                    }
                    else if (graphicsButton.Update(0,0,1,1)) {
                        gui = 1;
                    }
                    else if (mainMenuButton.Update(0,0,1,1)) {
                        if (worker.joinable()) {
                            worker.join();
                        }
                        if (chunkWorker.joinable()) {
                            chunkWorker.join();
                        }
                        generatingColumnX.store(-1, std::memory_order_release);
                        generatingColumnZ.store(-1, std::memory_order_release);
                        chunkFinished.store(0, std::memory_order_release);
                        world->Reset();
                        generationOrder.clear();
                        nextColumnToGenerate = 0;
                        nextColumnToFinalize = 0;
                        worldFinished = 0;
                        gui = 0;
                    }
                    else if (quitButton.Update(0,0,1,1)) {
                        CloseWindow();
                    }
                    returnLabel.Update(0,0,2,2);
                    mainMenuLabel.Update(0,0,2,2);
                    graphicsLabel.Update(0,0,2,2);
                    quitLabel.Update(0,0,2,2);

                }
                else if (gui==1) {
                    if (graphicsReturnButton.Update(0,0,1,1)) {
                        gui = 2;
                    }
                    if (nativeButton.Update(0,0,1,1)) {
                        SCALE = 1;
                        width = 800/SCALE;
                        height = 800/SCALE;
                        renderPort.cameraMoved = true;
                        renderPort.Render(camera,&generatingColumnX,&generatingColumnZ);
                    }
                    if (eightyButton.Update(0,0,1,1)) {
                        SCALE = 1.3;
                        width = 800/SCALE;
                        height = 800/SCALE;
                        renderPort.cameraMoved = true;
                        renderPort.Render(camera,&generatingColumnX,&generatingColumnZ);
                    }
                    if (sixtySixButton.Update(0,0,1,1)) {
                        SCALE = 1.5;
                        width = 800/SCALE;
                        height = 800/SCALE;
                        renderPort.cameraMoved = true;
                        renderPort.Render(camera,&generatingColumnX,&generatingColumnZ);
                    }
                    graphicsReturnLabel.Update(0,0,2,2);
                    nativeLabel.Update(0,0,2,2);
                    eightyLabel.Update(0,0,2,2);
                    sixtySixLabel.Update(0,0,2,2);
                }
                if (chunkFinished.load(std::memory_order_acquire) == 2) {
                    if (chunkWorker.joinable()) {
                        chunkWorker.join();
                    }
                    generatingColumnX.store(-1, std::memory_order_release);
                    generatingColumnZ.store(-1, std::memory_order_release);
                    chunkFinished.store(0, std::memory_order_release);
                    nextColumnToGenerate++;
                    generatedChunks++;
                }
                if (nextColumnToGenerate < generationOrder.size()) {
                    if (frame%2==0 && chunkFinished.load(std::memory_order_acquire) == 0) {
                        size_t candidate = generationOrder.size();
                        float bestDistance = std::numeric_limits<float>::max();
                        for (size_t i = nextColumnToGenerate; i < generationOrder.size(); i++) {
                            const int x = generationOrder[i].first;
                            const int z = generationOrder[i].second;

                            float dx = x * 32.0f + 16.0f - camera.position.x;
                            float dz = z * 32.0f + 16.0f - camera.position.z;
                            float distance = dx * dx + dz * dz;

                            if (distance <= RENDERDISTANCE * RENDERDISTANCE &&
                                distance < bestDistance &&
                                ColumnInFrustum(x, z, camera.position, matView, matProj)) {
                                candidate = i;
                                bestDistance = distance;
                            }
                        }
                        if (candidate != generationOrder.size()) {
                            std::swap(
                                generationOrder[nextColumnToGenerate],
                                generationOrder[candidate]
                            );
                            const int x = generationOrder[nextColumnToGenerate].first;
                            const int z = generationOrder[nextColumnToGenerate].second;
                            const Vector3 generationCameraPosition = camera.position;

                            generatingColumnX.store(x, std::memory_order_release);
                            generatingColumnZ.store(z, std::memory_order_release);
                            chunkFinished.store(1, std::memory_order_release);
                            chunkWorker = std::thread([this, x, z, generationCameraPosition]() {
                                using Clock = std::chrono::high_resolution_clock;

                                auto t0 = Clock::now();

                                world->InitColumn(generationCameraPosition, x, z);

                                auto t1 = Clock::now();

                                world->GenerateTerrain(world->chunkBiome[x][z], x, z);

                                auto t2 = Clock::now();

                                world->BuildDistanceToClosestVoxel(x, z);

                                auto t3 = Clock::now();

                                world->BuildDistanceLayerBaseline(x, z);

                                auto t4 = Clock::now();

                                world->BuildDistanceLayer(x, z, 8);

                                auto t5 = Clock::now();

                                world->BuildDistanceLayer(x, z, 4);

                                auto t6 = Clock::now();

                                for (int y = 0; y < WORLD_HEIGHT / 32; y++) {
                                    world->voxelChunks[x][y][z].CheckOriginals(
                                        world->traversalChunks[x][y][z].buildID
                                    );
                                }

                                auto t7 = Clock::now();

                                world->GenerateOccupancyMasks(x, z);

                                auto t8 = Clock::now();

                                auto ms = [](auto start, auto end) {
                                    return std::chrono::duration<double, std::milli>(end - start).count();
                                };

                                std::cout
                                    << "Column [" << x << ", " << z << "]\n"
                                    << "InitColumn:                     " << ms(t0, t1) << " ms\n"
                                    << "GenerateTerrain:                " << ms(t1, t2) << " ms\n"
                                    << "BuildDistanceToClosestVoxel:    " << ms(t2, t3) << " ms\n"
                                    << "BuildDistanceLayerBaseline:     " << ms(t3, t4) << " ms\n"
                                    << "BuildDistanceLayer 8:           " << ms(t4, t5) << " ms\n"
                                    << "BuildDistanceLayer 4:           " << ms(t5, t6) << " ms\n"
                                    << "CheckOriginals:                 " << ms(t6, t7) << " ms\n"
                                    << "GenerateOccupancyMasks:         " << ms(t7, t8) << " ms\n"
                                    << "TOTAL:                          " << ms(t0, t8) << " ms\n\n";
                                chunkFinished.store(2, std::memory_order_release);
                            });
                        }
                    }
                    
                }
                else if (chunkFinished.load(std::memory_order_acquire) == 0 && nextColumnToFinalize < generationOrder.size()) {
                    const int x = generationOrder[nextColumnToFinalize].first;
                    const int z = generationOrder[nextColumnToFinalize].second;

                    for (int y = 0; y < WORLD_HEIGHT / 32; y++) {
                        world->traversalChunks[x][y][z].CheckDelta(
                            world->traversalChunks[x][y][z].buildID
                        );
                    }

                    nextColumnToFinalize++;
                }
                EndDrawing();
                
                
            }
            else if (worldFinished==1) {
                if (dvdX>width || dvdX<0) dvdXChange *= -1;
                if (dvdY>height || dvdY<0) dvdYChange *= -1; 
                dvdX+=dvdXChange;
                dvdY+=dvdYChange;
                DrawRectangle(dvdX,dvdY,32,32,BLACK);
                DisableCursor();
                DrawText("Generating the world, please wait!", 0,0,25,BLACK);
                EndDrawing();
            }
            else if (worldFinished==0) {
                
                bool createWorldPressed = createWorldButton.Update(0,0,1,1);
                createWorldLabel.Update(0,0,2,2);
                if (createWorldPressed) {
                    generationOrder.clear();
                    nextColumnToGenerate = 0;
                    nextColumnToFinalize = 0;
                    const int chunkCountX = WORLD_WIDTH / 32;
                    const int chunkCountZ = WORLD_DEPTH / 32;
                    const int cameraChunkX = std::max(0, std::min(chunkCountX - 1, (int)camera.position.x / 32));
                    const int cameraChunkZ = std::max(0, std::min(chunkCountZ - 1, (int)camera.position.z / 32));
                    generationOrder.reserve(chunkCountX * chunkCountZ);
                    for (int x = 0; x < chunkCountX; x++) {
                        for (int z = 0; z < chunkCountZ; z++) {
                            generationOrder.push_back({x, z});
                        }
                    }
                    std::sort(generationOrder.begin(), generationOrder.end(),
                        [cameraChunkX, cameraChunkZ](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                            const long long adx = a.first - cameraChunkX;
                            const long long adz = a.second - cameraChunkZ;
                            const long long bdx = b.first - cameraChunkX;
                            const long long bdz = b.second - cameraChunkZ;
                            return adx * adx + adz * adz < bdx * bdx + bdz * bdz;
                        });
                    worldFinished = 1;                     
                    generatedChunks = 0;
                    worker = std::thread([=]() {
                        world->InitOneType(WORLD_PLAINS);

                        world->lightSources[world->lightSourceCount].position = {WORLD_WIDTH/2.0f, 120, WORLD_DEPTH/2.0f};
                        world->lightSources[world->lightSourceCount].colorR = 255;
                        world->lightSources[world->lightSourceCount].colorG = 255;
                        world->lightSources[world->lightSourceCount].colorB = 255;
                        world->lightSources[world->lightSourceCount].intensity = 0.3f;
                        world->lightSourceCount++;
                        world->lightSources[world->lightSourceCount].position = {WORLD_WIDTH/2.0f+25, 120+25, WORLD_DEPTH/2.0f-25};
                        world->lightSources[world->lightSourceCount].colorR = 255;
                        world->lightSources[world->lightSourceCount].colorG = 255;
                        world->lightSources[world->lightSourceCount].colorB = 255;
                        world->lightSources[world->lightSourceCount].intensity = 0.3f;
                          
                        world->lightSourceCount++;
                        world->lightSourceCount = 2;
                        std::cout<<world->lightSourceCount<<"\n";
                        worldFinished.store(2);
                    });
                    DisableCursor();
                    SetTargetFPS(-1);
                }
                EndDrawing();
            }
        if (chunkWorker.joinable()) {
            chunkWorker.join();
        }
        if (worker.joinable()) {
            worker.join();
        }
        std::cout<<generatedChunks<<"\n";
    }
}


};

int main() {
    App *app = new App;
    app->Run();
}

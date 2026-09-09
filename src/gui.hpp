#pragma once
#include "raylib.h"
const Color TRANSPARENT = {0,0,0,0};
namespace VX_GUI {
    class Button {
        public:
        Rectangle rec;
        Color color;
        int borderRadius;
        Color borderColor;
        Color hoverColor;
        
        Button(float x, float y, float width, float height, Color color, int borderRadius=5, Color borderColor=TRANSPARENT, Color hoverColor={120,120,120,120}) {
            this->rec = {x,y,width,height};
            this->color = color;
            this->borderColor = borderColor;
            this->borderRadius = borderRadius;
            this->hoverColor = hoverColor;
        }
        bool Update(float offx, float offy, float wScale, float hScale) {
            DrawRectangle((this->rec.x-borderRadius)*wScale+offx,(this->rec.y-borderRadius)*hScale+offy,(this->rec.width+borderRadius*2)*wScale,(this->rec.height+borderRadius*2)*hScale, borderColor);
            DrawRectangle((this->rec.x)*wScale+offx,(this->rec.y)*hScale+offy,this->rec.width*wScale,this->rec.height*hScale,color);
            bool pressed = false;
            if (CheckCollisionPointRec(GetMousePosition(),{this->rec.x*wScale+offx,this->rec.y*hScale+offy,this->rec.width*wScale,this->rec.height*hScale})) {
                DrawRectangle(this->rec.x*wScale+offx,this->rec.y*hScale+offy,width*wScale,height*hScale,hoverColor);
                if (IsMouseButtonDown(0)) {
                    pressed = true;
                }
            }
            return pressed;
        }
    };
    class ResizableObject {
        public:
        Rectangle rec;
        Color color;
        int borderRadius;
        Color borderColor;
        bool picked;
        float scaleW;
        float scaleH;
        Vector2 originalSize;
        ResizableObject(float x, float y, float width, float height, Color color, int borderRadius=5, Color borderColor=TRANSPARENT) {
            this->rec = {x,y,width,height};
            this->color = color;
            this->originalSize = {width,height};
            this->borderColor = borderColor;
            this->borderRadius = borderRadius;
            picked = false;
        }
        void Update() {
            scaleW = this->originalSize.x/this->rec.x;
            scaleW = this->originalSize.y/this->rec.y;
            DrawRectangle(this->rec.x-borderRadius,this->rec.y-borderRadius,this->rec.width+borderRadius*2,this->rec.height+borderRadius*2, borderColor);
            DrawRectangle(this->rec.x,this->rec.y,this->rec.width,this->rec.height,color);
            if (CheckCollisionPointRec(GetMousePosition(),{this->rec.x+this->rec.width-16,this->rec.y+this->rec.height-16,32,32})) {
                if (IsMouseButtonDown(0)) {
                    picked = true;        
                }
            }
            if (!IsMouseButtonDown(0)) picked = false;
            if (picked) {
                Vector2 mouseDelta = GetMouseDelta();
                this->rec.width+=mouseDelta.x;
                this->rec.height+=mouseDelta.y;
            
            }
        }
    };
    void TestScene() {
        InitWindow(800,800,"gui test scene");
        Button button(20,20,50,50,WHITE,2,BLACK,{120,120,120,120});
        ResizableObject resizable(100,100,350,350,WHITE,2,BLACK);        
        while (!WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(WHITE);
            resizable.Update();
            button.Update(100,100,resizable.scaleW,resizable.scaleH);
            EndDrawing();
        }
    }
};
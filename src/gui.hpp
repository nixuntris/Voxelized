#pragma once
#include "raylib.h"
#include <variant>
#include <vector>
const Color TRANSPARENT = {0,0,0,0};
namespace VX_GUI {
    enum GUI_TYPE {
        LABEL,
        SLIDER,
        BUTTON,
        RESIZABLE
    };
    class Label {
        public:
        std::string text;
        Rectangle rec;
        Color color;
        Color textColor;
        Color borderColor;
        int borderRadius;
        int textSize;
        Label(float x, float y, float width, float height, std::string text, int textSize,Color color, Color textColor=BLACK, Color borderColor=TRANSPARENT, int borderRadius=5) {
            this->rec = {x,y,width,height};
            this->color = color;
            this->textColor = textColor;
            this->text = text;
            this->borderColor = borderColor;
            this->borderRadius = borderRadius;
            this->textSize = textSize;
        }
        void Update(float offx, float offy, float wScale, float hScale) {
            float scaledTextSize = this->textSize*hScale;
            DrawRectangle((this->rec.x-borderRadius)*wScale+offx,(this->rec.y-borderRadius)*hScale+offy,(this->rec.width+borderRadius*2)*wScale,(this->rec.height+borderRadius*2)*hScale, borderColor);
            DrawRectangle((this->rec.x)*wScale+offx,(this->rec.y)*hScale+offy,this->rec.width*wScale,this->rec.height*hScale,color);
            DrawText(this->text.c_str(),(this->rec.x+4)*wScale+offx,(this->rec.y+4)*hScale+offy,scaledTextSize,textColor);
        }
    };
    class Slider {
        public:
        int value;
        int maxVal;
        int minVal;
        Rectangle rec;
        Color color;
        int borderRadius;
        Color borderColor;
        Color sliderColor;
        Slider(float x, float y, float width, float height, Color color, int borderRadius, Color borderColor=TRANSPARENT, Color sliderColor=BLACK, int minVal=0,int maxVal=10) {
            this->rec = {x,y,width,height};
            this->color = color;
            this->borderColor = borderColor;
            this->sliderColor = sliderColor;
            this->borderRadius = borderRadius;
            this->maxVal = maxVal;
            this->minVal = minVal;
            this->value = 512;
        }
        bool Update(float offx, float offy, float wScale, float hScale) {
            DrawRectangle((this->rec.x-borderRadius)*wScale+offx,(this->rec.y-borderRadius)*hScale+offy,(this->rec.width+borderRadius*2)*wScale,(this->rec.height+borderRadius*2)*hScale, borderColor);
            DrawRectangle((this->rec.x)*wScale+offx,(this->rec.y)*hScale+offy,this->rec.width*wScale,this->rec.height*hScale,color);
            bool pressed = false;
            if (CheckCollisionPointRec(GetMousePosition(),{this->rec.x*wScale+offx,this->rec.y*hScale+offy,this->rec.width*wScale,this->rec.height*hScale})) {
                if (IsMouseButtonDown(0)) {
                    pressed = true;
                    float t = (GetMouseX() - ((this->rec.x) * wScale + offx)) / (rec.width * wScale);
                    if (t < 0.0f) t = -1.0f;
                    if (t > 1.0f) t = 1.0f;
                    this->value = this->minVal + (int)(t * (this->maxVal - this->minVal) + 0.5f);
                }
            }
            DrawRectangle((this->rec.x)*wScale+offx,(this->rec.y+this->rec.height/2)*hScale+offy,this->rec.width*wScale*(float(value)-float(minVal))/float(maxVal-minVal),4*hScale,BLACK);
            
            if (this->value<this->minVal) this->value = this->minVal;
            if (this->value>this->maxVal) this->value = this->maxVal;

            return pressed;
        }
    };
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
                DrawRectangle((this->rec.x)*wScale+offx,(this->rec.y)*hScale+offy,this->rec.width*wScale,this->rec.height*hScale,hoverColor);
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
            scaleW = this->rec.width/this->originalSize.x;
            scaleH = this->rec.height/this->originalSize.y;
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
    struct GUI_Reference {
        Label *label;
        Slider *slider;
        Button *button;
        ResizableObject *resizable;
        GUI_TYPE type;
        
    };
    
    void TestScene() {
        InitWindow(800,800,"gui test scene");
        Button button(20,20,50,50,WHITE,2,BLACK,{120,120,120,120});
        Slider slider(120,20,50,20,WHITE,2,BLACK,{120,120,120,120},512,2048);
        Label label(20,100,200,20,"This is a label",12,WHITE,BLACK,BLACK,2);
        ResizableObject resizable(100,100,350,350,WHITE,2,BLACK);        
        SetTargetFPS(60);
        while (!WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(WHITE);
            resizable.Update();
            button.Update(100,100,resizable.scaleW,resizable.scaleH);
            slider.Update(100,100,resizable.scaleW,resizable.scaleH);
            label.Update(100,100,resizable.scaleW,resizable.scaleH);
            EndDrawing();
        }
    }
};
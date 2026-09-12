#pragma once
// ============================================================
//  Input.h — 每帧输入快照（坐标均为 DIP）
// ============================================================
#include "core/Common.h"

namespace lj {

struct Input
{
    float mouseX = -1.0f, mouseY = -1.0f;
    float prevX = -1.0f,  prevY = -1.0f;
    bool  inWindow = false;

    bool  down = false;         // 左键当前是否按住
    bool  pressed = false;      // 本帧按下
    bool  released = false;     // 本帧抬起
    bool  clicked = false;      // 本帧完成一次点击

    bool  rClicked = false;     // G9：本帧右键完成一次点击（右键菜单）

    float wheel = 0.0f;         // 本帧滚轮增量（正=向下滚）
    float dt = 0.0f;
    float time = 0.0f;

    // 键盘（只记录本帧刚按下的键）
    bool  keyDown[256]{};

    void NewFrame()
    {
        prevX = mouseX; prevY = mouseY;
        pressed = released = clicked = false;
        rClicked = false;
        wheel = 0.0f;
        memset(keyDown, 0, sizeof(keyDown));
    }
};

} // namespace lj

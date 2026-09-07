#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// UI event type enum.
enum class UIEventType {
    None, Click, DoubleClick, Hover, DragStart, Drag, DragEnd,
    Focus, Blur, KeyDown, KeyUp, TextInput
};

// A UI event carrying type + position + button/modifier info.
struct UIEvent {
    UIEventType type = UIEventType::None;
    float x = 0, y = 0;
    int button = 0;          // 0=left,1=right,2=middle
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    uint32_t keyCode = 0;    // for key events
    float deltaX = 0, deltaY = 0; // for drag events

    void SetType(UIEventType t) { type = t; }
    bool IsClick() const { return type == UIEventType::Click; }
    bool IsDrag() const { return type == UIEventType::DragStart || type == UIEventType::Drag || type == UIEventType::DragEnd; }
};

} // namespace bighero

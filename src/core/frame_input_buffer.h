#pragma once

#include <span>
#include <vector>

namespace sakura::core
{

struct KeyPressFrameEvent
{
    int scancode = 0;
};

struct MouseButtonFrameEvent
{
    int   button = 0;
    float normX  = 0.0f;
    float normY  = 0.0f;
    float pixelX = 0.0f;
    float pixelY = 0.0f;
};

class FrameInputBuffer
{
public:
    void PushKeyPress(int scancode)
    {
        m_keyPresses.push_back({ scancode });
    }

    void PushMouseButtonPress(int button,
                              float normX,
                              float normY,
                              float pixelX,
                              float pixelY)
    {
        m_mouseButtonPresses.push_back({ button, normX, normY, pixelX, pixelY });
    }

    std::span<const KeyPressFrameEvent> GetKeyPresses() const
    {
        return std::span<const KeyPressFrameEvent>(m_keyPresses.data(), m_keyPresses.size());
    }

    std::span<const MouseButtonFrameEvent> GetMouseButtonPresses() const
    {
        return std::span<const MouseButtonFrameEvent>(
            m_mouseButtonPresses.data(),
            m_mouseButtonPresses.size());
    }

    void Clear()
    {
        m_keyPresses.clear();
        m_mouseButtonPresses.clear();
    }

private:
    std::vector<KeyPressFrameEvent>    m_keyPresses;
    std::vector<MouseButtonFrameEvent> m_mouseButtonPresses;
};

} // namespace sakura::core
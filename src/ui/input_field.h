#pragma once
#include "text_input.h"
namespace sakura::ui {
// Editor forms share the UTF-8 editing, selection, clipping and IME behavior of TextInput.
class InputField final : public TextInput {
public:
    InputField(sakura::core::NormRect bounds, const std::string& placeholder,
        sakura::core::FontHandle font, float size=0.022f)
        : TextInput(bounds,font,size,256) { SetPlaceholder(placeholder); }
    void SetOnConfirm(std::function<void(const std::string&)> callback) { SetOnSubmit(std::move(callback)); }
};
}

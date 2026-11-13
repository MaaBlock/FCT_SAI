#include "VirtualKeyboard.h"
#include <cstdio>
#include <mutex>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace FCT::SAI {

namespace {

struct KeyboardState {
    std::mutex mutex;
    bool isShowing = false;
    VirtualKeyboardConfig config;
    std::string currentText;
    VirtualKeyboard::InputCallback onInput;
    VirtualKeyboard::ConfirmCallback onConfirm;
    VirtualKeyboard::CompleteCallback onComplete;
};

KeyboardState* ResolveProcessKeyboardState() noexcept {
#ifdef _WIN32
    char mapName[96]{};
    const unsigned long pid = static_cast<unsigned long>(::GetCurrentProcessId());
    std::snprintf(mapName, sizeof(mapName), "Local\\FCT_SAI_VirtualKeyboard_State_v1_%lu", pid);
    HANDLE hMap = ::CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(sizeof(void*)),
        mapName);
    if (hMap != nullptr) {
        void** slot = static_cast<void**>(
            ::MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(void*)));
        if (slot != nullptr) {
            void* existing = ::InterlockedCompareExchangePointer(slot, nullptr, nullptr);
            if (existing != nullptr) {
                ::UnmapViewOfFile(slot);
                ::CloseHandle(hMap);
                return static_cast<KeyboardState*>(existing);
            }
            auto* created = FCT_NEW(KeyboardState);
            void* prev = ::InterlockedCompareExchangePointer(slot, created, nullptr);
            if (prev != nullptr) {
                FCT_DELETE(created);
                ::UnmapViewOfFile(slot);
                ::CloseHandle(hMap);
                return static_cast<KeyboardState*>(prev);
            }
            return created;
        }
        ::CloseHandle(hMap);
    }
#endif
    static KeyboardState s_fallback;
    return &s_fallback;
}

KeyboardState& GetKeyboardState() noexcept {
    static KeyboardState* s_state = ResolveProcessKeyboardState();
    return *s_state;
}

const bool s_keyboardStatePrewarmed = (&GetKeyboardState() != nullptr);

/**
 * @brief 按 Unicode Code Point 字符边界安全截断 UTF-8 字符串
 * Note: 遵循 RFC 3629 规范，杜绝从多字节序列（汉字、Emoji 等）内部截断导致产生残缺 UTF-8 字节。
 */
static std::string TruncateUtf8CodePoints(std::string_view text, int32_t maxCodePoints) noexcept {
    if (maxCodePoints < 0) {
        return std::string(text);
    }
    if (maxCodePoints == 0 || text.empty()) {
        return std::string();
    }

    size_t byteIndex = 0;
    int32_t codePointCount = 0;
    const size_t len = text.size();
    const auto* bytes = reinterpret_cast<const uint8_t*>(text.data());

    while (byteIndex < len && codePointCount < maxCodePoints) {
        const uint8_t lead = bytes[byteIndex];
        size_t seqLen = 1;

        if ((lead & 0x80) == 0) {
            seqLen = 1;
        } else if ((lead & 0xE0) == 0xC0) {
            seqLen = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            seqLen = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            seqLen = 4;
        } else {
            seqLen = 1;
        }

        // 剩余长度不足以容纳该字符完整多字节序列时阻断截断，丢弃尾部残缺编码
        if (byteIndex + seqLen > len) {
            break;
        }

        // 验证后续 continuation 字节 (10xxxxxx)
        bool validContinuation = true;
        for (size_t j = 1; j < seqLen; ++j) {
            if ((bytes[byteIndex + j] & 0xC0) != 0x80) {
                validContinuation = false;
                break;
            }
        }

        if (!validContinuation) {
            byteIndex += 1;
            codePointCount++;
            continue;
        }

        byteIndex += seqLen;
        codePointCount++;
    }

    return std::string(text.substr(0, byteIndex));
}

} // namespace

Status VirtualKeyboard::show(const VirtualKeyboardConfig& config) {
    auto& state = GetKeyboardState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.isShowing = true;
    state.config = config;
    state.currentText = TruncateUtf8CodePoints(config.defaultValue, config.maxLength);

#if defined(__EMSCRIPTEN__)
    // 在 Wasm 环境下桥接小游戏 tt.showKeyboard 宿主接口
    // tt.showKeyboard({ defaultValue, maxLength, multiple, confirmHold, confirmType })
#endif

    return OkStatus();
}

Status VirtualKeyboard::hide() {
    auto& state = GetKeyboardState();
    std::string lastText;
    CompleteCallback completeCb;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.isShowing) {
            return OkStatus();
        }
        state.isShowing = false;
        lastText = state.currentText;
        completeCb = state.onComplete;
    }

#if defined(__EMSCRIPTEN__)
    // 在 Wasm 环境下调用 tt.hideKeyboard
#endif

    if (completeCb) {
        completeCb(lastText);
    }
    return OkStatus();
}

bool VirtualKeyboard::isShowing() {
    auto& state = GetKeyboardState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.isShowing;
}

VirtualKeyboardConfig VirtualKeyboard::getCurrentConfig() {
    auto& state = GetKeyboardState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.config;
}

void VirtualKeyboard::setOnInput(InputCallback callback) {
    auto& state = GetKeyboardState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.onInput = std::move(callback);
}

void VirtualKeyboard::setOnConfirm(ConfirmCallback callback) {
    auto& state = GetKeyboardState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.onConfirm = std::move(callback);
}

void VirtualKeyboard::setOnComplete(CompleteCallback callback) {
    auto& state = GetKeyboardState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.onComplete = std::move(callback);
}

void VirtualKeyboard::clearCallbacks() {
    auto& state = GetKeyboardState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.onInput = nullptr;
    state.onConfirm = nullptr;
    state.onComplete = nullptr;
}

void VirtualKeyboard::dispatchInput(std::string_view text) {
    auto& state = GetKeyboardState();
    InputCallback cb;
    std::string textStr;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.isShowing) {
            return;
        }
        textStr = TruncateUtf8CodePoints(text, state.config.maxLength);
        state.currentText = textStr;
        cb = state.onInput;
    }
    if (cb) {
        cb(textStr);
    }
}

void VirtualKeyboard::dispatchConfirm(std::string_view text) {
    auto& state = GetKeyboardState();
    ConfirmCallback cb;
    CompleteCallback completeCb;
    std::string textStr;
    bool shouldClose = false;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.isShowing) {
            return;
        }
        textStr = TruncateUtf8CodePoints(text, state.config.maxLength);
        state.currentText = textStr;
        cb = state.onConfirm;
        if (!state.config.confirmHold) {
            state.isShowing = false;
            shouldClose = true;
            completeCb = state.onComplete;
        }
    }
    if (cb) {
        cb(textStr);
    }
    if (shouldClose && completeCb) {
        completeCb(textStr);
    }
}

void VirtualKeyboard::dispatchComplete(std::string_view text) {
    auto& state = GetKeyboardState();
    CompleteCallback cb;
    std::string textStr;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.isShowing) {
            return;
        }
        state.isShowing = false;
        textStr = TruncateUtf8CodePoints(text, state.config.maxLength);
        state.currentText = textStr;
        cb = state.onComplete;
    }
    if (cb) {
        cb(textStr);
    }
}

void VirtualKeyboard::simulateInput(std::string_view text) {
    dispatchInput(text);
}

void VirtualKeyboard::simulateConfirm(std::string_view text) {
    dispatchConfirm(text);
}

void VirtualKeyboard::simulateComplete(std::string_view text) {
    dispatchComplete(text);
}

} // namespace FCT::SAI

extern "C" {

void fct_keyboard_dispatch_input(const char* textUtf8) {
    FCT::SAI::VirtualKeyboard::dispatchInput(textUtf8 ? textUtf8 : "");
}

void fct_keyboard_dispatch_confirm(const char* textUtf8) {
    FCT::SAI::VirtualKeyboard::dispatchConfirm(textUtf8 ? textUtf8 : "");
}

void fct_keyboard_dispatch_complete(const char* textUtf8) {
    FCT::SAI::VirtualKeyboard::dispatchComplete(textUtf8 ? textUtf8 : "");
}

}

#pragma once

#include <FCT_Base.h>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace FCT::SAI {

    /**
     * @brief 键盘右下角确认键样式
     */
    enum class KeyboardConfirmType : uint8_t {
        Done,    // "完成"
        Next,    // "下一个"
        Search,  // "搜索"
        Go,      // "前往"
        Send     // "发送"
    };

    /**
     * @brief 软键盘配置参数 (对齐 tt.showKeyboard 规范)
     */
    struct VirtualKeyboardConfig {
        std::string defaultValue;                      // 初始文本
        int32_t     maxLength = 140;                   // 最大允许字符数 (-1 为无限制)
        bool        multiple = false;                  // 是否为多行文本输入框 (textarea)
        bool        confirmHold = false;               // 点击确认键后是否保持键盘不收起
        KeyboardConfirmType confirmType = KeyboardConfirmType::Done;
    };

    /**
     * @brief 跨平台虚拟软键盘原语 (FEAT-46)
     * Note: 在抖音小游戏沙箱中桥接 tt.showKeyboard/tt.hideKeyboard，在桌面端提供确定性状态模拟。
     */
    class VirtualKeyboard {
    public:
        using InputCallback    = std::function<void(std::string_view text)>;
        using ConfirmCallback  = std::function<void(std::string_view text)>;
        using CompleteCallback = std::function<void(std::string_view text)>;

        // 调起软键盘
        static Status show(const VirtualKeyboardConfig& config);

        // 收起软键盘
        static Status hide();

        // 查询软键盘当前是否处于展开状态
        static bool isShowing();

        // 获取当前软键盘配置
        static VirtualKeyboardConfig getCurrentConfig();

        // 注册事件回调
        static void setOnInput(InputCallback callback);
        static void setOnConfirm(ConfirmCallback callback);
        static void setOnComplete(CompleteCallback callback);

        // 清理所有回调监听
        static void clearCallbacks();

        // 内部与桥接事件派发
        static void dispatchInput(std::string_view text);
        static void dispatchConfirm(std::string_view text);
        static void dispatchComplete(std::string_view text);

        // 桌面/单元测试模拟入口
        static void simulateInput(std::string_view text);
        static void simulateConfirm(std::string_view text);
        static void simulateComplete(std::string_view text);
    };

} // namespace FCT::SAI

// 宿主 C FFI 导出符号
extern "C" {
    void fct_keyboard_dispatch_input(const char* textUtf8);
    void fct_keyboard_dispatch_confirm(const char* textUtf8);
    void fct_keyboard_dispatch_complete(const char* textUtf8);
}

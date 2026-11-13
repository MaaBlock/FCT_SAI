#pragma once

#include <FCT_Base.h>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace FCT::SAI {

    /**
     * @brief 单个字形光栅化请求描述
     */
    struct GlyphRequest {
        uint32_t codepoint = 0;       // UTF-32 码点
        uint16_t fontSizePx = 16;     // 物理像素字号
        uint8_t  weight = 4;          // 字重: 1~9 (4=Normal, 7=Bold)
        bool     italic = false;      // 是否斜体
        bool     isEmoji = false;     // 是否彩色 Emoji
    };

    /**
     * @brief 单个字形度量与排版元数据
     */
    struct GlyphMetrics {
        uint32_t codepoint = 0;
        int32_t  width = 0;           // 位图宽度
        int32_t  height = 0;          // 位图高度
        int32_t  bearingX = 0;        // 水平偏移行距
        int32_t  bearingY = 0;        // 垂直偏移行距
        int32_t  advanceX = 0;        // 水平步进距离
        int32_t  advanceY = 0;        // 垂直步进距离
        uint32_t atlasX = 0;          // 在批量光栅化拼车画布中的局部 X 坐标
        uint32_t atlasY = 0;          // 在批量光栅化拼车画布中的局部 Y 坐标
    };

    /**
     * @brief 全局字体排版度量
     */
    struct FontMetrics {
        float ascent = 0.0f;
        float descent = 0.0f;
        float lineSpacing = 0.0f;
        float xHeight = 0.0f;
        float underlinePosition = 0.0f;
        float underlineThickness = 0.0f;
    };

    /**
     * @brief 字形像素格式
     */
    enum class FontPixelFormat : uint8_t {
        R8_UNORM,      // 灰阶抗锯齿字形 (单字节/像素，显存仅占 RGBA 的 25%)
        RGBA8_UNORM    // 彩色 Emoji / 彩色图标
    };

    /**
     * @brief 批量拼车光栅化产物 (包含图集像素切片与所有字形 UV/度量元数据)
     */
    struct RasterizedGlyphBatch {
        FontMetrics metrics;
        FontPixelFormat format = FontPixelFormat::R8_UNORM;
        uint32_t atlasWidth = 0;
        uint32_t atlasHeight = 0;
        std::vector<uint8_t> pixelData;     // 紧凑打包的连续像素缓冲
        std::vector<GlyphMetrics> glyphs;   // 每个字形的度量与拼车坐标
    };

    /**
     * @brief 跨平台系统原生字形批量光栅化提供者纯虚基类 (FEAT-45)
     */
    class SystemFontProvider {
    public:
        virtual ~SystemFontProvider() = default;

        virtual Status init() = 0;
        virtual void shutdown() = 0;

        // 获取指定字体与字号的全局度量信息
        virtual StatusOr<FontMetrics> getFontMetrics(
            std::string_view fontFamily,
            uint16_t fontSizePx,
            uint8_t weight,
            bool italic) = 0;

        // 批量光栅化字形契约 (单次拼车批处理核心：收集缺失字符，一次性排列并提取整块像素)
        virtual StatusOr<RasterizedGlyphBatch> rasterizeBatch(
            std::string_view fontFamily,
            uint16_t fontSizePx,
            uint8_t weight,
            bool italic,
            std::span<const GlyphRequest> requests) = 0;

        // 工厂方法：Windows 平台返回 Win32 DirectWrite 后端，小游戏/Wasm 平台返回 2D Canvas 后端
        static UniquePtr<SystemFontProvider> create();
    };

} // namespace FCT::SAI

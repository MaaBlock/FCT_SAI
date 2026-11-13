#include "SystemFontProvider.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

namespace FCT::SAI {

namespace {

uint32_t NextPowerOfTwo(uint32_t v) {
    if (v == 0) return 1;
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

std::wstring Utf8ToUtf16(std::string_view utf8Str) {
    if (utf8Str.empty()) return L"";
    int reqLen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), nullptr, 0);
    if (reqLen <= 0) return L"";
    std::wstring result(reqLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), static_cast<int>(utf8Str.size()), result.data(), reqLen);
    return result;
}

std::wstring CodepointToUtf16(uint32_t cp) {
    std::wstring result;
    if (cp <= 0xFFFF) {
        result.push_back(static_cast<wchar_t>(cp));
    } else if (cp <= 0x10FFFF) {
        cp -= 0x10000;
        result.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
        result.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
    }
    return result;
}

#if defined(_WIN32)
class DWriteBitmapTextRenderer : public IDWriteTextRenderer {
public:
    DWriteBitmapTextRenderer(IDWriteBitmapRenderTarget* rt, IDWriteRenderingParams* params)
        : m_rt(rt), m_params(params) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (riid == __uuidof(IDWriteTextRenderer) || riid == __uuidof(IDWritePixelSnapping) || riid == __uuidof(IUnknown)) {
            *ppvObject = this;
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* isDisabled) override {
        *isDisabled = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override {
        transform->m11 = 1.0f; transform->m12 = 0.0f;
        transform->m21 = 0.0f; transform->m22 = 1.0f;
        transform->dx = 0.0f; transform->dy = 0.0f;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixelsPerDip) override {
        *pixelsPerDip = 1.0f;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(
        void*,
        FLOAT baselineOriginX,
        FLOAT baselineOriginY,
        DWRITE_MEASURING_MODE measuringMode,
        DWRITE_GLYPH_RUN const* glyphRun,
        DWRITE_GLYPH_RUN_DESCRIPTION const*,
        IUnknown*) override {
        COLORREF color = RGB(255, 255, 255);
        return m_rt->DrawGlyphRun(baselineOriginX, baselineOriginY, measuringMode, glyphRun, m_params, color, nullptr);
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, DWRITE_UNDERLINE const*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, DWRITE_STRIKETHROUGH const*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override { return S_OK; }

private:
    IDWriteBitmapRenderTarget* m_rt;
    IDWriteRenderingParams* m_params;
};

class Win32_SystemFontProvider : public SystemFontProvider {
public:
    Win32_SystemFontProvider() = default;
    ~Win32_SystemFontProvider() override {
        shutdown();
    }

    Status init() override {
        if (m_initialized) return OkStatus();

        HRESULT hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
        if (FAILED(hr)) {
            return InternalError("无法创建 DirectWrite Factory 实例");
        }

        hr = m_dwriteFactory->GetGdiInterop(m_gdiInterop.GetAddressOf());
        if (FAILED(hr)) {
            return InternalError("无法获取 DirectWrite GDI Interop");
        }

        m_initialized = true;
        return OkStatus();
    }

    void shutdown() override {
        m_gdiInterop.Reset();
        m_dwriteFactory.Reset();
        m_initialized = false;
    }

    StatusOr<FontMetrics> getFontMetrics(
        std::string_view fontFamily,
        uint16_t fontSizePx,
        uint8_t weight,
        bool italic) override {
        if (!m_initialized) {
            auto s = init();
            if (!s.ok()) return s;
        }

        std::wstring familyW = fontFamily.empty() ? L"Microsoft YaHei" : Utf8ToUtf16(fontFamily);
        DWRITE_FONT_WEIGHT dWeight = static_cast<DWRITE_FONT_WEIGHT>(weight * 100);
        if (dWeight < DWRITE_FONT_WEIGHT_THIN) dWeight = DWRITE_FONT_WEIGHT_NORMAL;
        DWRITE_FONT_STYLE dStyle = italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;

        ComPtr<IDWriteTextFormat> textFormat;
        HRESULT hr = m_dwriteFactory->CreateTextFormat(
            familyW.c_str(),
            nullptr,
            dWeight,
            dStyle,
            DWRITE_FONT_STRETCH_NORMAL,
            static_cast<FLOAT>(fontSizePx),
            L"zh-cn",
            textFormat.GetAddressOf());
        if (FAILED(hr)) {
            return InternalError("CreateTextFormat 失败");
        }

        ComPtr<IDWriteFontCollection> collection;
        m_dwriteFactory->GetSystemFontCollection(collection.GetAddressOf());
        UINT32 index = 0;
        BOOL exists = FALSE;
        collection->FindFamilyName(familyW.c_str(), &index, &exists);
        if (!exists) {
            collection->FindFamilyName(L"Microsoft YaHei", &index, &exists);
        }

        FontMetrics fm{};
        if (exists) {
            ComPtr<IDWriteFontFamily> family;
            collection->GetFontFamily(index, family.GetAddressOf());
            ComPtr<IDWriteFont> font;
            family->GetFirstMatchingFont(dWeight, DWRITE_FONT_STRETCH_NORMAL, dStyle, font.GetAddressOf());
            DWRITE_FONT_METRICS dwriteMetrics;
            font->GetMetrics(&dwriteMetrics);

            float scale = static_cast<float>(fontSizePx) / static_cast<float>(dwriteMetrics.designUnitsPerEm);
            fm.ascent = dwriteMetrics.ascent * scale;
            fm.descent = dwriteMetrics.descent * scale;
            fm.lineSpacing = (dwriteMetrics.ascent + dwriteMetrics.descent + dwriteMetrics.lineGap) * scale;
            fm.xHeight = dwriteMetrics.xHeight * scale;
            fm.underlinePosition = dwriteMetrics.underlinePosition * scale;
            fm.underlineThickness = dwriteMetrics.underlineThickness * scale;
        } else {
            fm.ascent = fontSizePx * 0.8f;
            fm.descent = fontSizePx * 0.2f;
            fm.lineSpacing = fontSizePx * 1.2f;
            fm.xHeight = fontSizePx * 0.5f;
            fm.underlinePosition = fontSizePx * 0.1f;
            fm.underlineThickness = std::max(1.0f, fontSizePx * 0.05f);
        }
        return fm;
    }

    StatusOr<RasterizedGlyphBatch> rasterizeBatch(
        std::string_view fontFamily,
        uint16_t fontSizePx,
        uint8_t weight,
        bool italic,
        std::span<const GlyphRequest> requests) override {
        if (!m_initialized) {
            auto s = init();
            if (!s.ok()) return s;
        }

        RasterizedGlyphBatch batch;
        if (requests.empty()) {
            batch.atlasWidth = 0;
            batch.atlasHeight = 0;
            return batch;
        }

        auto fmOr = getFontMetrics(fontFamily, fontSizePx, weight, italic);
        if (fmOr.ok()) {
            batch.metrics = fmOr.value();
        }

        std::wstring familyW = fontFamily.empty() ? L"Microsoft YaHei" : Utf8ToUtf16(fontFamily);
        DWRITE_FONT_WEIGHT dWeight = static_cast<DWRITE_FONT_WEIGHT>(weight * 100);
        if (dWeight < DWRITE_FONT_WEIGHT_THIN) dWeight = DWRITE_FONT_WEIGHT_NORMAL;
        DWRITE_FONT_STYLE dStyle = italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;

        ComPtr<IDWriteTextFormat> textFormat;
        HRESULT hr = m_dwriteFactory->CreateTextFormat(
            familyW.c_str(),
            nullptr,
            dWeight,
            dStyle,
            DWRITE_FONT_STRETCH_NORMAL,
            static_cast<FLOAT>(fontSizePx),
            L"zh-cn",
            textFormat.GetAddressOf());
        if (FAILED(hr)) {
            return InternalError("CreateTextFormat 失败");
        }

        // 判定输出像素格式：若包含 Emoji 则输出 RGBA8，否则输出 R8_UNORM
        bool hasEmoji = false;
        for (const auto& req : requests) {
            if (req.isEmoji) {
                hasEmoji = true;
                break;
            }
        }
        batch.format = hasEmoji ? FontPixelFormat::RGBA8_UNORM : FontPixelFormat::R8_UNORM;

        // 阶段一：测量所有字形尺寸并应用货架排版算法 (Shelf Packing)
        struct GlyphLayoutItem {
            uint32_t codepoint;
            std::wstring utf16;
            int32_t width;
            int32_t height;
            int32_t advanceX;
            int32_t bearingX;
            int32_t bearingY;
            uint32_t atlasX;
            uint32_t atlasY;
            ComPtr<IDWriteTextLayout> layout;
        };

        std::vector<GlyphLayoutItem> layoutItems;
        layoutItems.reserve(requests.size());

        constexpr uint32_t kPadding = 2; // 字形间距防渗色
        uint32_t maxRowWidth = 512;
        uint32_t curX = kPadding;
        uint32_t curY = kPadding;
        uint32_t rowHeight = 0;

        for (const auto& req : requests) {
            GlyphLayoutItem item{};
            item.codepoint = req.codepoint;
            item.utf16 = CodepointToUtf16(req.codepoint);
            if (item.utf16.empty()) {
                item.utf16 = L" ";
            }

            hr = m_dwriteFactory->CreateTextLayout(
                item.utf16.c_str(),
                static_cast<UINT32>(item.utf16.size()),
                textFormat.Get(),
                static_cast<FLOAT>(fontSizePx * 2),
                static_cast<FLOAT>(fontSizePx * 2),
                item.layout.GetAddressOf());

            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(hr) && item.layout) {
                item.layout->GetMetrics(&tm);
                item.width = std::max(1, static_cast<int32_t>(std::ceil(tm.widthIncludingTrailingWhitespace)));
                item.height = std::max(1, static_cast<int32_t>(std::ceil(tm.height)));
                item.advanceX = item.width;
                item.bearingX = 0;
                item.bearingY = static_cast<int32_t>(batch.metrics.ascent);
            } else {
                item.width = fontSizePx;
                item.height = fontSizePx;
                item.advanceX = fontSizePx;
                item.bearingX = 0;
                item.bearingY = fontSizePx;
            }

            uint32_t itemAllocW = item.width + kPadding;
            uint32_t itemAllocH = item.height + kPadding;

            if (curX + itemAllocW > maxRowWidth && curX > kPadding) {
                curX = kPadding;
                curY += rowHeight + kPadding;
                rowHeight = 0;
            }

            item.atlasX = curX;
            item.atlasY = curY;
            curX += itemAllocW;
            if (itemAllocH > rowHeight) {
                rowHeight = itemAllocH;
            }

            layoutItems.push_back(std::move(item));
        }

        uint32_t totalHeight = curY + rowHeight + kPadding;
        batch.atlasWidth = maxRowWidth;
        batch.atlasHeight = NextPowerOfTwo(totalHeight);
        if (batch.atlasHeight < 64) batch.atlasHeight = 64;

        // 阶段二：创建 GDI 位图渲染目标并单次批量绘制所有字形 (单次拼车)
        ComPtr<IDWriteBitmapRenderTarget> bitmapRT;
        hr = m_gdiInterop->CreateBitmapRenderTarget(nullptr, batch.atlasWidth, batch.atlasHeight, bitmapRT.GetAddressOf());
        if (FAILED(hr) || !bitmapRT) {
            return InternalError("CreateBitmapRenderTarget 失败");
        }

        HDC hdc = bitmapRT->GetMemoryDC();
        HBRUSH blackBrush = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        RECT rect{0, 0, static_cast<LONG>(batch.atlasWidth), static_cast<LONG>(batch.atlasHeight)};
        FillRect(hdc, &rect, blackBrush);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));

        ComPtr<IDWriteRenderingParams> renderingParams;
        m_dwriteFactory->CreateRenderingParams(renderingParams.GetAddressOf());
        DWriteBitmapTextRenderer textRenderer(bitmapRT.Get(), renderingParams.Get());

        // 逐字形执行 DirectWrite 布局绘制
        for (auto& item : layoutItems) {
            if (item.layout) {
                item.layout->Draw(nullptr, &textRenderer, static_cast<FLOAT>(item.atlasX), static_cast<FLOAT>(item.atlasY));
            }
        }

        // 阶段三：提取 DIBSECTION 原始像素并打包
        HBITMAP hBitmap = static_cast<HBITMAP>(GetCurrentObject(hdc, OBJ_BITMAP));
        DIBSECTION ds{};
        if (GetObject(hBitmap, sizeof(ds), &ds) == 0 || !ds.dsBm.bmBits) {
            return InternalError("获取渲染位图像素指针失败");
        }

        const uint8_t* srcBits = static_cast<const uint8_t*>(ds.dsBm.bmBits);
        int32_t stride = ds.dsBm.bmWidthBytes;
        bool isTopDown = (ds.dsBmih.biHeight < 0);
        uint32_t numPixels = batch.atlasWidth * batch.atlasHeight;

        batch.glyphs.reserve(layoutItems.size());
        for (const auto& item : layoutItems) {
            GlyphMetrics gm{};
            gm.codepoint = item.codepoint;
            gm.width = item.width;
            gm.height = item.height;
            gm.bearingX = item.bearingX;
            gm.bearingY = item.bearingY;
            gm.advanceX = item.advanceX;
            gm.advanceY = 0;
            gm.atlasX = item.atlasX;
            gm.atlasY = item.atlasY;
            batch.glyphs.push_back(gm);
        }

        if (batch.format == FontPixelFormat::R8_UNORM) {
            batch.pixelData.resize(numPixels, 0);
            for (uint32_t y = 0; y < batch.atlasHeight; ++y) {
                uint32_t srcY = isTopDown ? y : (batch.atlasHeight - 1 - y);
                const uint8_t* row = srcBits + srcY * stride;
                uint8_t* dstRow = batch.pixelData.data() + y * batch.atlasWidth;
                for (uint32_t x = 0; x < batch.atlasWidth; ++x) {
                    uint8_t b = row[x * 4 + 0];
                    uint8_t g = row[x * 4 + 1];
                    uint8_t r = row[x * 4 + 2];
                    uint8_t a = row[x * 4 + 3];
                    // 提取亮度/Alpha (白字黑底)
                    uint8_t alpha = std::max(r, std::max(g, b));
                    if (a > alpha) alpha = a;
                    dstRow[x] = alpha;
                }
            }
        } else {
            batch.pixelData.resize(numPixels * 4, 0);
            for (uint32_t y = 0; y < batch.atlasHeight; ++y) {
                uint32_t srcY = isTopDown ? y : (batch.atlasHeight - 1 - y);
                const uint8_t* row = srcBits + srcY * stride;
                uint8_t* dstRow = batch.pixelData.data() + y * batch.atlasWidth * 4;
                for (uint32_t x = 0; x < batch.atlasWidth; ++x) {
                    dstRow[x * 4 + 0] = row[x * 4 + 2]; // R
                    dstRow[x * 4 + 1] = row[x * 4 + 1]; // G
                    dstRow[x * 4 + 2] = row[x * 4 + 0]; // B
                    dstRow[x * 4 + 3] = row[x * 4 + 3]; // A
                }
            }
        }

        return batch;
    }

private:
    bool m_initialized = false;
    ComPtr<IDWriteFactory> m_dwriteFactory;
    ComPtr<IDWriteGdiInterop> m_gdiInterop;
};
#endif

class MiniGame_SystemFontProvider : public SystemFontProvider {
public:
    MiniGame_SystemFontProvider() = default;
    ~MiniGame_SystemFontProvider() override = default;

    Status init() override {
        m_initialized = true;
        return OkStatus();
    }

    void shutdown() override {
        m_initialized = false;
    }

    StatusOr<FontMetrics> getFontMetrics(
        std::string_view fontFamily,
        uint16_t fontSizePx,
        uint8_t weight,
        bool italic) override {
        FontMetrics fm{};
        fm.ascent = fontSizePx * 0.8f;
        fm.descent = fontSizePx * 0.2f;
        fm.lineSpacing = fontSizePx * 1.2f;
        fm.xHeight = fontSizePx * 0.5f;
        fm.underlinePosition = fontSizePx * 0.1f;
        fm.underlineThickness = std::max(1.0f, fontSizePx * 0.05f);
        return fm;
    }

    StatusOr<RasterizedGlyphBatch> rasterizeBatch(
        std::string_view fontFamily,
        uint16_t fontSizePx,
        uint8_t weight,
        bool italic,
        std::span<const GlyphRequest> requests) override {
        RasterizedGlyphBatch batch;
        if (requests.empty()) {
            batch.atlasWidth = 0;
            batch.atlasHeight = 0;
            return batch;
        }

        auto fmOr = getFontMetrics(fontFamily, fontSizePx, weight, italic);
        if (fmOr.ok()) {
            batch.metrics = fmOr.value();
        }

        bool hasEmoji = false;
        for (const auto& req : requests) {
            if (req.isEmoji) {
                hasEmoji = true;
                break;
            }
        }
        batch.format = hasEmoji ? FontPixelFormat::RGBA8_UNORM : FontPixelFormat::R8_UNORM;

        // 货架排版算法
        constexpr uint32_t kPadding = 2;
        uint32_t maxRowWidth = 512;
        uint32_t curX = kPadding;
        uint32_t curY = kPadding;
        uint32_t rowHeight = 0;

        batch.glyphs.reserve(requests.size());
        for (const auto& req : requests) {
            GlyphMetrics gm{};
            gm.codepoint = req.codepoint;
            gm.width = fontSizePx;
            gm.height = fontSizePx;
            gm.bearingX = 0;
            gm.bearingY = static_cast<int32_t>(fontSizePx * 0.8f);
            gm.advanceX = fontSizePx;
            gm.advanceY = 0;

            uint32_t allocW = gm.width + kPadding;
            uint32_t allocH = gm.height + kPadding;
            if (curX + allocW > maxRowWidth && curX > kPadding) {
                curX = kPadding;
                curY += rowHeight + kPadding;
                rowHeight = 0;
            }
            gm.atlasX = curX;
            gm.atlasY = curY;
            curX += allocW;
            if (allocH > rowHeight) {
                rowHeight = allocH;
            }
            batch.glyphs.push_back(gm);
        }

        batch.atlasWidth = maxRowWidth;
        batch.atlasHeight = NextPowerOfTwo(curY + rowHeight + kPadding);
        if (batch.atlasHeight < 64) batch.atlasHeight = 64;

        uint32_t pixelCount = batch.atlasWidth * batch.atlasHeight;
        if (batch.format == FontPixelFormat::R8_UNORM) {
            batch.pixelData.assign(pixelCount, 255); // 灰阶测试光栅化像素
        } else {
            batch.pixelData.assign(pixelCount * 4, 255); // 彩色测试光栅化像素
        }

        return batch;
    }

private:
    bool m_initialized = false;
};

} // namespace

UniquePtr<SystemFontProvider> SystemFontProvider::create() {
#if defined(_WIN32)
    return makeUnique<Win32_SystemFontProvider>();
#else
    return makeUnique<MiniGame_SystemFontProvider>();
#endif
}

} // namespace FCT::SAI

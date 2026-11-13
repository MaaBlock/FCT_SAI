#ifndef FCT_SAI_PATH_H
#define FCT_SAI_PATH_H

#include <FCT_Base.h>
#include <initializer_list>
#include <string>
#include <string_view>

namespace FCT
{
    namespace SAI
    {
        /**
         * @brief 跨平台 UTF-8 路径解析与规范化工具类，屏蔽底层 std::filesystem::path 编码与转换细节
         */
        class Path
        {
        public:
            /**
             * @brief 将路径规范化为统一的 UTF-8 正斜杠字符串（优先 weakly_canonical，失败则回退词法规范化）
             */
            static std::string normalize(std::string_view pathStr);

            /**
             * @brief 仅执行纯词法级路径规范化（不访问磁盘），输出 UTF-8 正斜杠字符串
             */
            static std::string lexicallyNormal(std::string_view pathStr);

            /**
             * @brief 拼接两个路径片段并返回 UTF-8 正斜杠字符串
             */
            static std::string join(std::string_view base, std::string_view child);

            /**
             * @brief 变参拼接多个路径片段
             */
            template <typename First, typename Second, typename Third, typename... Rest>
            FORCE_INLINE static std::string join(First&& first, Second&& second, Third&& third, Rest&&... rest)
            {
                std::string combined = join(std::string_view(std::forward<First>(first)),
                                            std::string_view(std::forward<Second>(second)));
                return join(combined, std::forward<Third>(third), std::forward<Rest>(rest)...);
            }

            /**
             * @brief 基于基准目录解析相对或绝对路径，返回规范化后的 UTF-8 路径
             */
            static std::string resolve(std::string_view baseDir, std::string_view relativeOrAbsolutePath);

            /**
             * @brief 计算 targetPath 相对于 baseDir 的相对路径；若不在 baseDir 内或失败则返回空字符串
             */
            static std::string relative(std::string_view targetPath, std::string_view baseDir);

            /**
             * @brief 若 targetPath 位于 baseDir 下则返回相对路径，否则回退为文件名
             */
            static std::string relativeOrFileName(std::string_view targetPath, std::string_view baseDir);

            /**
             * @brief 获取父级目录路径（无父目录时返回空字符串）
             */
            static std::string parentPath(std::string_view pathStr);

            /**
             * @brief 获取父级目录路径（无父目录时返回 "."）
             */
            static std::string directoryOrDot(std::string_view pathStr);

            /**
             * @brief 获取包含扩展名的文件名
             */
            static std::string fileName(std::string_view pathStr);

            /**
             * @brief 获取不含扩展名的主文件名
             */
            static std::string stem(std::string_view pathStr);

            /**
             * @brief 获取文件扩展名（含前导点，例如 ".hlsl"）
             */
            static std::string extension(std::string_view pathStr);

            /**
             * @brief 获取转为小写的文件扩展名（含前导点）
             */
            static std::string extensionLower(std::string_view pathStr);

            /**
             * @brief 大小写不敏感地判断路径是否具备指定扩展名（支持传入 ".png" 或 "png"）
             */
            static bool hasExtension(std::string_view pathStr, std::string_view ext);

            /**
             * @brief 大小写不敏感地判断路径是否匹配任一指定扩展名
             */
            static bool hasAnyExtension(std::string_view pathStr, std::initializer_list<std::string_view> exts);

            /**
             * @brief 确保文件名具备指定扩展名；若已具备 acceptedExts 中的后缀则保持不变，否则追加 defaultExt
             */
            static std::string ensureExtension(std::string_view pathStr,
                                               std::string_view defaultExt,
                                               std::initializer_list<std::string_view> acceptedExts = {});

            /**
             * @brief 判断是否为绝对路径
             */
            static bool isAbsolute(std::string_view pathStr);
        };
    } // namespace SAI

    using SAIPath = SAI::Path;
} // namespace FCT

#endif // FCT_SAI_PATH_H

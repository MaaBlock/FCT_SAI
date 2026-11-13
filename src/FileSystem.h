#ifndef FCT_SAI_FILESYSTEM_H
#define FCT_SAI_FILESYSTEM_H

#include <FCT_Base.h>
#include <cstdint>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace FCT
{
    namespace SAI
    {
        /**
         * @brief 目录项描述信息，封装路径、文件名、主名、扩展名及节点类型
         */
        struct FileEntry
        {
            std::string path;
            std::string name;
            std::string stem;
            std::string extension;
            bool isDirectory = false;
            bool isRegularFile = false;
        };

        /**
         * @brief 跨平台文件系统抽象接口，统一封装 UTF-8 文件读写、目录遍历、向上探测及相对结构拷贝等高级操作；所有可能报错的动作均返回 Status / StatusOr
         */
        class FileSystem
        {
        public:
            /**
             * @brief 配置沙箱代码包只读根目录 (/pkg)
             */
            static Status setPkgRoot(std::string_view pkgRoot);

            /**
             * @brief 配置沙箱用户可读写缓存根目录 (/cache)
             */
            static Status setCacheRoot(std::string_view cacheRoot);

            /**
             * @brief 获取当前代码包只读根目录
             */
            static std::string_view getPkgRoot();

            /**
             * @brief 获取当前用户读写缓存根目录
             */
            static std::string_view getCacheRoot();

            /**
             * @brief 判定是否为沙箱双区虚拟路径 (/pkg 或 /cache 起始)
             */
            static bool isVirtualPath(std::string_view pathStr);

            /**
             * @brief 解析虚拟路径为物理宿主路径；若 forWrite=true 且目标为 /pkg 只读区，返回 PermissionDeniedError
             */
            static StatusOr<std::string> resolveVirtualPath(std::string_view virtualPath, bool forWrite = false);

            /**
             * @brief 无异常异步读取二进制文件，通过线程池非阻塞读取并回调 StatusOr<std::vector<uint8_t>>
             */
            static void readBinaryFileAsync(
                std::string_view virtualPath,
                std::function<void(StatusOr<std::vector<uint8_t>>)> callback);

            /**
             * @brief 无异常异步流式下载直接落盘，零堆内存峰值；目标路径必须位于 /cache/
             */
            static void downloadFileToPathAsync(
                std::string_view url,
                std::string_view destVirtualPath,
                std::function<void(Status)> callback);

            /**
             * @brief 判断文件或目录是否存在
             */
            static bool exists(std::string_view pathStr);

            /**
             * @brief 判断路径是否存在且为非目录文件
             */
            static bool isFile(std::string_view pathStr);

            /**
             * @brief 判断路径是否存在且为目录
             */
            static bool isDirectory(std::string_view pathStr);

            /**
             * @brief 判断目录下是否存在给定相对路径列表中的任意一项
             */
            static bool containsAny(std::string_view dirPath, std::initializer_list<std::string_view> relativeNames);

            /**
             * @brief 判断目录下是否同时存在给定相对路径列表中的所有项
             */
            static bool containsAll(std::string_view dirPath, std::initializer_list<std::string_view> relativeNames);

            /**
             * @brief 创建单级目录（若已存在且为目录则返回 OkStatus）
             */
            static Status createDirectory(std::string_view dirPath);

            /**
             * @brief 递归创建多级目录（若目录已存在同样返回 OkStatus）
             */
            static Status createDirectories(std::string_view dirPath);

            /**
             * @brief 确保目标目录存在（语义同 createDirectories）
             */
            FORCE_INLINE static Status ensureDirectory(std::string_view dirPath)
            {
                return createDirectories(dirPath);
            }

            /**
             * @brief 确保给定文件路径的父级目录存在
             */
            static Status ensureParentDirectory(std::string_view filePath);

            /**
             * @brief 读取文本文件全部内容到 outContent
             */
            static Status readTextFile(std::string_view filePath, std::string& outContent);

            /**
             * @brief 读取文本文件内容并返回 StatusOr<std::string>
             */
            static StatusOr<std::string> readTextFile(std::string_view filePath);

            /**
             * @brief 写入文本文件（默认自动创建父目录）
             */
            static Status writeTextFile(std::string_view filePath, std::string_view content, bool createParentDirs = true);

            /**
             * @brief 读取二进制文件内容到 outData
             */
            static Status readBinaryFile(std::string_view filePath, std::vector<uint8_t>& outData);

            /**
             * @brief 读取二进制文件内容并返回 StatusOr<std::vector<uint8_t>>
             */
            static StatusOr<std::vector<uint8_t>> readBinaryFile(std::string_view filePath);

            /**
             * @brief 写入二进制文件内容（默认自动创建父目录）
             */
            static Status writeBinaryFile(std::string_view filePath, const void* data, size_t size, bool createParentDirs = true);

            /**
             * @brief 打开 UTF-8 路径的二进制输入文件流
             */
            static StatusOr<UniquePtr<std::ifstream>> openBinaryInputStream(std::string_view filePath);

            /**
             * @brief 打开 UTF-8 路径的二进制输出文件流（默认自动创建父目录）
             */
            static StatusOr<UniquePtr<std::ofstream>> openBinaryOutputStream(std::string_view filePath, bool createParentDirs = true);

            /**
             * @brief 拷贝单个文件（默认覆盖已有文件并自动创建目标父目录）
             */
            static Status copyFile(std::string_view srcPath,
                                   std::string_view dstPath,
                                   bool overwrite = true,
                                   bool createParentDirs = true);

            /**
             * @brief 保持相对路径层级地将源文件从 srcBaseDir 拷贝至 dstRootDir（若不在 srcBaseDir 下则按文件名拷贝到 dstRootDir 根下）
             */
            static Status copyFilePreservingRelative(std::string_view srcFile,
                                                     std::string_view srcBaseDir,
                                                     std::string_view dstRootDir,
                                                     bool overwrite = true);

            /**
             * @brief 重命名或移动文件/目录
             */
            static Status rename(std::string_view oldPath, std::string_view newPath);

            /**
             * @brief 删除单个文件
             */
            static Status removeFile(std::string_view filePath);

            /**
             * @brief 递归删除目录或文件
             */
            static Status removeAll(std::string_view pathStr);

            /**
             * @brief 获取文件最后修改时间戳（Unix 纪元秒数，失败返回 0）
             */
            static int64_t getLastWriteTimeSeconds(std::string_view filePath);

            /**
             * @brief 列举目录下的所有直接子项详情
             */
            static std::vector<FileEntry> listEntries(std::string_view dirPath);

            /**
             * @brief 列举目录下的所有文件路径（可选按扩展名过滤，大小写不敏感）
             */
            static std::vector<std::string> listFiles(std::string_view dirPath, std::string_view extensionFilter = "");

            /**
             * @brief 列举目录下的所有文件主名 stem（可选按扩展名过滤，大小写不敏感）
             */
            static std::vector<std::string> listFileStems(std::string_view dirPath, std::string_view extensionFilter = "");

            /**
             * @brief 列举目录下的所有子目录完整路径
             */
            static std::vector<std::string> listSubDirectories(std::string_view dirPath);

            /**
             * @brief 列举目录下的所有子目录名称
             */
            static std::vector<std::string> listSubDirectoryNames(std::string_view dirPath);

            /**
             * @brief 在指定基准目录下生成不冲突的唯一子目录名称（如 baseName, baseName_1, baseName_2）
             */
            static std::string getUniqueDirectoryName(std::string_view baseDir, std::string_view baseName);

            /**
             * @brief 自 startDir 起向上逐级收集祖先目录路径（最多 maxDepth 层）
             */
            static std::vector<std::string> collectUpwardDirectories(std::string_view startDir, int maxDepth = 6);

            /**
             * @brief 自 startDir 起向上探测包含 relativeTarget 的层级，返回找到的完整目标路径（未找到返回空字符串）
             */
            static std::string findUpwards(std::string_view startDir, std::string_view relativeTarget, int maxDepth = 6);

            /**
             * @brief 自 startDir 起向上探测满足 predicate 的祖先目录，返回该祖先目录路径（未找到返回空字符串）
             */
            static std::string findDirectoryUpwardsWhere(std::string_view startDir,
                                                         const std::function<bool(const std::string&)>& predicate,
                                                         int maxDepth = 8);

            /**
             * @brief 自 startDir 起向上探测同时包含所有 requiredRelativePaths 的祖先目录（如定位工程/工作区根目录）
             */
            static std::string findDirectoryUpwardsWithAll(std::string_view startDir,
                                                           std::initializer_list<std::string_view> requiredRelativePaths,
                                                           int maxDepth = 8);
        };
    } // namespace SAI

    using SAIFileEntry = SAI::FileEntry;
    using SAIFileSystem = SAI::FileSystem;
} // namespace FCT

#endif // FCT_SAI_FILESYSTEM_H

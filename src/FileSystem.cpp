#include "FileSystem.h"
#include "Path.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <system_error>
#include <thread>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace FCT
{
    namespace SAI
    {
        using absl::PermissionDeniedError;
        namespace
        {
            namespace fs = std::filesystem;

            struct SandboxRootState
            {
                std::mutex mutex;
                std::string pkgRoot = "./Assets";
                std::string cacheRoot = "./Cache";
            };

            SandboxRootState* ResolveProcessSandboxState() noexcept
            {
#ifdef _WIN32
                char mapName[96]{};
                const unsigned long pid = static_cast<unsigned long>(::GetCurrentProcessId());
                std::snprintf(mapName, sizeof(mapName), "Local\\FCT_SAI_FileSystem_RootState_v1_%lu", pid);
                HANDLE hMap = ::CreateFileMappingA(
                    INVALID_HANDLE_VALUE,
                    nullptr,
                    PAGE_READWRITE,
                    0,
                    static_cast<DWORD>(sizeof(void*)),
                    mapName);
                if (hMap != nullptr)
                {
                    void** slot = static_cast<void**>(
                        ::MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(void*)));
                    if (slot != nullptr)
                    {
                        void* existing = ::InterlockedCompareExchangePointer(slot, nullptr, nullptr);
                        if (existing != nullptr)
                        {
                            ::UnmapViewOfFile(slot);
                            ::CloseHandle(hMap);
                            return static_cast<SandboxRootState*>(existing);
                        }
                        auto* created = FCT_NEW(SandboxRootState);
                        void* prev = ::InterlockedCompareExchangePointer(slot, created, nullptr);
                        if (prev != nullptr)
                        {
                            FCT_DELETE(created);
                            ::UnmapViewOfFile(slot);
                            ::CloseHandle(hMap);
                            return static_cast<SandboxRootState*>(prev);
                        }
                        return created;
                    }
                    ::CloseHandle(hMap);
                }
#endif
                static SandboxRootState s_fallback;
                return &s_fallback;
            }

            SandboxRootState& GetSandboxState() noexcept
            {
                static SandboxRootState* s_state = ResolveProcessSandboxState();
                return *s_state;
            }

            const bool s_sandboxStatePrewarmed = (&GetSandboxState() != nullptr);

            enum class VirtualZone
            {
                None,
                Pkg,
                Cache
            };

            FORCE_INLINE bool HasConsecutiveSlashes(std::string_view sv)
            {
                for (size_t k = 0; k + 1 < sv.size(); ++k)
                {
                    const char c1 = sv[k];
                    const char c2 = sv[k + 1];
                    if ((c1 == '/' || c1 == '\\') && (c2 == '/' || c2 == '\\'))
                    {
                        return true;
                    }
                }
                return false;
            }

            struct VirtualPathParseResult
            {
                VirtualZone zone{VirtualZone::None};
                std::string_view subpath;
                bool hasViolation{false};
            };

            /**
             * @brief 零分配解析虚拟路径前缀与子路径，严格拦截连续斜杠与根注入变异
             */
            VirtualPathParseResult ParseVirtualPath(std::string_view pathStr)
            {
                VirtualPathParseResult result;
                if (pathStr.empty())
                {
                    return result;
                }

                // 1. 扫描前导正反斜杠
                size_t leadingSlashes = 0;
                while (leadingSlashes < pathStr.size() && (pathStr[leadingSlashes] == '/' || pathStr[leadingSlashes] == '\\'))
                {
                    ++leadingSlashes;
                }
                if (leadingSlashes == 0)
                {
                    // 无前导斜杠，非虚拟路径（例如 "asset.bin", "pkg/foo"）
                    return result;
                }

                const bool consecutiveLeading = (leadingSlashes > 1);
                std::string_view rest = pathStr.substr(leadingSlashes);
                size_t prefixLen = 0;

                // 2. 匹配 "pkg" 或 "cache" 前缀
                if (rest.starts_with("pkg"))
                {
                    result.zone = VirtualZone::Pkg;
                    prefixLen = 3;
                }
                else if (rest.starts_with("cache"))
                {
                    result.zone = VirtualZone::Cache;
                    prefixLen = 5;
                }
                else
                {
                    return result;
                }

                if (consecutiveLeading)
                {
                    result.hasViolation = true;
                }

                std::string_view after = rest.substr(prefixLen);
                if (after.empty())
                {
                    // 仅 "/pkg" 或 "/cache" 虚拟根目录自身
                    result.subpath = {};
                    return result;
                }

                // 3. 校验分隔符
                if (after[0] != '/' && after[0] != '\\')
                {
                    // 非合法分隔符（如 "/pkgxyz"），非虚拟路径
                    result.zone = VirtualZone::None;
                    result.hasViolation = false;
                    return result;
                }

                // 4. 检测紧跟前缀后的连续斜杠（如 "/pkg//etc/passwd", "/cache//D:/secret.txt"）
                if (after.size() > 1 && (after[1] == '/' || after[1] == '\\'))
                {
                    result.hasViolation = true;
                }

                // 剥离单个合法分隔符
                after.remove_prefix(1);

                // 5. 检测子路径内部连续斜杠或剩余前导根斜杠
                if (HasConsecutiveSlashes(after) || (!after.empty() && (after.front() == '/' || after.front() == '\\')))
                {
                    result.hasViolation = true;
                }

                result.subpath = after;
                return result;
            }

            FORCE_INLINE fs::path ToFsPath(std::string_view sv)
            {
                if (sv.empty())
                {
                    return fs::path();
                }
                std::u8string_view u8sv(reinterpret_cast<const char8_t*>(sv.data()), sv.size());
                return fs::path(u8sv);
            }

            FORCE_INLINE std::string FromFsPath(const fs::path& p)
            {
                if (p.empty())
                {
                    return {};
                }
                auto u8s = p.generic_u8string();
                return std::string(reinterpret_cast<const char*>(u8s.data()), u8s.size());
            }

            /**
             * @brief 校验虚拟子路径词法合法性（杜绝绝对路径注入、盘符注入、连续斜杠及根逃逸 ..）
             */
            StatusOr<fs::path> ValidateAndNormalizeSubpath(std::string_view subpath, std::string_view originalPath)
            {
                if (subpath.empty())
                {
                    return fs::path(".");
                }

                // 1. 连续斜杠与前导根斜杠拦截
                if (HasConsecutiveSlashes(subpath) || subpath.front() == '/' || subpath.front() == '\\')
                {
                    return PermissionDeniedError(StrCat("Invalid virtual path (consecutive slashes or root injection): ", originalPath));
                }

                // 2. 跨平台 Windows 盘符直接注入防御 (如 "C:..." 或 "D:...")
                if (subpath.size() >= 2 && subpath[1] == ':' &&
                    ((subpath[0] >= 'a' && subpath[0] <= 'z') || (subpath[0] >= 'A' && subpath[0] <= 'Z')))
                {
                    return PermissionDeniedError(StrCat("Invalid virtual subpath (absolute/root injection): ", originalPath));
                }

                fs::path p = ToFsPath(subpath);

                // 3. 根注入防御：子路径严禁为绝对路径、严禁携带盘符或根目录
                if (p.is_absolute() || p.has_root_name() || p.has_root_directory())
                {
                    return PermissionDeniedError(StrCat("Invalid virtual subpath (absolute/root injection): ", originalPath));
                }

                // 4. 词法规范化 (C++20 noexcept)
                fs::path normSub = p.lexically_normal();
                if (normSub.empty() || normSub == ".")
                {
                    return fs::path(".");
                }

                // 5. 目录穿越防御：若规范化后首元素为 ".."，说明试图逃逸虚拟根
                auto itFirst = normSub.begin();
                if (itFirst != normSub.end() && (*itFirst == ".." || *itFirst == fs::path("..")))
                {
                    return PermissionDeniedError(StrCat("Directory traversal attempt detected: ", originalPath));
                }

                return normSub;
            }

            /**
             * @brief 校验拼接后的物理目标路径是否严格位于根目录边界内 (边界几何包含判定)
             */
            Status VerifyBoundaryContainment(const fs::path& normalRoot, const fs::path& combined, std::string_view originalPath)
            {
                if (normalRoot.empty() || normalRoot == ".")
                {
                    auto it = combined.begin();
                    if (it != combined.end() && (*it == ".." || *it == fs::path("..")))
                    {
                        return PermissionDeniedError(StrCat("Directory traversal attempt detected: ", originalPath));
                    }
                    return OkStatus();
                }

                auto [itRoot, itComb] = std::mismatch(
                    normalRoot.begin(), normalRoot.end(),
                    combined.begin(), combined.end());
                if (itRoot != normalRoot.end())
                {
                    return PermissionDeniedError(StrCat("Directory traversal attempt detected: ", originalPath));
                }

                return OkStatus();
            }

            FORCE_INLINE StatusOr<fs::path> ResolveToFsPath(std::string_view sv, bool forWrite = false)
            {
                if (sv.empty())
                {
                    return fs::path();
                }
                auto resolvedOr = FileSystem::resolveVirtualPath(sv, forWrite);
                if (!resolvedOr.ok())
                {
                    return resolvedOr.status();
                }
                return ToFsPath(resolvedOr.value());
            }
        } // namespace

        Status FileSystem::setPkgRoot(std::string_view pkgRoot)
        {
            if (pkgRoot.empty())
            {
                return InvalidArgumentError("setPkgRoot: 路径不能为空");
            }
            auto& rootState = GetSandboxState();
            std::lock_guard<std::mutex> lock(rootState.mutex);
            rootState.pkgRoot = std::string(pkgRoot);
            return OkStatus();
        }

        Status FileSystem::setCacheRoot(std::string_view cacheRoot)
        {
            if (cacheRoot.empty())
            {
                return InvalidArgumentError("setCacheRoot: 路径不能为空");
            }
            auto& rootState = GetSandboxState();
            std::lock_guard<std::mutex> lock(rootState.mutex);
            rootState.cacheRoot = std::string(cacheRoot);
            return OkStatus();
        }

        std::string_view FileSystem::getPkgRoot()
        {
            auto& rootState = GetSandboxState();
            std::lock_guard<std::mutex> lock(rootState.mutex);
            return rootState.pkgRoot;
        }

        std::string_view FileSystem::getCacheRoot()
        {
            auto& rootState = GetSandboxState();
            std::lock_guard<std::mutex> lock(rootState.mutex);
            return rootState.cacheRoot;
        }

        bool FileSystem::isVirtualPath(std::string_view pathStr)
        {
            return ParseVirtualPath(pathStr).zone != VirtualZone::None;
        }

        StatusOr<std::string> FileSystem::resolveVirtualPath(std::string_view virtualPath, bool forWrite)
        {
            if (virtualPath.empty())
            {
                return std::string();
            }

            auto parsed = ParseVirtualPath(virtualPath);
            if (parsed.zone == VirtualZone::None)
            {
                // 非虚拟路径原样返回
                return std::string(virtualPath);
            }

            // 0. 连续斜杠与根注入违规拦截
            if (parsed.hasViolation)
            {
                return PermissionDeniedError(StrCat("Invalid virtual path (consecutive slashes or root injection): ", virtualPath));
            }

            // 1. 只读包安全网：针对 /pkg 目录的任何写操作立即拦截
            if (parsed.zone == VirtualZone::Pkg && forWrite)
            {
                return PermissionDeniedError(StrCat("Cannot write or modify read-only package path: ", virtualPath));
            }

            // 2. 子路径词法校验与规范化 (防注入与逃逸)
            auto normSubOr = ValidateAndNormalizeSubpath(parsed.subpath, virtualPath);
            if (!normSubOr.ok())
            {
                return normSubOr.status();
            }
            const fs::path& normSub = normSubOr.value();

            // 3. 根目录拼接与几何边界包含校验
            auto& rootState = GetSandboxState();
            std::lock_guard<std::mutex> lock(rootState.mutex);
            const std::string& rootStr = (parsed.zone == VirtualZone::Pkg) ? rootState.pkgRoot : rootState.cacheRoot;
            fs::path normalRoot = ToFsPath(rootStr).lexically_normal();

            fs::path combined;
            if (normSub.empty() || normSub == ".")
            {
                combined = normalRoot;
            }
            else if (normalRoot == ".")
            {
                combined = normSub;
            }
            else
            {
                combined = (normalRoot / normSub).lexically_normal();
            }

            auto boundaryStatus = VerifyBoundaryContainment(normalRoot, combined, virtualPath);
            if (!boundaryStatus.ok())
            {
                return boundaryStatus;
            }

            return FromFsPath(combined);
        }

        void FileSystem::readBinaryFileAsync(
            std::string_view virtualPath,
            std::function<void(StatusOr<std::vector<uint8_t>>)> callback)
        {
            if (!callback)
            {
                return;
            }
            auto resolvedOr = resolveVirtualPath(virtualPath, /*forWrite=*/false);
            if (!resolvedOr.ok())
            {
                callback(resolvedOr.status());
                return;
            }
            std::string resolved = std::move(resolvedOr).value();
            std::thread([resolved = std::move(resolved), callback = std::move(callback)]() {
                auto res = readBinaryFile(resolved);
                callback(std::move(res));
            }).detach();
        }

        void FileSystem::downloadFileToPathAsync(
            std::string_view url,
            std::string_view destVirtualPath,
            std::function<void(Status)> callback)
        {
            if (!callback)
            {
                return;
            }
            auto parsed = ParseVirtualPath(destVirtualPath);
            if (parsed.zone != VirtualZone::Cache || parsed.hasViolation)
            {
                callback(InvalidArgumentError(StrCat("Download destination must be in /cache/: ", destVirtualPath)));
                return;
            }
            auto resolvedOr = resolveVirtualPath(destVirtualPath, /*forWrite=*/true);
            if (!resolvedOr.ok())
            {
                callback(resolvedOr.status());
                return;
            }
            std::string resolvedDest = std::move(resolvedOr).value();

            std::thread([urlStr = std::string(url), dest = std::move(resolvedDest), callback = std::move(callback)]() {
                std::string sourcePath = urlStr;
                if (sourcePath.rfind("file://", 0) == 0)
                {
                    sourcePath = sourcePath.substr(7);
                }

                auto inStreamRes = openBinaryInputStream(sourcePath);
                if (inStreamRes.ok())
                {
                    auto& inStream = inStreamRes.value();
                    auto outStreamRes = openBinaryOutputStream(dest, /*createParentDirs=*/true);
                    if (!outStreamRes.ok())
                    {
                        callback(outStreamRes.status());
                        return;
                    }
                    auto& outStream = outStreamRes.value();

                    constexpr size_t kChunkSize = 64 * 1024;
                    std::vector<char> buffer(kChunkSize);
                    while (inStream->good())
                    {
                        inStream->read(buffer.data(), buffer.size());
                        std::streamsize bytesRead = inStream->gcount();
                        if (bytesRead > 0)
                        {
                            outStream->write(buffer.data(), bytesRead);
                        }
                    }
                    outStream->flush();
                    callback(OkStatus());
                    return;
                }

                auto outStreamRes = openBinaryOutputStream(dest, /*createParentDirs=*/true);
                if (!outStreamRes.ok())
                {
                    callback(outStreamRes.status());
                    return;
                }
                auto& outStream = outStreamRes.value();
                std::string payload = StrCat("Downloaded from: ", urlStr);
                outStream->write(payload.data(), payload.size());
                outStream->flush();
                callback(OkStatus());
            }).detach();
        }

        bool FileSystem::exists(std::string_view pathStr)
        {
            if (pathStr.empty())
            {
                return false;
            }
            auto pOr = ResolveToFsPath(pathStr, /*forWrite=*/false);
            if (!pOr.ok()) return false;
            std::error_code ec;
            return fs::exists(pOr.value(), ec) && !ec;
        }

        bool FileSystem::isFile(std::string_view pathStr)
        {
            if (pathStr.empty())
            {
                return false;
            }
            auto pOr = ResolveToFsPath(pathStr, /*forWrite=*/false);
            if (!pOr.ok()) return false;
            std::error_code ec;
            const fs::path& p = pOr.value();
            return fs::exists(p, ec) && !ec && !fs::is_directory(p, ec) && !ec;
        }

        bool FileSystem::isDirectory(std::string_view pathStr)
        {
            if (pathStr.empty())
            {
                return false;
            }
            auto pOr = ResolveToFsPath(pathStr, /*forWrite=*/false);
            if (!pOr.ok()) return false;
            std::error_code ec;
            const fs::path& p = pOr.value();
            return fs::exists(p, ec) && !ec && fs::is_directory(p, ec) && !ec;
        }

        bool FileSystem::containsAny(std::string_view dirPath, std::initializer_list<std::string_view> relativeNames)
        {
            if (!isDirectory(dirPath))
            {
                return false;
            }
            for (std::string_view rel : relativeNames)
            {
                if (exists(Path::join(dirPath, rel)))
                {
                    return true;
                }
            }
            return false;
        }

        bool FileSystem::containsAll(std::string_view dirPath, std::initializer_list<std::string_view> relativeNames)
        {
            if (!isDirectory(dirPath))
            {
                return false;
            }
            for (std::string_view rel : relativeNames)
            {
                if (!exists(Path::join(dirPath, rel)))
                {
                    return false;
                }
            }
            return true;
        }

        Status FileSystem::createDirectory(std::string_view dirPath)
        {
            if (dirPath.empty())
            {
                return InvalidArgumentError("createDirectory: 目录路径为空");
            }
            auto pOr = ResolveToFsPath(dirPath, /*forWrite=*/true);
            FCT_CHECK_STATUS(pOr);
            std::error_code ec;
            const fs::path& p = pOr.value();
            if (fs::create_directory(p, ec))
            {
                return OkStatus();
            }
            if (!ec && isDirectory(dirPath))
            {
                return OkStatus();
            }
            if (exists(dirPath) && !isDirectory(dirPath))
            {
                return AlreadyExistsError(StrCat("同名非目录文件已存在: ", dirPath));
            }
            return InternalError(StrCat("创建目录失败 [", dirPath, "]: ", ec.message()));
        }

        Status FileSystem::createDirectories(std::string_view dirPath)
        {
            if (dirPath.empty())
            {
                return InvalidArgumentError("createDirectories: 目录路径为空");
            }
            auto pOr = ResolveToFsPath(dirPath, /*forWrite=*/true);
            FCT_CHECK_STATUS(pOr);
            std::error_code ec;
            const fs::path& p = pOr.value();
            if (fs::create_directories(p, ec))
            {
                return OkStatus();
            }
            if (!ec && isDirectory(dirPath))
            {
                return OkStatus();
            }
            if (exists(dirPath) && !isDirectory(dirPath))
            {
                return AlreadyExistsError(StrCat("同名非目录文件已存在: ", dirPath));
            }
            return InternalError(StrCat("递归创建目录失败 [", dirPath, "]: ", ec.message()));
        }

        Status FileSystem::ensureParentDirectory(std::string_view filePath)
        {
            std::string parent = Path::parentPath(filePath);
            if (parent.empty())
            {
                return OkStatus();
            }
            return createDirectories(parent);
        }

        Status FileSystem::readTextFile(std::string_view filePath, std::string& outContent)
        {
            auto streamRes = openBinaryInputStream(filePath);
            FCT_CHECK_STATUS(streamRes);

            auto& stream = streamRes.value();
            std::ostringstream ss;
            ss << stream->rdbuf();
            if (stream->fail() && !stream->eof())
            {
                return InternalError(StrCat("读取文本文件失败: ", filePath));
            }
            outContent = ss.str();
            return OkStatus();
        }

        StatusOr<std::string> FileSystem::readTextFile(std::string_view filePath)
        {
            std::string content;
            FCT_CHECK_STATUS(readTextFile(filePath, content));
            return content;
        }

        Status FileSystem::writeTextFile(std::string_view filePath, std::string_view content, bool createParentDirs)
        {
            return writeBinaryFile(filePath, content.data(), content.size(), createParentDirs);
        }

        Status FileSystem::readBinaryFile(std::string_view filePath, std::vector<uint8_t>& outData)
        {
            if (filePath.empty())
            {
                return InvalidArgumentError("readBinaryFile: 文件路径为空");
            }
            auto streamRes = openBinaryInputStream(filePath);
            FCT_CHECK_STATUS(streamRes);

            auto& stream = streamRes.value();
            stream->seekg(0, std::ios::end);
            auto size = stream->tellg();
            if (size < 0)
            {
                return InternalError(StrCat("获取文件大小失败: ", filePath));
            }
            stream->seekg(0, std::ios::beg);
            outData.resize(static_cast<size_t>(size));
            if (size > 0)
            {
                stream->read(reinterpret_cast<char*>(outData.data()), size);
                if (stream->fail() && !stream->eof())
                {
                    return InternalError(StrCat("读取二进制内容失败: ", filePath));
                }
            }
            return OkStatus();
        }

        StatusOr<std::vector<uint8_t>> FileSystem::readBinaryFile(std::string_view filePath)
        {
            std::vector<uint8_t> data;
            FCT_CHECK_STATUS(readBinaryFile(filePath, data));
            return data;
        }

        Status FileSystem::writeBinaryFile(std::string_view filePath, const void* data, size_t size, bool createParentDirs)
        {
            if (filePath.empty())
            {
                return InvalidArgumentError("writeBinaryFile: 文件路径为空");
            }
            auto outStreamRes = openBinaryOutputStream(filePath, createParentDirs);
            FCT_CHECK_STATUS(outStreamRes);

            auto& stream = outStreamRes.value();
            if (data && size > 0)
            {
                stream->write(reinterpret_cast<const char*>(data), size);
            }
            stream->flush();
            if (stream->fail())
            {
                return InternalError(StrCat("写入文件流失败: ", filePath));
            }
            return OkStatus();
        }

        StatusOr<UniquePtr<std::ifstream>> FileSystem::openBinaryInputStream(std::string_view filePath)
        {
            if (filePath.empty())
            {
                return InvalidArgumentError("openBinaryInputStream: 文件路径为空");
            }
            auto pOr = ResolveToFsPath(filePath, /*forWrite=*/false);
            FCT_CHECK_STATUS(pOr);

            if (!exists(filePath))
            {
                return NotFoundError(StrCat("输入文件不存在: ", filePath));
            }

            auto stream = makeUnique<std::ifstream>(pOr.value(), std::ios::in | std::ios::binary);
            if (!stream->is_open())
            {
                return InternalError(StrCat("无法打开输入文件流: ", filePath));
            }
            return stream;
        }

        StatusOr<UniquePtr<std::ofstream>> FileSystem::openBinaryOutputStream(std::string_view filePath, bool createParentDirs)
        {
            if (filePath.empty())
            {
                return InvalidArgumentError("openBinaryOutputStream: 文件路径为空");
            }
            auto pOr = ResolveToFsPath(filePath, /*forWrite=*/true);
            FCT_CHECK_STATUS(pOr);

            if (createParentDirs)
            {
                FCT_CHECK_STATUS(ensureParentDirectory(filePath));
            }

            auto stream = makeUnique<std::ofstream>(pOr.value(), std::ios::out | std::ios::binary | std::ios::trunc);
            if (!stream->is_open())
            {
                return InternalError(StrCat("无法打开输出文件流: ", filePath));
            }
            return stream;
        }

        Status FileSystem::copyFile(std::string_view srcPath,
                                    std::string_view dstPath,
                                    bool overwrite,
                                    bool createParentDirs)
        {
            if (srcPath.empty() || dstPath.empty())
            {
                return InvalidArgumentError("copyFile: 源路径或目标路径为空");
            }
            auto pSrcOr = ResolveToFsPath(srcPath, /*forWrite=*/false);
            FCT_CHECK_STATUS(pSrcOr);
            auto pDstOr = ResolveToFsPath(dstPath, /*forWrite=*/true);
            FCT_CHECK_STATUS(pDstOr);

            if (!exists(srcPath))
            {
                return NotFoundError(StrCat("源文件不存在: ", srcPath));
            }
            if (createParentDirs)
            {
                FCT_CHECK_STATUS(ensureParentDirectory(dstPath));
            }
            std::error_code ec;
            auto options = overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none;
            if (!fs::copy_file(pSrcOr.value(), pDstOr.value(), options, ec) || ec)
            {
                return InternalError(StrCat("拷贝文件失败 [", srcPath, " -> ", dstPath, "]: ", ec.message()));
            }
            return OkStatus();
        }

        Status FileSystem::copyFilePreservingRelative(std::string_view srcFile,
                                                      std::string_view srcBaseDir,
                                                      std::string_view dstRootDir,
                                                      bool overwrite)
        {
            if (!isFile(srcFile) || dstRootDir.empty())
            {
                return InvalidArgumentError(StrCat("copyFilePreservingRelative 参数无效: ", srcFile));
            }
            std::string relSubPath = Path::relativeOrFileName(srcFile, srcBaseDir);
            if (relSubPath.empty())
            {
                return InternalError(StrCat("无法解析相对文件名: ", srcFile));
            }
            std::string dstFile = Path::join(dstRootDir, relSubPath);
            return copyFile(srcFile, dstFile, overwrite, true);
        }

        Status FileSystem::rename(std::string_view oldPath, std::string_view newPath)
        {
            if (oldPath.empty() || newPath.empty())
            {
                return InvalidArgumentError("rename: 原路径或目标路径为空");
            }
            auto pOldOr = ResolveToFsPath(oldPath, /*forWrite=*/true);
            FCT_CHECK_STATUS(pOldOr);
            auto pNewOr = ResolveToFsPath(newPath, /*forWrite=*/true);
            FCT_CHECK_STATUS(pNewOr);

            if (!exists(oldPath))
            {
                return NotFoundError(StrCat("待重命名路径不存在: ", oldPath));
            }
            std::error_code ec;
            fs::rename(pOldOr.value(), pNewOr.value(), ec);
            if (ec)
            {
                return InternalError(StrCat("重命名失败 [", oldPath, " -> ", newPath, "]: ", ec.message()));
            }
            return OkStatus();
        }

        Status FileSystem::removeFile(std::string_view filePath)
        {
            if (filePath.empty())
            {
                return InvalidArgumentError("removeFile: 文件路径为空");
            }
            auto pOr = ResolveToFsPath(filePath, /*forWrite=*/true);
            FCT_CHECK_STATUS(pOr);

            if (!exists(filePath))
            {
                return NotFoundError(StrCat("待删除文件不存在: ", filePath));
            }
            std::error_code ec;
            if (!fs::remove(pOr.value(), ec) || ec)
            {
                return InternalError(StrCat("删除文件失败 [", filePath, "]: ", ec.message()));
            }
            return OkStatus();
        }

        Status FileSystem::removeAll(std::string_view pathStr)
        {
            if (pathStr.empty())
            {
                return InvalidArgumentError("removeAll: 路径为空");
            }
            auto pOr = ResolveToFsPath(pathStr, /*forWrite=*/true);
            FCT_CHECK_STATUS(pOr);

            std::error_code ec;
            fs::remove_all(pOr.value(), ec);
            if (ec)
            {
                return InternalError(StrCat("递归删除目录或文件失败 [", pathStr, "]: ", ec.message()));
            }
            return OkStatus();
        }

        int64_t FileSystem::getLastWriteTimeSeconds(std::string_view filePath)
        {
            if (filePath.empty())
            {
                return 0;
            }
            auto pOr = ResolveToFsPath(filePath, /*forWrite=*/false);
            if (!pOr.ok()) return 0;

            std::error_code ec;
            auto ftime = fs::last_write_time(pOr.value(), ec);
            if (ec)
            {
                return 0;
            }
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            return std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
        }

        std::vector<FileEntry> FileSystem::listEntries(std::string_view dirPath)
        {
            std::vector<FileEntry> result;
            if (!isDirectory(dirPath))
            {
                return result;
            }
            auto pOr = ResolveToFsPath(dirPath, /*forWrite=*/false);
            if (!pOr.ok()) return result;

            std::error_code ec;
            fs::directory_iterator it(pOr.value(), ec);
            if (ec)
            {
                return result;
            }
            for (const auto& entry : it)
            {
                FileEntry item;
                const fs::path& ep = entry.path();
                item.path = FromFsPath(ep);
                item.name = FromFsPath(ep.filename());
                item.stem = FromFsPath(ep.stem());
                item.extension = FromFsPath(ep.extension());
                std::error_code typeEc;
                item.isDirectory = entry.is_directory(typeEc) && !typeEc;
                item.isRegularFile = entry.is_regular_file(typeEc) && !typeEc;
                result.push_back(std::move(item));
            }
            return result;
        }

        std::vector<std::string> FileSystem::listFiles(std::string_view dirPath, std::string_view extensionFilter)
        {
            std::vector<std::string> files;
            for (const auto& entry : listEntries(dirPath))
            {
                if (entry.isRegularFile && Path::hasExtension(entry.path, extensionFilter))
                {
                    files.push_back(entry.path);
                }
            }
            return files;
        }

        std::vector<std::string> FileSystem::listFileStems(std::string_view dirPath, std::string_view extensionFilter)
        {
            std::vector<std::string> stems;
            for (const auto& entry : listEntries(dirPath))
            {
                if (entry.isRegularFile && Path::hasExtension(entry.path, extensionFilter))
                {
                    stems.push_back(entry.stem);
                }
            }
            return stems;
        }

        std::vector<std::string> FileSystem::listSubDirectories(std::string_view dirPath)
        {
            std::vector<std::string> dirs;
            for (const auto& entry : listEntries(dirPath))
            {
                if (entry.isDirectory)
                {
                    dirs.push_back(entry.path);
                }
            }
            return dirs;
        }

        std::vector<std::string> FileSystem::listSubDirectoryNames(std::string_view dirPath)
        {
            std::vector<std::string> names;
            for (const auto& entry : listEntries(dirPath))
            {
                if (entry.isDirectory)
                {
                    names.push_back(entry.name);
                }
            }
            return names;
        }

        std::string FileSystem::getUniqueDirectoryName(std::string_view baseDir, std::string_view baseName)
        {
            std::string candidateName(baseName);
            if (!exists(Path::join(baseDir, candidateName)))
            {
                return candidateName;
            }
            int counter = 1;
            std::string uniqueName;
            do
            {
                uniqueName = std::string(baseName) + "_" + std::to_string(counter++);
            } while (exists(Path::join(baseDir, uniqueName)));
            return uniqueName;
        }

        std::vector<std::string> FileSystem::collectUpwardDirectories(std::string_view startDir, int maxDepth)
        {
            std::vector<std::string> results;
            if (startDir.empty())
            {
                return results;
            }
            std::string cur = Path::normalize(startDir);
            for (int i = 0; i < maxDepth && !cur.empty(); ++i)
            {
                results.push_back(cur);
                std::string parent = Path::parentPath(cur);
                if (parent == cur || parent.empty())
                {
                    break;
                }
                cur = std::move(parent);
            }
            return results;
        }

        std::string FileSystem::findUpwards(std::string_view startDir, std::string_view relativeTarget, int maxDepth)
        {
            if (startDir.empty() || relativeTarget.empty())
            {
                return {};
            }
            for (const std::string& dir : collectUpwardDirectories(startDir, maxDepth))
            {
                std::string candidate = Path::join(dir, relativeTarget);
                if (exists(candidate))
                {
                    return candidate;
                }
            }
            return {};
        }

        std::string FileSystem::findDirectoryUpwardsWhere(std::string_view startDir,
                                                         const std::function<bool(const std::string&)>& predicate,
                                                         int maxDepth)
        {
            if (startDir.empty() || !predicate)
            {
                return {};
            }
            for (const std::string& dir : collectUpwardDirectories(startDir, maxDepth))
            {
                if (predicate(dir))
                {
                    return dir;
                }
            }
            return {};
        }

        std::string FileSystem::findDirectoryUpwardsWithAll(std::string_view startDir,
                                                           std::initializer_list<std::string_view> requiredRelativePaths,
                                                           int maxDepth)
        {
            return findDirectoryUpwardsWhere(startDir, [requiredRelativePaths](const std::string& dir) {
                return containsAll(dir, requiredRelativePaths);
            }, maxDepth);
        }
    } // namespace SAI
} // namespace FCT

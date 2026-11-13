#include "Path.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace FCT
{
    namespace SAI
    {
        namespace
        {
            namespace fs = std::filesystem;

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

            std::string ToLowerAscii(std::string_view sv)
            {
                std::string res(sv);
                std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                return res;
            }

            std::string NormalizeExtensionDot(std::string_view ext)
            {
                if (ext.empty())
                {
                    return {};
                }
                std::string lower = ToLowerAscii(ext);
                if (lower.front() != '.')
                {
                    lower.insert(lower.begin(), '.');
                }
                return lower;
            }
        } // namespace

        std::string Path::normalize(std::string_view pathStr)
        {
            if (pathStr.empty())
            {
                return {};
            }
            try
            {
                fs::path p = ToFsPath(pathStr);
                std::error_code ec;
                fs::path canonical = fs::weakly_canonical(p, ec);
                if (!ec && !canonical.empty())
                {
                    return FromFsPath(canonical);
                }
                return FromFsPath(p.lexically_normal());
            }
            catch (...)
            {
                return std::string(pathStr);
            }
        }

        std::string Path::lexicallyNormal(std::string_view pathStr)
        {
            if (pathStr.empty())
            {
                return {};
            }
            try
            {
                return FromFsPath(ToFsPath(pathStr).lexically_normal());
            }
            catch (...)
            {
                return std::string(pathStr);
            }
        }

        std::string Path::join(std::string_view base, std::string_view child)
        {
            if (base.empty())
            {
                return FromFsPath(ToFsPath(child));
            }
            if (child.empty())
            {
                return FromFsPath(ToFsPath(base));
            }
            try
            {
                fs::path joined = ToFsPath(base) / ToFsPath(child);
                return FromFsPath(joined);
            }
            catch (...)
            {
                return std::string(base) + "/" + std::string(child);
            }
        }

        std::string Path::resolve(std::string_view baseDir, std::string_view relativeOrAbsolutePath)
        {
            if (relativeOrAbsolutePath.empty())
            {
                return normalize(baseDir);
            }
            try
            {
                fs::path p = ToFsPath(relativeOrAbsolutePath);
                if (p.is_absolute() || baseDir.empty() || baseDir == ".")
                {
                    return FromFsPath(p.lexically_normal());
                }
                fs::path base = ToFsPath(baseDir);
                return FromFsPath((base / p).lexically_normal());
            }
            catch (...)
            {
                if (baseDir.empty() || baseDir == ".")
                {
                    return std::string(relativeOrAbsolutePath);
                }
                return std::string(baseDir) + "/" + std::string(relativeOrAbsolutePath);
            }
        }

        std::string Path::relative(std::string_view targetPath, std::string_view baseDir)
        {
            if (targetPath.empty() || baseDir.empty())
            {
                return {};
            }
            try
            {
                std::error_code ec;
                fs::path rel = fs::relative(ToFsPath(targetPath), ToFsPath(baseDir), ec);
                if (ec || rel.empty())
                {
                    return {};
                }
                std::string relStr = FromFsPath(rel);
                if (relStr.starts_with("..") || relStr == ".")
                {
                    return {};
                }
                return relStr;
            }
            catch (...)
            {
                return {};
            }
        }

        std::string Path::relativeOrFileName(std::string_view targetPath, std::string_view baseDir)
        {
            std::string rel = relative(targetPath, baseDir);
            if (!rel.empty())
            {
                return rel;
            }
            return fileName(targetPath);
        }

        std::string Path::parentPath(std::string_view pathStr)
        {
            if (pathStr.empty())
            {
                return {};
            }
            try
            {
                fs::path p = ToFsPath(pathStr);
                return p.has_parent_path() ? FromFsPath(p.parent_path()) : std::string{};
            }
            catch (...)
            {
                return {};
            }
        }

        std::string Path::directoryOrDot(std::string_view pathStr)
        {
            std::string dir = parentPath(pathStr);
            return dir.empty() ? "." : dir;
        }

        std::string Path::fileName(std::string_view pathStr)
        {
            if (pathStr.empty())
            {
                return {};
            }
            try
            {
                return FromFsPath(ToFsPath(pathStr).filename());
            }
            catch (...)
            {
                return std::string(pathStr);
            }
        }

        std::string Path::stem(std::string_view pathStr)
        {
            if (pathStr.empty())
            {
                return {};
            }
            try
            {
                return FromFsPath(ToFsPath(pathStr).stem());
            }
            catch (...)
            {
                return std::string(pathStr);
            }
        }

        std::string Path::extension(std::string_view pathStr)
        {
            if (pathStr.empty())
            {
                return {};
            }
            try
            {
                return FromFsPath(ToFsPath(pathStr).extension());
            }
            catch (...)
            {
                return {};
            }
        }

        std::string Path::extensionLower(std::string_view pathStr)
        {
            return ToLowerAscii(extension(pathStr));
        }

        bool Path::hasExtension(std::string_view pathStr, std::string_view ext)
        {
            if (ext.empty())
            {
                return true;
            }
            return extensionLower(pathStr) == NormalizeExtensionDot(ext);
        }

        bool Path::hasAnyExtension(std::string_view pathStr, std::initializer_list<std::string_view> exts)
        {
            std::string actualExt = extensionLower(pathStr);
            for (std::string_view candidate : exts)
            {
                if (actualExt == NormalizeExtensionDot(candidate))
                {
                    return true;
                }
            }
            return false;
        }

        std::string Path::ensureExtension(std::string_view pathStr,
                                          std::string_view defaultExt,
                                          std::initializer_list<std::string_view> acceptedExts)
        {
            if (acceptedExts.size() > 0 && hasAnyExtension(pathStr, acceptedExts))
            {
                return std::string(pathStr);
            }
            if (hasExtension(pathStr, defaultExt))
            {
                return std::string(pathStr);
            }
            std::string extWithDot = defaultExt.starts_with('.') ? std::string(defaultExt) : ("." + std::string(defaultExt));
            return std::string(pathStr) + extWithDot;
        }

        bool Path::isAbsolute(std::string_view pathStr)
        {
            if (pathStr.empty())
            {
                return false;
            }
            try
            {
                return ToFsPath(pathStr).is_absolute();
            }
            catch (...)
            {
                return false;
            }
        }
    } // namespace SAI
} // namespace FCT

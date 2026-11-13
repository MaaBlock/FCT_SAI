#include "System.h"
#include "FileSystem.h"
#include "Path.h"
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>
#endif

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
        } // namespace

        std::string System::getCurrentDirectory()
        {
            std::error_code ec;
            fs::path cur = fs::current_path(ec);
            if (ec)
            {
                return {};
            }
            return Path::normalize(FromFsPath(cur));
        }

        Status System::setCurrentDirectory(std::string_view dirPath)
        {
            if (dirPath.empty())
            {
                return InvalidArgumentError("setCurrentDirectory: 目录路径为空");
            }
            if (!FileSystem::isDirectory(dirPath))
            {
                return NotFoundError(StrCat("目标工作目录不存在: ", dirPath));
            }
            std::error_code ec;
            fs::current_path(ToFsPath(dirPath), ec);
            if (ec)
            {
                return InternalError(StrCat("切换工作目录失败 [", dirPath, "]: ", ec.message()));
            }
            return OkStatus();
        }

        std::string System::getExecutablePath()
        {
#ifdef _WIN32
            wchar_t moduleBuf[MAX_PATH] = {};
            DWORD len = GetModuleFileNameW(nullptr, moduleBuf, MAX_PATH);
            if (len > 0 && len < MAX_PATH)
            {
                return Path::normalize(FromFsPath(fs::path(moduleBuf)));
            }
            return {};
#elif defined(__linux__)
            char buf[PATH_MAX] = {};
            ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
            if (len > 0)
            {
                buf[len] = '\0';
                return Path::normalize(buf);
            }
            return {};
#else
            return {};
#endif
        }

        std::string System::getExecutableDirectory()
        {
            return Path::parentPath(getExecutablePath());
        }

        std::string System::getExecutableAncestorDirectory(int levelsUp)
        {
            std::string dir = getExecutableDirectory();
            for (int i = 0; i < levelsUp && !dir.empty(); ++i)
            {
                std::string parent = Path::parentPath(dir);
                if (parent.empty() || parent == dir)
                {
                    return {};
                }
                dir = parent;
            }
            return dir;
        }

        Status System::spawnDetachedProcess(std::string_view exePath,
                                            const std::vector<std::string>& args,
                                            std::string_view workingDir)
        {
            if (exePath.empty())
            {
                return InvalidArgumentError("spawnDetachedProcess: 可执行文件路径为空");
            }
#ifdef _WIN32
            fs::path exeFsPath = ToFsPath(exePath);
            fs::path workFsDir = workingDir.empty() ? exeFsPath.parent_path() : ToFsPath(workingDir);

            std::wstring cmdLine = L"\"" + exeFsPath.wstring() + L"\"";
            for (const auto& arg : args)
            {
                std::wstring argW = ToFsPath(arg).wstring();
                if (argW.find(L' ') != std::wstring::npos || argW.find(L'\t') != std::wstring::npos)
                {
                    cmdLine += L" \"" + argW + L"\"";
                }
                else
                {
                    cmdLine += L" " + argW;
                }
            }

            std::wstring workDirW = workFsDir.wstring();
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};

            std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
            cmdBuf.push_back(L'\0');

            BOOL ok = CreateProcessW(
                exeFsPath.c_str(),
                cmdBuf.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                workDirW.empty() ? nullptr : workDirW.c_str(),
                &si,
                &pi);

            if (!ok)
            {
                return InternalError(StrCat("CreateProcessW 调用失败 [", exePath, "]，Windows 错误码: ", GetLastError()));
            }

            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return OkStatus();
#else
            (void)args;
            (void)workingDir;
            return UnimplementedError("当前平台暂未实现子进程启动");
#endif
        }
    } // namespace SAI
} // namespace FCT

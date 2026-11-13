#ifndef FCT_SAI_SYSTEM_H
#define FCT_SAI_SYSTEM_H

#include <FCT_Base.h>
#include <string>
#include <string_view>
#include <vector>

namespace FCT
{
    namespace SAI
    {
        /**
         * @brief 跨平台操作系统级抽象接口，统一管理工作目录、可执行程序路径探测及外部进程拉起；可失败操作返回 Status
         */
        class System
        {
        public:
            /**
             * @brief 获取当前进程工作目录（返回规范化后的 UTF-8 路径）
             */
            static std::string getCurrentDirectory();

            /**
             * @brief 设置当前进程工作目录
             */
            static Status setCurrentDirectory(std::string_view dirPath);

            /**
             * @brief 获取当前可执行文件完整路径（返回规范化后的 UTF-8 路径）
             */
            static std::string getExecutablePath();

            /**
             * @brief 获取当前可执行文件所在目录路径
             */
            static std::string getExecutableDirectory();

            /**
             * @brief 获取当前可执行文件目录向上回溯 levelsUp 层的祖先目录（例如构建根目录探测）
             */
            static std::string getExecutableAncestorDirectory(int levelsUp = 1);

            /**
             * @brief 异步拉起外部可执行进程（自动处理平台宽字符编码与参数转义）
             */
            static Status spawnDetachedProcess(std::string_view exePath,
                                               const std::vector<std::string>& args,
                                               std::string_view workingDir = "");
        };
    } // namespace SAI

    using SAISystem = SAI::System;
} // namespace FCT

#endif // FCT_SAI_SYSTEM_H

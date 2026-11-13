#ifndef FCT_SAI_PATH_SEARCHER_H
#define FCT_SAI_PATH_SEARCHER_H

#include <FCT_Base.h>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace FCT
{
    namespace SAI
    {
        /**
         * @brief 声明式多候选路径搜索器，用于插件、用户代码动态库、脚本及可执行文件的高级多层级探测
         */
        class PathSearcher
        {
        public:
            /**
             * @brief 添加单个候选路径
             */
            PathSearcher& addCandidate(std::string_view candidatePath);

            /**
             * @brief 基于指定基准目录批量展开相对候选路径（若 baseDir 为空则忽略）
             */
            PathSearcher& addCandidatesFromBase(std::string_view baseDir,
                                                std::initializer_list<std::string_view> relativePatterns);

            /**
             * @brief 基于当前可执行文件目录批量展开相对候选路径
             */
            PathSearcher& addCandidatesFromExecutableDir(std::initializer_list<std::string_view> relativePatterns);

            /**
             * @brief 基于当前可执行文件向上回溯 levelsUp 层的祖先目录批量展开相对候选路径
             */
            PathSearcher& addCandidatesFromExecutableAncestor(int levelsUp,
                                                              std::initializer_list<std::string_view> relativePatterns);

            /**
             * @brief 自当前工作目录向上逐级回溯（最多 maxDepth 层），在每层目录下展开相对候选路径
             */
            PathSearcher& addCandidatesUpwardFromCurrentDir(int maxDepth,
                                                            std::initializer_list<std::string_view> relativePatterns);

            /**
             * @brief 按顺序查找第一个实际存在的文件，返回规范化后的 UTF-8 路径；未找到则返回 fallback
             */
            std::string resolveFirstFile(std::string_view fallback = "") const;

            /**
             * @brief 按顺序查找第一个满足自定义校验器的候选路径，返回规范化后的 UTF-8 路径；未找到则返回 fallback
             */
            std::string resolveFirstMatching(const std::function<bool(const std::string&)>& validator,
                                             std::string_view fallback = "") const;

            /**
             * @brief 按顺序查找第一个实际存在的文件，成功返回规范化后的 UTF-8 路径，失败返回 NotFoundError
             */
            StatusOr<std::string> findFirstFile() const;

            /**
             * @brief 按顺序查找第一个满足自定义校验器的候选路径，成功返回规范化后的 UTF-8 路径，失败返回 NotFoundError
             */
            StatusOr<std::string> findFirstMatching(const std::function<bool(const std::string&)>& validator) const;

            /**
             * @brief 获取已收集的全部候选路径列表
             */
            FORCE_INLINE const std::vector<std::string>& candidates() const { return m_candidates; }

        private:
            std::vector<std::string> m_candidates;
        };
    } // namespace SAI

    using SAIPathSearcher = SAI::PathSearcher;
} // namespace FCT

#endif // FCT_SAI_PATH_SEARCHER_H

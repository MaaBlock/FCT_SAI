#include "PathSearcher.h"
#include "FileSystem.h"
#include "Path.h"
#include "System.h"

namespace FCT
{
    namespace SAI
    {
        PathSearcher& PathSearcher::addCandidate(std::string_view candidatePath)
        {
            if (!candidatePath.empty())
            {
                m_candidates.emplace_back(candidatePath);
            }
            return *this;
        }

        PathSearcher& PathSearcher::addCandidatesFromBase(std::string_view baseDir,
                                                          std::initializer_list<std::string_view> relativePatterns)
        {
            if (baseDir.empty())
            {
                return *this;
            }
            for (std::string_view pattern : relativePatterns)
            {
                if (!pattern.empty())
                {
                    m_candidates.push_back(Path::join(baseDir, pattern));
                }
            }
            return *this;
        }

        PathSearcher& PathSearcher::addCandidatesFromExecutableDir(std::initializer_list<std::string_view> relativePatterns)
        {
            return addCandidatesFromBase(System::getExecutableDirectory(), relativePatterns);
        }

        PathSearcher& PathSearcher::addCandidatesFromExecutableAncestor(int levelsUp,
                                                                        std::initializer_list<std::string_view> relativePatterns)
        {
            return addCandidatesFromBase(System::getExecutableAncestorDirectory(levelsUp), relativePatterns);
        }

        PathSearcher& PathSearcher::addCandidatesUpwardFromCurrentDir(int maxDepth,
                                                                      std::initializer_list<std::string_view> relativePatterns)
        {
            for (const auto& dir : FileSystem::collectUpwardDirectories(System::getCurrentDirectory(), maxDepth))
            {
                for (std::string_view pattern : relativePatterns)
                {
                    if (!pattern.empty())
                    {
                        m_candidates.push_back(Path::join(dir, pattern));
                    }
                }
            }
            return *this;
        }

        std::string PathSearcher::resolveFirstFile(std::string_view fallback) const
        {
            return resolveFirstMatching(
                [](const std::string& candidate) {
                    return FileSystem::isFile(candidate);
                },
                fallback);
        }

        std::string PathSearcher::resolveFirstMatching(const std::function<bool(const std::string&)>& validator,
                                                       std::string_view fallback) const
        {
            if (!validator)
            {
                return std::string(fallback);
            }
            for (const auto& candidate : m_candidates)
            {
                if (validator(candidate))
                {
                    return Path::normalize(candidate);
                }
            }
            return std::string(fallback);
        }

        StatusOr<std::string> PathSearcher::findFirstFile() const
        {
            return findFirstMatching([](const std::string& candidate) {
                return FileSystem::isFile(candidate);
            });
        }

        StatusOr<std::string> PathSearcher::findFirstMatching(const std::function<bool(const std::string&)>& validator) const
        {
            if (!validator)
            {
                return InvalidArgumentError("findFirstMatching: 校验器为空");
            }
            for (const auto& candidate : m_candidates)
            {
                if (validator(candidate))
                {
                    return Path::normalize(candidate);
                }
            }
            return NotFoundError("未找到匹配的目标路径");
        }
    } // namespace SAI
} // namespace FCT

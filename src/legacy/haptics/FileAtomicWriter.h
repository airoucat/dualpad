#pragma once

#include <filesystem>
#include <functional>
#include <ostream>

namespace dualpad::haptics
{
    class FileAtomicWriter
    {
    public:
        using WriteFn = std::function<bool(std::ostream&)>;

        // 原子写二进制（ios::binary）
        static bool WriteBinary(
            const std::filesystem::path& finalPath,
            const WriteFn& writer,
            const char* logTag = "FileAtomicWriter");

        // 原子写文本
        static bool WriteText(
            const std::filesystem::path& finalPath,
            const WriteFn& writer,
            const char* logTag = "FileAtomicWriter");

    private:
        static bool EnsureParentDir(const std::filesystem::path& p);

        static bool WriteImpl(
            const std::filesystem::path& finalPath,
            const WriteFn& writer,
            std::ios::openmode mode,
            const char* logTag);
    };
}
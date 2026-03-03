#include "pch.h"
#include "haptics/FileAtomicWriter.h"

#include <SKSE/SKSE.h>
#include <fstream>

namespace logger = SKSE::log;

namespace dualpad::haptics
{
    bool FileAtomicWriter::WriteBinary(
        const std::filesystem::path& finalPath,
        const WriteFn& writer,
        const char* logTag)
    {
        return WriteImpl(finalPath, writer, std::ios::binary, logTag);
    }

    bool FileAtomicWriter::WriteText(
        const std::filesystem::path& finalPath,
        const WriteFn& writer,
        const char* logTag)
    {
        return WriteImpl(finalPath, writer, std::ios::openmode(0), logTag);
    }

    bool FileAtomicWriter::EnsureParentDir(const std::filesystem::path& p)
    {
        auto parent = p.parent_path();
        if (parent.empty()) {
            return true;
        }

        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            logger::error("[Haptics][FileAtomicWriter] create dir failed: {} ({})",
                parent.string(), ec.message());
            return false;
        }
        return true;
    }

    bool FileAtomicWriter::WriteImpl(
        const std::filesystem::path& finalPath,
        const WriteFn& writer,
        std::ios::openmode mode,
        const char* logTag)
    {
        if (!writer) {
            logger::error("[Haptics][{}] writer is null", logTag ? logTag : "FileAtomicWriter");
            return false;
        }

        if (!EnsureParentDir(finalPath)) {
            return false;
        }

        std::filesystem::path tmpPath = finalPath;
        tmpPath += ".tmp";

        {
            std::ofstream ofs(tmpPath, std::ios::trunc | mode);
            if (!ofs.is_open()) {
                logger::error("[Haptics][{}] open tmp failed: {}",
                    logTag, tmpPath.string());
                return false;
            }

            if (!writer(ofs)) {
                ofs.close();
                std::error_code ec;
                std::filesystem::remove(tmpPath, ec);
                return false;
            }

            ofs.flush();
            if (!ofs.good()) {
                logger::error("[Haptics][{}] flush tmp failed: {}",
                    logTag, tmpPath.string());
                ofs.close();
                std::error_code ec;
                std::filesystem::remove(tmpPath, ec);
                return false;
            }
        }

        std::error_code ec;
        std::filesystem::remove(finalPath, ec);  // 不存在也无妨
        ec.clear();

        std::filesystem::rename(tmpPath, finalPath, ec);
        if (ec) {
            logger::error("[Haptics][{}] rename tmp->final failed: {} -> {} ({})",
                logTag, tmpPath.string(), finalPath.string(), ec.message());

            std::error_code ec2;
            std::filesystem::remove(tmpPath, ec2);
            return false;
        }

        return true;
    }
}
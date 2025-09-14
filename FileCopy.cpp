#include "FileCopy.h"
#include <filesystem>
#include <thread>
#include <fstream>
#include <atomic>

namespace fs = std::filesystem;

FileCopy::result FileCopy::copy(const std::string& source, const std::string& destination) {
    // for avoiding fucking users trying to copy while another copy is in progress
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        if (copyInProgress) {
            return result::FileCopyInProgress;
        }
    }
    // clear previous copy info
    failedDuringCopy = false;
    copyCanceled = false;
    {
        std::lock_guard<std::mutex> lock(infoMutex);
        clearCopyInfo();
    }
    copyInProgress = true;
    fs::path srcPath(source);
    fs::path destPath(destination);

    // check
    // source not found
    if (!fs::exists(srcPath)) {
        logCopyInfo({ srcPath, destPath, result::SourceNotFound });
        copyInProgress = false;
        return result::SourceNotFound;
    }

    // destination is a file
    if (fs::exists(destPath) && !fs::is_directory(destPath)) {
        logCopyInfo({ srcPath, destPath, result::DestinationIsFile });
        copyInProgress = false;
        return result::DestinationIsFile;
    }

    destPath /= srcPath.filename();

    // source is same as destination
    if (fs::exists(destPath) && fs::equivalent(srcPath, destPath)) {
        logCopyInfo({ srcPath, destPath, result::SourceIsEqualToDestination });
        copyInProgress = false;
        return result::SourceIsEqualToDestination;
    }

    // source is a file
    if (!fs::is_directory(srcPath)) {
        fs::path destFilePath = destPath / srcPath.filename();
        copyFiletoFile(srcPath, destFilePath);
        copyInProgress = false;
        return copyInfos.empty() ? result::NoResult : copyInfos.back().res;
    }

    // destination is subdirectory of source
    if (fs::is_directory(srcPath)) {
        fs::path relative = fs::relative(destPath, srcPath);
        if (relative.empty() == false && relative.string().find("..") != 0) {
            logCopyInfo({ srcPath, destPath, result::SourceIsSubdirectoryOfDestination });
            copyInProgress = false;
            return result::SourceIsSubdirectoryOfDestination;
        }
    }

    
    // destination does not exist, create it
    if (!fs::exists(destPath)) {
        try {
            fs::create_directories(destPath);
        } catch (const fs::filesystem_error& e) {
            logCopyInfo({ srcPath, destPath, result::IOError });
            copyInProgress = false;
            return result::IOError;
        }
    }

    // Pre-check for existing files if mode is Cancel
    for (const auto& entry : fs::directory_iterator(destPath)) {
        fs::path relativePath = fs::relative(entry.path(), srcPath);
        fs::path destFilePath = destPath / relativePath;
        if (fs::exists(destFilePath)) {
            if (copyMode == CopyMode::Cancel) {
                logCopyInfo({ srcPath, destPath, result::FileExistsError });
                copyInProgress = false;
                return result::FileExistsError;
            }
        }
    }
    // Start copying files
    // begin to document copy info if enabled
    
    std::vector<std::thread> threads;
    for (const auto& entry : fs::recursive_directory_iterator(srcPath)) {
        {
            // check if copy is canceled
            std::lock_guard<std::mutex> lock(copyCanceledMutex);
            if (copyCanceled) {
                for (auto& thread : threads) {
                    if (thread.joinable()) {
                        thread.join();
                    }
                }
                withdrawCopiedFiles();
                copyInProgress = false;
                return result::FileExistsError;
            }
        }
        if(fs::is_directory(entry.path())) {
            continue; // skip directories, only copy files
        }

        fs::path relativePath = fs::relative(entry.path(), srcPath);
        fs::path destFilePath = destPath / relativePath;

        while (threads.size() >= maxThreadCount) {
            for (auto it = threads.begin(); it != threads.end(); ) {
                if (it->joinable()) {
                    it->join();
                    it = threads.erase(it);
                } else {
                    ++it;
                }
            }
            std::this_thread::yield();
        }

        threads.emplace_back(copyFiletoFile, this, entry.path(), destFilePath);
    }

    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    copyInProgress = false;
    return failedDuringCopy ? result::ErrorWhenCopying : result::Success;
}

void FileCopy::copyFiletoFile(const fs::path& source, const fs::path& destination) {
    // check

    // source not found
    // or source is a directory (why would this happen?)
    if (!fs::exists(source) || fs::is_directory(source)) {
        logCopyInfo({ source, destination, result::SourceNotFound });
        failedDuringCopy = true;
        return;
    }
    // source is not a regular file and copySystemFiles is false
    if (!fs::is_regular_file(source) && (!copySystemFiles || !fs::is_other(source))) {
        logCopyInfo({ source, destination, result::CopySystemFilesError });
        failedDuringCopy = true;
        return;
    }
    if (fs::exists(destination)) {
        // destination is already a directory
        if (fs::is_directory(destination)) {
            logCopyInfo({ source, destination, result::FileIsSameNameAsDirectory });
            failedDuringCopy = true;
            return;
        }
        // destination is a file and copyMode is Skip
        if (copyMode == CopyMode::Skip) {
            logCopyInfo({ source, destination, result::CopyingSkipped });
            failedDuringCopy = true;
            return;
       } else if (copyMode == CopyMode::Cancel) {
            logCopyInfo({ source, destination, result::FileExistsError });
            std::lock_guard<std::mutex> lock(copyCanceledMutex);
            copyCanceled = true;
            failedDuringCopy = true;
            return;
        }
    } else {
        try {
            fs::create_directories(destination.parent_path());
        } catch (const fs::filesystem_error& e) {
            logCopyInfo({ source, destination, result::IOError });
            return;
        }
    }
    // copy begin
    try {
        copy_file(source, destination, bufferSize);
        logCopyInfo({ source, destination, result::Success });
    } catch (const fs::filesystem_error& e) {
        failedDuringCopy = true;
        logCopyInfo({ source, destination, result::IOError });
    }
}

void FileCopy::withdrawCopiedFiles() {
    std::lock_guard<std::mutex> lock(infoMutex);
    for (const auto& info : copyInfos) {
        if (info.res == result::Success && fs::exists(info.destinationPath)) {
            try {
                fs::remove(info.destinationPath);
                logCopyInfo({ info.sourcePath, info.destinationPath, result::RemovingCopiedFilesSuccess });
            } catch (const fs::filesystem_error& e) {
                // I think this should not happen except users use magic to change the file system
                // log error but continue
                logCopyInfo({ info.sourcePath, info.destinationPath, result::RemovingCopiedFilesError });
            }
        }
    }
}

void FileCopy::cancelCopy() {
    if (copyInProgress == false) return;
    std::lock_guard<std::mutex> lock(copyCanceledMutex);
    copyCanceled = true;
}

void FileCopy::copy_file(const fs::path& source, const fs::path& destination, size_t bufferSize) {
    std::ifstream src(source, std::ios::binary);
    std::ofstream dest(destination, std::ios::binary);
    std::vector<char> buffer(bufferSize);
    while (src) {
        if (copyCanceled) {
            break;
        }
        src.read(buffer.data(), buffer.size());
        dest.write(buffer.data(), src.gcount());
    }
}

void FileCopy::setMaxThreadCount(size_t count) {
    if (count <= 0 || count > 65535) return;
    std::lock_guard<std::mutex> lock(progressMutex);
    if(copyInProgress) return;
    maxThreadCount = count;
}

size_t FileCopy::getMaxThreadCount() const {
    return maxThreadCount;
}

void FileCopy::setCopyMode(CopyMode mode) {
    copyMode = mode;
}

FileCopy::CopyMode FileCopy::getCopyMode() const {
    return copyMode;
}

void FileCopy::setCopySystemFiles(bool copy) {
    std::lock_guard<std::mutex> lock(progressMutex);
    if(copyInProgress) return;
    copySystemFiles = copy;
}

bool FileCopy::getCopySystemFiles() const {
    return copySystemFiles;
}

void FileCopy::setDocumentCopyInfo(bool document) {
    std::lock_guard<std::mutex> lock(progressMutex);
    if(copyInProgress) return;
    documentCopyInfo = document;
}

bool FileCopy::getDocumentCopyInfo() const {
    std::lock_guard<std::mutex> lock(infoMutex);
    return documentCopyInfo;
}

bool FileCopy::isCopyInProgress() const {
    std::lock_guard<std::mutex> lock(progressMutex);
    return copyInProgress;
}

void FileCopy::clearCopyInfo() {
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        if(copyInProgress) return;
    }
    std::lock_guard<std::mutex> lock(infoMutex);
    copyInfos.clear();
}

std::vector<FileCopy::CopyInfo> FileCopy::getCopyInfos() const {
    std::lock_guard<std::mutex> lock(infoMutex);
    return copyInfos;
}

void FileCopy::logCopyInfo(const CopyInfo& info) {
    if (!documentCopyInfo) return;
    std::lock_guard<std::mutex> lock(infoMutex);
    copyInfos.push_back(info);
}

void FileCopy::setBufferSize(size_t size) {
    if (size < MinBufferSize || size > MaxBufferSize) return; // between 1 KB and 100 MB
    std::lock_guard<std::mutex> lock(progressMutex);
    if(copyInProgress) return;
    bufferSize = size;
}

size_t FileCopy::getBufferSize() const {
    return bufferSize;
}

const std::vector<FileCopy::CopyInfo> & FileCopy::getCopyInfo() const {
    std::lock_guard<std::mutex> lock(infoMutex);
    return copyInfos;
}
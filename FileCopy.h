#include <vector>
#include <mutex>
#include <thread>
#include <filesystem>
#include <atomic>
#pragma once


class FileCopy {
public:
    static const size_t MinBufferSize = 1024; // 1 KB
    static const size_t MaxBufferSize = 104857600; // 100 MB
    static const size_t MaxThreadCount = 65535;

    enum class result : short {
        DestinationIsFile,
        SourceNotFound,
        SourceIsEqualToDestination,
        NoResult,
        Success,
        CopySystemFilesError,
        FileExistsError,
        IOError,
        FileIsSameNameAsDirectory,
        FileCopyInProgress,
        SourceIsSubdirectoryOfDestination,
        ErrorWhenCopying,
        RemovingCopiedFilesError,
        RemovingCopiedFilesSuccess,
        CopyingSkipped
    };
    enum class CopyMode {
        Overwrite,
        Skip,
        Cancel
    };

    class CopyInfo {
    public:
        std::filesystem::path sourcePath;
        std::filesystem::path destinationPath;
        result res;
    };

    // copy directory or file from source to destination
    // destination must be a directory
    /*
    Situations to consider:
    1. Source is a file, destination is a directory -> copy file into directory
    2. Source is a directory, destination is a directory -> copy all contents into directory
    3. Destination is a file -> error (cannot copy directory to file)
    4. Source does not exist -> error
    5. Destination does not exist -> create destination directory
    6. Source and destination are the same -> error
    7. Source is a system file, handle based on copySystemFiles setting
    8. File with same name exists in destination, handle based on copyMode setting
    9. Copy operation fails due to IO error or permission error -> error
    10. Copy operation already in progress -> error
    11. File is the same name as a directory in the destination -> error
    12. destination is the subdirectory of source -> error
    strange errors: user change the file system during copying
    */

    FileCopy() = default;
    FileCopy(const FileCopy&) = delete;
    FileCopy& operator=(const FileCopy&) = delete;
    FileCopy(FileCopy&&) = default;
    FileCopy& operator=(FileCopy&&) = default;
    FileCopy(bool copySystemFiles, CopyMode mode, size_t maxThreads = 8, size_t bufferSize = 81920, bool documentInfo = true)
        : copySystemFiles(copySystemFiles), copyMode(mode), maxThreadCount(maxThreads), bufferSize(bufferSize), documentCopyInfo(documentInfo) {}

    // ~FileCopy() = default;

    result copy(const std::string& source, const std::string& destination);

    void setMaxThreadCount(size_t count);
    size_t getMaxThreadCount() const;
    void setCopyMode(CopyMode mode);
    CopyMode getCopyMode() const;
    void setCopySystemFiles(bool copy);
    bool getCopySystemFiles() const;
    void setDocumentCopyInfo(bool document);
    bool getDocumentCopyInfo() const;
    bool isCopyInProgress() const;
    void setBufferSize(size_t size);
    size_t getBufferSize() const;
    const std::vector<CopyInfo> & getCopyInfo() const;

    // use the cancelCopy() when user launch copy() asynchronously and want to cancel the copy operation
    void cancelCopy();
    
    void clearCopyInfo();
    std::vector<CopyInfo> getCopyInfos() const;

private:
    size_t maxThreadCount = 8;
    size_t bufferSize = 81920; // 80 KB
    CopyMode copyMode = CopyMode::Skip;
    bool copySystemFiles = false;
    bool documentCopyInfo = true;
    bool copyInProgress = false;
    bool failedDuringCopy = false;
    std::mutex copyCanceledMutex;
    std::atomic<bool> copyCanceled = false;

    std::vector<CopyInfo> copyInfos;
    mutable std::mutex infoMutex;
    mutable std::mutex progressMutex;

    void logCopyInfo(const CopyInfo& info);

    // copy file to file
    // destination must be a file path
    // used internally by copy() as a thread worker
    void copyFiletoFile(const std::filesystem::path& source, const std::filesystem::path& destination);

    // a function like std::filesystem::copy_file but can stop in the middle when copyCanceled is true
    void copy_file(const std::filesystem::path& source, const std::filesystem::path& destination, size_t bufferSize = 81920);

    // used when copyMode is Cancel and files have been copied
    // normally this situation should not happen
    // but user may call copy() with Cancel mode then use other approach to copying files into destination
    // in this case, the check in copy will not catch this
    // but we should still handle it gracefully
    void withdrawCopiedFiles();
};
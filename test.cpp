#include "FileCopy.h"
#include <iostream>

using namespace std;

int main() {
    clock_t startTime = clock();
    FileCopy fileCopier(true, FileCopy::CopyMode::Overwrite, 100, 16384, true); // 16 KB buffer size
    FileCopy::result res = fileCopier.copy("D:\\WPS Office", "D:\\WPS Office");

    auto infos = fileCopier.getCopyInfos();
    // for (const auto& info : infos) {
    //     cout << "Source: " << info.sourcePath << ", Destination: " << info.destinationPath
    //          << ", Result: " << static_cast<int>(info.res) << endl;
    // }
    if (res == FileCopy::result::Success) {
        cout << "Files copied successfully." << endl;
    } else {
        cout << "Error during file copy: " << static_cast<int>(res) << endl;
    }
    clock_t endTime = clock();
    double elapsedSeconds = double(endTime - startTime) / CLOCKS_PER_SEC;
    cout << "Elapsed time: " << elapsedSeconds << " seconds." << endl;
    return 0;
}
#pragma once

namespace ExitCodes {
    constexpr int OK = 0;
    constexpr int INVALID_ARGUMENT_COUNT = 1;
    constexpr int CONFIG_OPEN_ERROR = 3;
    constexpr int OUTFILE_OPEN_ERROR = 4;
    constexpr int CONFIG_READ_ERROR = 5;
    constexpr int OUTFILE_WRITE_ERROR = 6;
    constexpr int INVALID_DIRECTORY = 26;
}

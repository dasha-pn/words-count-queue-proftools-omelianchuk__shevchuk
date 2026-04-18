#pragma once

#include <boost/program_options.hpp>
#include <vector>

class FileLoadException : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

class ConfigParseException : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Config {
    std::string indir;
    std::string out_by_a;
    std::string out_by_n;
    std::vector<std::string> indexing_extensions;
    std::vector<std::string> archives_extensions;
    size_t max_file_size;
    size_t indexing_threads;
    size_t unarchiving_threads = 0;
    size_t merging_threads;
    size_t filenames_queue_size;
    size_t raw_files_queue_size;
    size_t dictionaries_queue_size;

    //dynamic dispatch config
    size_t dynamic_workers_total = 0;
    size_t dynamic_min_indexing_threads = 1;
    size_t dynamic_max_indexing_threads = 0;
    size_t dynamic_min_merging_threads = 1;
    size_t dynamic_max_merging_threads = 0;
    size_t dynamic_rebalance_threshold = 2;
};

Config read_config(const std::string &filename);

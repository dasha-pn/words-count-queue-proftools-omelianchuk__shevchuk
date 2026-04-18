// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: http://www.viva64.com

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <sstream>

#include <archive.h>
#include <archive_entry.h>
#include <boost/locale.hpp>

#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_hash_map.h>
#include <tbb/task_group.h>
#include <tbb/global_control.h>

#include "exit_codes.hpp"
#include "config_parser.hpp"
#include "time_measurements.hpp"

namespace fs = std::filesystem;
namespace bl = boost::locale;

using ConcurrentMap = tbb::concurrent_hash_map<std::string, size_t>;

enum class EntryType {
    FILE,
    ARCHIVE,
    END_OF_QUEUE
};

using FilenameQueueItem = std::pair<fs::path, EntryType>;
using DataQueueItem = std::pair<std::string, EntryType>;


// #################
// HELPER FUNCTIONS
// #################

std::string read_entry(const fs::path& path) {
    std::ifstream entry(path, std::ios::binary);
    if (!entry) {
        return "";
    }

    std::ostringstream ss;
    ss << entry.rdbuf();
    return ss.str();
}

bool has_extension(const std::vector<std::string>& extensions, const std::string& ext) {
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

void add_word_to_map(const std::string& word, ConcurrentMap& word_counts) {
    ConcurrentMap::accessor accessor;
    word_counts.insert(accessor, word);
    accessor->second++;
}

void process_text(const std::string& text, ConcurrentMap& word_counts) {
    std::string normalized = bl::fold_case(bl::normalize(text));

    bl::boundary::ssegment_index map(
        bl::boundary::word,
        normalized.begin(),
        normalized.end()
    );
    map.rule(bl::boundary::word_letters);

    for (auto it = map.begin(), e = map.end(); it != e; ++it) {
        add_word_to_map(*it, word_counts);
    }
}

void process_archive(
    const std::string& data,
    size_t max_size,
    const std::vector<std::string>& extensions,
    ConcurrentMap& word_counts
) {
    archive* a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    if (archive_read_open_memory(a, data.data(), data.size()) == ARCHIVE_OK) {
        archive_entry* entry = nullptr;

        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            std::string ext = fs::path(archive_entry_pathname(entry)).extension().string();

            if (!has_extension(extensions, ext)) {
                archive_read_data_skip(a);
                continue;
            }

            const la_int64_t entry_size_signed = archive_entry_size(entry);
            if (entry_size_signed <= 0) {
                archive_read_data_skip(a);
                continue;
            }

            const auto entry_size = static_cast<size_t>(entry_size_signed);
            if (entry_size > max_size) {
                archive_read_data_skip(a);
                continue;
            }

            std::string text_data(entry_size, '\0');
            const la_ssize_t bytes_read = archive_read_data(a, text_data.data(), entry_size);

            if (bytes_read > 0) {
                text_data.resize(static_cast<size_t>(bytes_read));
                process_text(text_data, word_counts);
            }
        }
    }

    archive_read_free(a);
}


// ####################
// TASK FUNCTIONS
// ####################

void process_directory(
    const Config& config,
    tbb::concurrent_bounded_queue<FilenameQueueItem>& queue,
    long long& time
) {
    auto start_time = get_current_time_fenced();
    fs::path dir(config.indir);

    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::string ext = entry.path().extension().string();

        if (has_extension(config.archives_extensions, ext)) {
            queue.push({entry.path(), EntryType::ARCHIVE});
        } else if (has_extension(config.indexing_extensions, ext)) {
            if (entry.file_size() <= config.max_file_size) {
                queue.push({entry.path(), EntryType::FILE});
            }
        }
    }

    queue.push({fs::path(), EntryType::END_OF_QUEUE});
    time = to_ms(get_current_time_fenced() - start_time);
}

void extract_data(
    tbb::concurrent_bounded_queue<FilenameQueueItem>& filename_queue,
    tbb::concurrent_bounded_queue<DataQueueItem>& data_queue,
    size_t indexing_threads,
    long long& time
) {
    auto start_time = get_current_time_fenced();

    while (true) {
        FilenameQueueItem item;
        filename_queue.pop(item);

        if (item.second == EntryType::END_OF_QUEUE) {
            for (size_t i = 0; i < indexing_threads; ++i) {
                data_queue.push({"", EntryType::END_OF_QUEUE});
            }
            break;
        }

        std::string data = read_entry(item.first);
        if (!data.empty()) {
            data_queue.push({std::move(data), item.second});
        }
    }

    time = to_ms(get_current_time_fenced() - start_time);
}

void index_data(
    const Config& config,
    tbb::concurrent_bounded_queue<DataQueueItem>& data_queue,
    ConcurrentMap& global_map
) {
    while (true) {
        DataQueueItem item;
        data_queue.pop(item);

        if (item.second == EntryType::END_OF_QUEUE) {
            break;
        }

        if (item.second == EntryType::FILE) {
            process_text(item.first, global_map);
        } else if (item.second == EntryType::ARCHIVE) {
            process_archive(item.first, config.max_file_size, config.indexing_extensions, global_map);
        }
    }
}


// ########
// MAIN
// ########

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config_file>\n";
        return ExitCodes::INVALID_ARGUMENT_COUNT;
    }

    bl::localization_backend_manager lbm = bl::localization_backend_manager::global();
    lbm.select("icu");

    bl::generator gen;
    std::locale::global(gen("en_US.UTF-8"));

    auto start_total = get_current_time_fenced();

    Config config;
    try {
        config = read_config(argv[1]);
    } catch (const FileLoadException& e) {
        std::cerr << e.what() << "\n";
        return ExitCodes::CONFIG_OPEN_ERROR;
    } catch (const ConfigParseException& e) {
        std::cerr << e.what() << "\n";
        return ExitCodes::CONFIG_READ_ERROR;
    }

    fs::path dir(config.indir);
    if (!fs::exists(dir)) {
        std::cerr << "Directory " + config.indir + " does not exist\n";
        return ExitCodes::INVALID_DIRECTORY;
    }
    if (!fs::is_directory(dir)) {
        std::cerr << config.indir + " is not a directory\n";
        return ExitCodes::INVALID_DIRECTORY;
    }

    long long finding_time = 0;
    long long reading_time = 0;

    tbb::concurrent_bounded_queue<FilenameQueueItem> filename_queue;
    filename_queue.set_capacity(config.filenames_queue_size);

    tbb::concurrent_bounded_queue<DataQueueItem> data_queue;
    data_queue.set_capacity(config.raw_files_queue_size);

    ConcurrentMap global_map;

    const size_t scheduler_parallelism = 2 + config.indexing_threads;
    tbb::global_control control(
        tbb::global_control::max_allowed_parallelism,
        scheduler_parallelism
    );

    tbb::task_group tg;

    tg.run([&]() {
        process_directory(config, filename_queue, finding_time);
    });

    tg.run([&]() {
        extract_data(filename_queue, data_queue, config.indexing_threads, reading_time);
    });

    for (size_t i = 0; i < config.indexing_threads; ++i) {
        tg.run([&]() {
            index_data(config, data_queue, global_map);
        });
    }

    tg.wait();

    auto start_write = get_current_time_fenced();

    std::vector<std::pair<std::string, size_t>> sorted_words;
    sorted_words.reserve(global_map.size());

    for (const auto& entry : global_map) {
        sorted_words.emplace_back(entry.first, entry.second);
    }

    std::sort(sorted_words.begin(), sorted_words.end(),
        [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

    std::ofstream out_a(config.out_by_a);
    if (!out_a) {
        std::cerr << "Error opening file for alphabetically sorted results "
                  << config.out_by_a << "\n";
        return ExitCodes::OUTFILE_OPEN_ERROR;
    }

    for (const auto& pair : sorted_words) {
        out_a << std::left << std::setw(25) << pair.first << " : " << pair.second << "\n";
    }

    if (!out_a.good()) {
        std::cerr << "Error writing to file for alphabetically sorted results "
                  << config.out_by_a << "\n";
        return ExitCodes::OUTFILE_WRITE_ERROR;
    }
    out_a.close();

    std::sort(sorted_words.begin(), sorted_words.end(),
        [](const auto& a, const auto& b) {
            if (a.second != b.second) {
                return a.second > b.second;
            }
            return a.first < b.first;
        });

    std::ofstream out_n(config.out_by_n);
    if (!out_n) {
        std::cerr << "Error opening file for n-sorted results "
                  << config.out_by_n << "\n";
        return ExitCodes::OUTFILE_OPEN_ERROR;
    }

    for (const auto& pair : sorted_words) {
        out_n << std::left << std::setw(25) << pair.first << " : " << pair.second << "\n";
    }

    if (!out_n.good()) {
        std::cerr << "Error writing to file for n-sorted results "
                  << config.out_by_n << "\n";
        return ExitCodes::OUTFILE_WRITE_ERROR;
    }
    out_n.close();

    auto end_total = get_current_time_fenced();

    auto total = to_ms(end_total - start_total);
    auto total_write = to_ms(end_total - start_write);

    std::cout << "Total=" << total << "\n";
    std::cout << "Finding=" << finding_time << "\n";
    std::cout << "Reading=" << reading_time << "\n";
    std::cout << "Writing=" << total_write << "\n";

    return ExitCodes::OK;
}

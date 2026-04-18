// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: http://www.viva64.com

#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <iomanip>
#include <archive.h>
#include <archive_entry.h>
#include <boost/locale.hpp>
#include <map>
#include <unordered_map>
#include <thread>

#include "exit_codes.hpp"
#include "config_parser.hpp"
#include "time_measurements.hpp"
#include "thread_safe_queue.hpp"
#include "merge_maps.hpp"

namespace fs = std::filesystem;
namespace bl = boost::locale;

using MapType = std::unordered_map<std::string, size_t>;

enum class EntryType {
    FILE,
    ARCHIVE,
    END_OF_QUEUE
};

// #################
// HELPER FUNCTIONS
// #################

std::string read_entry(const fs::path& path) {
    std::ifstream entry(path, std::ios::binary);
    if (!entry) return "";

    std::ostringstream ss;
    ss << entry.rdbuf();
    return ss.str();
}

void process_text(const std::string& text, MapType& word_counts) {
    std::string normalized = bl::fold_case(bl::normalize(text));

    bl::boundary::ssegment_index map(bl::boundary::word, normalized.begin(), normalized.end());
    map.rule(bl::boundary::word_letters);

    for (bl::boundary::ssegment_index::iterator it = map.begin(), e = map.end(); it != e; ++it) {
        word_counts[*it]++;
    }
}

bool has_extension(const std::vector<std::string>& extensions, const std::string& ext) {
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

void process_archive(const std::string& data, size_t max_size, const std::vector<std::string>& extensions, MapType& word_counts) {
    struct archive *a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    if (archive_read_open_memory(a, data.data(), data.size()) == ARCHIVE_OK) {
        struct archive_entry *entry;
        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            std::string ext = fs::path(archive_entry_pathname(entry)).extension().string();

            if (has_extension(extensions, ext)) {
                const size_t size = archive_entry_size(entry);

                if (size > 0 && size <= max_size) {
                    std::string text_data(size, '\0');
                    archive_read_data(a, text_data.data(), size);
                    
                    process_text(text_data, word_counts);
                }
            }
        }
    }
    archive_read_free(a);
}

// ####################
// THREAD FUNCTIONS
// ####################

void process_directory(const Config& config, ThreadSafeQueue<std::pair<fs::path, EntryType>>& queue, long long& time) {
    auto start_time = get_current_time_fenced();
    fs::path dir(config.indir);

    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();

            // archive
            if (has_extension(config.archives_extensions, ext)) {
                queue.emplace(entry, EntryType::ARCHIVE);
            }
            // regular file
            else if (has_extension(config.indexing_extensions, ext)) {
                if (entry.file_size() <= config.max_file_size) {
                    queue.emplace(entry, EntryType::FILE);
                }
            }
        }
    }

    queue.emplace(fs::path(""), EntryType::END_OF_QUEUE);
    time = to_ms(get_current_time_fenced() - start_time);
}

void extract_data(ThreadSafeQueue<std::pair<fs::path, EntryType>>& filename_queue, ThreadSafeQueue<std::pair<std::string, EntryType>>& data_queue, long long& time) {
    auto start_time = get_current_time_fenced();
    while(auto pair = filename_queue.deque()) {
        EntryType type = (*pair).second;
        if(type == EntryType::END_OF_QUEUE) {
            data_queue.emplace("", EntryType::END_OF_QUEUE);
            break;
        }

        std::string data = read_entry((*pair).first);
        if(!data.empty()) {
            data_queue.emplace(std::move(data), type);
        }
    }
    time = to_ms(get_current_time_fenced() - start_time);
}

void index_data(const Config& config, ThreadSafeQueue<std::pair<std::string, EntryType>>& data_queue, 
    ThreadSafeQueue<MapType>& map_queue, std::atomic<int>& map_count) {
    while(auto pair = data_queue.deque()) {
        EntryType type = (*pair).second;
        if(type == EntryType::END_OF_QUEUE) {
            data_queue.emplace("", EntryType::END_OF_QUEUE);
            break;
        }

        MapType word_counts;
        std::string data = std::move((*pair).first);
        if (type == EntryType::FILE) {
            process_text(data, word_counts);
        }
        else if (type == EntryType::ARCHIVE) {
            process_archive(data, config.max_file_size, config.indexing_extensions, word_counts);
        }

        if (!word_counts.empty()) {
            map_queue.emplace_producer(std::move(word_counts));
            map_count++;
        }
    }
}

// ########
// MAIN
// ########

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <config_file>\n";
        return ExitCodes::INVALID_ARGUMENT_COUNT;
    }

    #ifdef POISON_PILL
    std::cout << "Compiled with the POISON_PILL merge method\n";
    #elif ATOMIC_BOOL
    std::cout << "Compiled with the ATOMIC_BOOL merge method\n";
    #elif DOUBLE_DEQUE
    std::cout << "Compiled with the DOUBLE_DEQUE merge method\n";
    #endif

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

    // ##################
    // THREADS
    // ##################

    std::vector<std::thread> threads;
    threads.reserve(1 + 1 + config.merging_threads);
    ThreadSafeQueue<std::pair<fs::path, EntryType>> filename_queue(config.filenames_queue_size);
    ThreadSafeQueue<std::pair<std::string, EntryType>> data_queue(config.raw_files_queue_size);
    ThreadSafeQueue<MapType> map_queue(config.dictionaries_queue_size, config.dictionaries_queue_size - config.merging_threads);
    long long finding_time = 0;
    long long reading_time = 0;
    
    threads.emplace_back(process_directory, std::ref(config), std::ref(filename_queue), std::ref(finding_time));
    threads.emplace_back(extract_data, std::ref(filename_queue), std::ref(data_queue), std::ref(reading_time));

    std::vector<std::thread> indexing_threads;
    indexing_threads.reserve(config.indexing_threads);
    std::atomic<int> indexed_files = 0;
    for (size_t i = 0; i < config.indexing_threads; i++) {
        indexing_threads.emplace_back(index_data, std::ref(config), std::ref(data_queue), 
        std::ref(map_queue), std::ref(indexed_files));
    }

    #ifndef POISON_PILL
    std::atomic_bool indexing_done = false;
    #endif

    for (size_t i = 0; i < config.merging_threads; i++) {
        #ifdef POISON_PILL
        threads.emplace_back(MergeMaps::poison_pill, std::ref(map_queue), std::ref(indexed_files));
        #elif ATOMIC_BOOL
        threads.emplace_back(MergeMaps::atomic_bool, std::ref(map_queue), std::ref(indexed_files), std::ref(indexing_done));
        #else
        threads.emplace_back(MergeMaps::double_deque, std::ref(map_queue), std::ref(indexed_files), std::ref(indexing_done));
        #endif
    }

    for (auto& thread : indexing_threads) {
        if (thread.joinable()) thread.join();
    }
    #ifdef POISON_PILL
    map_queue.enque(MapType{});
    #else
    indexing_done = true;
    #endif

    for (auto& thread : threads) {
        if (thread.joinable()) thread.join();
    }

    MapType word_counts;
    if (auto opt = map_queue.try_deque()) {
        word_counts = std::move(*opt);
    }
    
    // #####################
    // WRITING
    // #####################

    auto start_write = get_current_time_fenced();
    
    std::vector<std::pair<std::string, size_t>> sorted_words(word_counts.begin(), word_counts.end());

    std::sort(sorted_words.begin(), sorted_words.end(), [](const auto& a, const auto& b) {
        return a.first < b.first; 
    });

    std::ofstream out_a(config.out_by_a);
    if (!out_a) {
        std::cerr << "Error opening file for alphabetically sorted results " << config.out_by_a << "\n";
        return ExitCodes::OUTFILE_OPEN_ERROR;
    }
    for (const auto& pair : sorted_words) {
        out_a << std::left << std::setw(25) << pair.first << " : " << pair.second << "\n";
    }

    if (!out_a.good()) {
        std::cerr << "Error writing to file for alphabetically sorted results " << config.out_by_a << "\n";
        return ExitCodes::OUTFILE_WRITE_ERROR;
    }
    out_a.close();

    std::sort(sorted_words.begin(), sorted_words.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        else return a.first < b.first;
    });

    std::ofstream out_n(config.out_by_n);
    if (!out_n) {
        std::cerr << "Error opening file for n-sorted results " << config.out_by_n << "\n";
        return ExitCodes::OUTFILE_OPEN_ERROR;
    }

    for (const auto& pair : sorted_words) {
        out_n << std::left << std::setw(25) << pair.first << " : " << pair.second << "\n";
    }

    if (!out_n.good()) {
        std::cerr << "Error writing to file for n-sorted results " << config.out_by_n << "\n";
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

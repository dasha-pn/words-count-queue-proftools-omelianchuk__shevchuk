// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: http://www.viva64.com

#include <fstream>
#include <boost/program_options.hpp>
#include "config_parser.hpp"

namespace po = boost::program_options;

static std::string strip_wrapping_quotes(const std::string& value) {
    if (value.size() < 2) {
        return value;
    }

    const char first = value.front();
    const char last = value.back();
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
        return value.substr(1, value.size() - 2);
    }

    return value;
}

Config read_config(const std::string& filename) {
    Config cfg;

    po::options_description desc("Configuration");
    desc.add_options()
        ("indir", po::value<std::string>(&cfg.indir)->required())
        ("out_by_a", po::value<std::string>(&cfg.out_by_a)->required())
        ("out_by_n", po::value<std::string>(&cfg.out_by_n)->required())
        ("indexing_extensions", po::value<std::vector<std::string>>(&cfg.indexing_extensions)->multitoken()->required())
        ("archives_extensions", po::value<std::vector<std::string>>(&cfg.archives_extensions)->multitoken()->required())
        ("max_file_size", po::value<size_t>(&cfg.max_file_size)->required())

        ("indexing_threads", po::value<size_t>(&cfg.indexing_threads)->required())
        ("unarchiving_threads", po::value<size_t>(&cfg.unarchiving_threads)->default_value(0))
        ("merging_threads", po::value<size_t>(&cfg.merging_threads)->required())

        ("filenames_queue_size", po::value<size_t>(&cfg.filenames_queue_size)->required())
        ("raw_files_queue_size", po::value<size_t>(&cfg.raw_files_queue_size)->required())
        ("dictionaries_queue_size", po::value<size_t>(&cfg.dictionaries_queue_size)->required())

        ("dynamic_workers_total", po::value<size_t>(&cfg.dynamic_workers_total)->default_value(0))
        ("dynamic_min_indexing_threads", po::value<size_t>(&cfg.dynamic_min_indexing_threads)->default_value(1))
        ("dynamic_max_indexing_threads", po::value<size_t>(&cfg.dynamic_max_indexing_threads)->default_value(0))
        ("dynamic_min_merging_threads", po::value<size_t>(&cfg.dynamic_min_merging_threads)->default_value(1))
        ("dynamic_max_merging_threads", po::value<size_t>(&cfg.dynamic_max_merging_threads)->default_value(0))
        ("dynamic_rebalance_threshold", po::value<size_t>(&cfg.dynamic_rebalance_threshold)->default_value(2));

    po::variables_map vm;
    std::ifstream file(filename);

    if (!file) {
        throw FileLoadException("Failed to open configuration file " + filename);
    }

    try {
        po::store(po::parse_config_file(file, desc), vm);
        po::notify(vm);
    } catch (const po::error& ex) {
        throw ConfigParseException("Error parsing config: " + std::string(ex.what()));
    }

    cfg.indir = strip_wrapping_quotes(cfg.indir);
    cfg.out_by_a = strip_wrapping_quotes(cfg.out_by_a);
    cfg.out_by_n = strip_wrapping_quotes(cfg.out_by_n);

    for (std::string& ext : cfg.indexing_extensions) {
        ext = strip_wrapping_quotes(ext);
    }
    for (std::string& ext : cfg.archives_extensions) {
        ext = strip_wrapping_quotes(ext);
    }

    if (cfg.dynamic_workers_total == 0) {
        cfg.dynamic_workers_total = cfg.indexing_threads + cfg.merging_threads;
    }

    if (cfg.dynamic_max_indexing_threads == 0) {
        cfg.dynamic_max_indexing_threads = cfg.dynamic_workers_total;
    }

    if (cfg.dynamic_max_merging_threads == 0) {
        cfg.dynamic_max_merging_threads = cfg.dynamic_workers_total;
    }

    if (cfg.dynamic_min_indexing_threads > cfg.dynamic_max_indexing_threads) {
        throw ConfigParseException("dynamic_min_indexing_threads must be <= dynamic_max_indexing_threads");
    }

    if (cfg.dynamic_min_merging_threads > cfg.dynamic_max_merging_threads) {
        throw ConfigParseException("dynamic_min_merging_threads must be <= dynamic_max_merging_threads");
    }

    if (cfg.dynamic_min_indexing_threads + cfg.dynamic_min_merging_threads > cfg.dynamic_workers_total) {
        throw ConfigParseException(
            "dynamic_min_indexing_threads + dynamic_min_merging_threads must be <= dynamic_workers_total"
        );
    }

    return cfg;
}

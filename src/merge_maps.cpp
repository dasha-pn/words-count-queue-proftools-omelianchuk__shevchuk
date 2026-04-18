#include <unordered_map>
#include <atomic>
#include <string>
#include <thread>
#include <chrono>

#include "thread_safe_queue.hpp"
#include "merge_maps.hpp"

namespace MergeMaps {
    void poison_pill(ThreadSafeQueue<MapType>& map_queue, std::atomic<int>& map_count) {
        MapType pending_map{};

        while(auto opt = map_queue.deque()) {
            MapType new_map = std::move(*opt);
            if (new_map.empty()) {
                if (map_count.load() <= 1) {
                    if (!pending_map.empty()) {
                        map_queue.emplace(std::move(pending_map));
                    }
                    map_queue.enque(MapType{});
                    break;
                }
                if (!pending_map.empty()) {
                    map_queue.emplace(std::move(pending_map));
                    pending_map.clear();
                }
                map_queue.enque(MapType{});
                continue;
            }

            if (pending_map.empty()) {
                pending_map = std::move(new_map);
            }
            else {
                if (pending_map.size() < new_map.size()) pending_map.swap(new_map);
                pending_map.reserve(pending_map.size() + new_map.size());

                for (const auto& [word, count] : new_map) {
                    pending_map[word] += count;
                }

                map_queue.emplace(std::move(pending_map));
                pending_map.clear();
                map_count--;
            }
        }
    }

    void atomic_bool(ThreadSafeQueue<MapType>& map_queue, std::atomic<int>& map_count, std::atomic_bool& indexing_done) {
        while(true) {
            int count = map_count.load();
            if (indexing_done && count <= 1) break;

            if (count < 2) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            auto opt1 = map_queue.try_deque();
            if (!opt1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            auto opt2 = map_queue.try_deque();
            if (!opt2) {
                map_queue.emplace(std::move(*opt1));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            auto map1 = *opt1;
            auto map2 = *opt2;

            if (map1.size() < map2.size()) map1.swap(map2);
            map1.reserve(map1.size() + map2.size());

            for (const auto& [word, count] : map2)
                map1[word] += count;

            map_queue.emplace(std::move(map1));
            map_count--;
        }
    }

    void double_deque(ThreadSafeQueue<MapType>& map_queue, std::atomic<int>& map_count, std::atomic_bool& indexing_done) {
        while(auto opt = map_queue.deque_double()) {
            MapType map1 = std::move(opt->first);
            MapType map2 = std::move(opt->second);

            if (map1.size() < map2.size()) map1.swap(map2);
            map1.reserve(map1.size() + map2.size());

            for (const auto& [word, count] : map2) {
                map1[word] += count;
            }

            map_queue.emplace(std::move(map1));
            if (--map_count <= 1 && indexing_done) {
                map_queue.close();
                break;
            }
        }
    }
}

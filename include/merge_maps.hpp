using MapType = std::unordered_map<std::string, size_t>;

namespace MergeMaps {
    void poison_pill(ThreadSafeQueue<MapType>& map_queue, std::atomic<int>& map_count);

    void atomic_bool(ThreadSafeQueue<MapType>& map_queue, std::atomic<int>& map_count, std::atomic_bool& indexing_done);

    void double_deque(ThreadSafeQueue<MapType>& map_queue, std::atomic<int>& map_count, std::atomic_bool& indexing_done);
}

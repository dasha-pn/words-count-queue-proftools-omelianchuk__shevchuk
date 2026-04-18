#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

template <class T>
class ThreadSafeQueue final
{
public:
    explicit ThreadSafeQueue(std::size_t max_capacity)
        : max_capacity_(max_capacity), producer_capacity_limit_(max_capacity) {}

    ThreadSafeQueue(std::size_t max_capacity, std::size_t producer_capacity_limit)
        : max_capacity_(max_capacity),
          producer_capacity_limit_(producer_capacity_limit > max_capacity ? max_capacity : producer_capacity_limit) {}

    ThreadSafeQueue(const ThreadSafeQueue &) = delete;
    ThreadSafeQueue &operator=(const ThreadSafeQueue &) = delete;
    ThreadSafeQueue(ThreadSafeQueue &&) = delete;
    ThreadSafeQueue &operator=(ThreadSafeQueue &&) = delete;

    ~ThreadSafeQueue() noexcept
    {
        close();
    }

    // copy
    bool enque(const T &value)
    {
        std::unique_lock<std::mutex> ul(m_);
        cv_enque.wait(ul, [this]
                      { return closed_ || q_.size() < max_capacity_; });

        if (closed_)
            return false;
        q_.push_back(value);
        ul.unlock();
        cv_.notify_one();
        return true;
    }

    // move
    bool enque(T &&value)
    {
        std::unique_lock<std::mutex> ul(m_);
        cv_enque.wait(ul, [this]
                      { return closed_ || q_.size() < max_capacity_; });

        if (closed_)
            return false;
        q_.push_back(std::move(value));
        ul.unlock();
        cv_.notify_one();
        return true;
    }

    template <class... Args>
    bool emplace(Args &&...args)
    {
        std::unique_lock<std::mutex> ul(m_);
        cv_enque.wait(ul, [this]
                      { return closed_ || q_.size() < max_capacity_; });

        if (closed_)
            return false;
        q_.emplace_back(std::forward<Args>(args)...);
        ul.unlock();
        cv_.notify_one();
        return true;
    }

    template <class... Args>
    bool emplace_producer(Args &&...args)
    {
        std::unique_lock<std::mutex> ul(m_);
        cv_enque.wait(ul, [this]
                      { return closed_ || q_.size() < producer_capacity_limit_; });

        if (closed_)
            return false;
        q_.emplace_back(std::forward<Args>(args)...);
        ul.unlock();
        cv_.notify_one();
        return true;
    }

    // blocking
    std::optional<T> deque()
    {
        std::unique_lock<std::mutex> ul(m_);
        cv_.wait(ul, [this]
                 { return closed_ || !q_.empty(); });

        if (q_.empty())
        {
            return std::nullopt;
        }

        T value = std::move(q_.front());
        q_.pop_front();
        ul.unlock();
        cv_enque.notify_one();

        return value;
    }

    std::optional<std::pair<T, T>> deque_double()
    {
        std::unique_lock<std::mutex> ul(m_);
        cv_.wait(ul, [this]
                 { return closed_ || q_.size() >= 2; });

        if (q_.size() < 2)
        {
            return std::nullopt;
        }

        T value1 = std::move(q_.front());
        q_.pop_front();
        T value2 = std::move(q_.front());
        q_.pop_front();
        ul.unlock();
        cv_enque.notify_one();
        cv_enque.notify_one();

        return std::pair<T, T>(std::move(value1), std::move(value2));
    }

    std::optional<T> try_deque()
    {
        std::lock_guard<std::mutex> lg(m_);
        if (q_.empty())
            return std::nullopt;

        T value = std::move(q_.front());
        q_.pop_front();

        cv_enque.notify_one();
        return value;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lg(m_);
        return q_.empty();
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lg(m_);
        return q_.size();
    }

    void close() noexcept
    {
        bool need_notify = false;
        {
            std::lock_guard<std::mutex> lg(m_);
            if (!closed_)
            {
                closed_ = true;
                need_notify = true;
            }
        }
        if (need_notify)
        {
            cv_.notify_all();
            cv_enque.notify_all();
        }
    }

    bool is_closed() const
    {
        std::lock_guard<std::mutex> lg(m_);
        return closed_;
    }

    size_t approx_size() const
    {
        std::lock_guard<std::mutex> lock(m_);
        return q_.size();
    }

private:
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::condition_variable cv_enque;
    std::deque<T> q_;
    std::size_t max_capacity_;
    std::size_t producer_capacity_limit_;
    bool closed_{false};
};

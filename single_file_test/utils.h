
//
// Logging
//

auto mainThreadString = std::this_thread::get_id();

auto getThreadId() -> std::string
{
    std::stringstream ss;
    ss << std::this_thread::get_id();
    return ss.str();
}

void log(const std::string& message)
{
    std::cout << (std::this_thread::get_id() == mainThreadString) ? "[Main Thread] " : "[Worker Thread] " << message << std::endl;
}

//
// Safe Single-Producer Single-Consumer Queue
// Godbolt replacement for moodycamel::ConcurrentQueue
//

template <typename T>
class SafeSPSCQueue
{
public:
    explicit SafeSPSCQueue(size_t capacity = 1024)
    : capacity_(capacity)
    , buffer_(new T[capacity])
    , readPos_(0)
    , writePos_(0)
    , size_(0)
    {
    }

    ~SafeSPSCQueue()
    {
        // Clean up any remaining items
        while (size_.load(std::memory_order_acquire) > 0)
        {
            buffer_[readPos_].~T();
            readPos_ = (readPos_ + 1) % capacity_;
            size_.fetch_sub(1, std::memory_order_relaxed);
        }
        delete[] buffer_;
    }

    // Copy and move operations
    SafeSPSCQueue(const SafeSPSCQueue&)            = delete;
    SafeSPSCQueue& operator=(const SafeSPSCQueue&) = delete;
    SafeSPSCQueue(SafeSPSCQueue&&)                 = delete;
    SafeSPSCQueue& operator=(SafeSPSCQueue&&)      = delete;

    // Producer methods (single producer)
    template <typename... Args>
    bool try_emplace(Args&&... args) noexcept
    {
        size_t currentSize = size_.load(std::memory_order_acquire);
        if (currentSize >= capacity_)
        {
            return false; // Queue full
        }

        // Construct the item in place
        new (&buffer_[writePos_]) T(std::forward<Args>(args)...);
        writePos_ = (writePos_ + 1) % capacity_;
        size_.fetch_add(1, std::memory_order_release);
        return true;
    }

    // Consumer methods (single consumer)
    bool try_dequeue(T& item) noexcept
    {
        size_t currentSize = size_.load(std::memory_order_acquire);
        if (currentSize == 0)
        {
            return false; // Queue empty
        }

        // Move the item out
        item = std::move(buffer_[readPos_]);
        buffer_[readPos_].~T();
        readPos_ = (readPos_ + 1) % capacity_;
        size_.fetch_sub(1, std::memory_order_release);
        return true;
    }

    // Utility methods
    size_t size() const noexcept
    {
        return size_.load(std::memory_order_acquire);
    }

    bool empty() const noexcept
    {
        return size_.load(std::memory_order_acquire) == 0;
    }

    size_t capacity() const noexcept
    {
        return capacity_;
    }

private:
    const size_t        capacity_;
    T*                  buffer_;
    size_t              readPos_;
    size_t              writePos_;
    std::atomic<size_t> size_;
};

#include "buffer.h"
#include <map>

namespace novemus {

mutable_buffer mutable_buffer::create(size_t size) noexcept(true)
{
    static const time_t cleanup_period = 30;

    static time_t s_clean = 0;
    static std::multimap<size_t, mutable_buffer> s_cache;
    static std::mutex s_mutex;

    std::unique_lock<std::mutex> lock(s_mutex);

    auto compress = [&]()
    {
        time_t now = std::time(0);

        if (s_clean < now)
        {
            auto it = s_cache.begin();
            while (it != s_cache.end())
            {
                if (it->second.unique())
                    it = s_cache.erase(it);
                else
                    ++it;
            }

            s_clean = now + cleanup_period;
        }
    };

    auto iter = s_cache.lower_bound(size);
    while (iter != s_cache.end())
    {
        if (iter->second.unique())
        {
            mutable_buffer buf = iter->second;
            std::memset(buf.data(), 0, buf.size());

            compress();

            return buf;
        }
        ++iter;
    }

    iter = s_cache.emplace(size, mutable_buffer(size));
    return iter->second;
}

}
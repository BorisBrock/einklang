#pragma once

#include <functional>
#include <vector>
#include <cstdint>

// Generic event that any number of handlers can subscribe to
template <typename... Args>
class Event {
public:
    using Handler = std::function<void(Args...)>;
    using HandlerId = uint64_t;

    HandlerId subscribe(Handler handler) {
        auto id = nextId_++;
        handlers_.push_back({id, std::move(handler)});
        return id;
    }

    void unsubscribe(HandlerId id) {
        std::erase_if(handlers_, [id](const auto& entry) {
            return entry.id == id;
        });
    }

    void emit(Args... args) const {
        for (const auto& entry : handlers_) {
            entry.handler(args...);
        }
    }

private:
    struct Entry {
        HandlerId id;
        Handler handler;
    };
    std::vector<Entry> handlers_;
    HandlerId nextId_ = 0;
};
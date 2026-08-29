#pragma once

#include "gain_event_processor.h"

#include <clap/clap.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace webview_gui::examples::gain {

class GainWebviewParameterBridge {
public:
    static constexpr std::size_t kMessageSize = 16;
    static constexpr std::size_t kQueueCapacity = 32;

    void init(const clap_host_t *host) noexcept {
        host_ = host;
        hostParams_ = nullptr;
        writeIndex_.store(0, std::memory_order_relaxed);
        readIndex_.store(0, std::memory_order_relaxed);
        gainGestureOpen_ = false;
        bypassGestureOpen_ = false;
        active_.store(false, std::memory_order_relaxed);
        if (host_ && host_->get_extension) {
            hostParams_ = static_cast<const clap_host_params_t *>(
                host_->get_extension(host_, CLAP_EXT_PARAMS));
        }
    }

    void setActive(bool active) noexcept {
        active_.store(active, std::memory_order_release);
        if (!active && hasPending())
            schedulePending();
    }

    [[nodiscard]] bool receive(bool guiCreated,
                               const void *buffer,
                               uint32_t size) noexcept {
        if (!guiCreated || !buffer || size != kMessageSize)
            return false;

        Command command{};
        if (!decode(static_cast<const uint8_t *>(buffer), command))
            return false;
        if (!canSchedule())
            return false;
        if (!pushPreservingGestureClosure(command))
            return false;

        schedulePending();
        return true;
    }

    void closeOpenGestures() noexcept {
        bool queued = false;
        if (gainGestureOpen_)
            queued = queueGestureEnd(kGainParamId) || queued;
        if (bypassGestureOpen_)
            queued = queueGestureEnd(kBypassParamId) || queued;
        if (queued)
            schedulePending();
    }

    void drain(const clap_output_events_t *out, GainEventProcessor &processor) noexcept {
        if (!out || !out->try_push) {
            reschedulePendingIfInactive();
            return;
        }

        Command command{};
        while (peek(command)) {
            if (!emit(out, command)) {
                reschedulePendingIfInactive();
                return;
            }

            if (command.kind == CommandKind::Value) {
                clap_event_param_value_t valueEvent{};
                initialiseValueEvent(valueEvent, command.paramId, command.value);
                (void)processor.applyParameterValue(valueEvent);
            }
            pop();
        }
    }

private:
    enum class CommandKind : uint8_t {
        GestureBegin = 1,
        Value = 2,
        GestureEnd = 3,
    };

    struct Command {
        CommandKind kind = CommandKind::Value;
        clap_id paramId = CLAP_INVALID_ID;
        double value = 0.0;
    };

    static_assert(kQueueCapacity > 2,
                  "Gain WebView parameter queue needs room for a command and gesture closure");
    static_assert(std::atomic<uint32_t>::is_always_lock_free,
                  "Gain WebView parameter queue indices must be lock-free");

    static uint64_t loadU64Le(const uint8_t *source) noexcept {
        uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i)
            value |= static_cast<uint64_t>(source[i]) << (i * 8u);
        return value;
    }

    static bool decode(const uint8_t *bytes, Command &command) noexcept {
        if (bytes[0] != 'W' || bytes[1] != 'V' || bytes[2] != 'G' || bytes[3] != '1' ||
            bytes[6] != 0 || bytes[7] != 0)
            return false;

        switch (bytes[4]) {
            case 1:
                command.kind = CommandKind::GestureBegin;
                break;
            case 2:
                command.kind = CommandKind::Value;
                break;
            case 3:
                command.kind = CommandKind::GestureEnd;
                break;
            default:
                return false;
        }

        switch (bytes[5]) {
            case 1:
                command.paramId = kGainParamId;
                break;
            case 2:
                command.paramId = kBypassParamId;
                break;
            default:
                return false;
        }

        uint64_t valueBits = loadU64Le(bytes + 8);
        std::memcpy(&command.value, &valueBits, sizeof(command.value));

        if (command.kind != CommandKind::Value)
            return valueBits == 0;

        if (!std::isfinite(command.value))
            return false;
        if (command.paramId == kGainParamId)
            return command.value >= GainProcessor::kMinimumGainDb &&
                   command.value <= GainProcessor::kMaximumGainDb;
        return command.value == 0.0 || command.value == 1.0;
    }

    [[nodiscard]] bool canSchedule() const noexcept {
        if (active_.load(std::memory_order_acquire))
            return host_ && host_->request_process;
        if (hostParams_ && hostParams_->request_flush)
            return true;
        return host_ && host_->request_process;
    }

    void schedulePending() const noexcept {
        if (active_.load(std::memory_order_acquire)) {
            if (host_ && host_->request_process)
                host_->request_process(host_);
            return;
        }

        if (hostParams_ && hostParams_->request_flush) {
            hostParams_->request_flush(host_);
            return;
        }

        if (host_ && host_->request_process)
            host_->request_process(host_);
    }

    void reschedulePendingIfInactive() const noexcept {
        // Active drain() is reached from the audio-thread process path. The
        // command stays in this bounded queue and the Gain plug-in returns
        // CLAP_PROCESS_CONTINUE, so the next process block is the retry point.
        // Only inactive flush handling needs an explicit host wake-up here.
        if (!active_.load(std::memory_order_acquire) && hasPending())
            schedulePending();
    }

    [[nodiscard]] bool hasPending() const noexcept {
        return readIndex_.load(std::memory_order_acquire) !=
               writeIndex_.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint32_t freeSlots() const noexcept {
        const auto write = writeIndex_.load(std::memory_order_relaxed);
        const auto read = readIndex_.load(std::memory_order_acquire);
        const auto capacity = static_cast<uint32_t>(kQueueCapacity);
        const auto used = write >= read ? write - read : capacity - (read - write);
        return (capacity - 1u) - used;
    }

    [[nodiscard]] uint32_t openGestureCount() const noexcept {
        return (gainGestureOpen_ ? 1u : 0u) + (bypassGestureOpen_ ? 1u : 0u);
    }

    bool *gestureState(clap_id paramId) noexcept {
        if (paramId == kGainParamId)
            return &gainGestureOpen_;
        if (paramId == kBypassParamId)
            return &bypassGestureOpen_;
        return nullptr;
    }

    bool queueGestureEnd(clap_id paramId) noexcept {
        Command command{};
        command.kind = CommandKind::GestureEnd;
        command.paramId = paramId;
        return pushPreservingGestureClosure(command);
    }

    bool pushPreservingGestureClosure(const Command &command) noexcept {
        auto *gestureOpen = gestureState(command.paramId);
        if (!gestureOpen)
            return false;

        auto reservedAfter = openGestureCount();
        if (command.kind == CommandKind::GestureBegin) {
            if (*gestureOpen)
                return false;
            ++reservedAfter;
        } else if (command.kind == CommandKind::GestureEnd) {
            if (!*gestureOpen)
                return false;
            --reservedAfter;
        }

        // One slot is needed for this command. Every gesture which remains open
        // afterwards permanently reserves one additional slot for its matching
        // CLAP_EVENT_PARAM_GESTURE_END. The consumer can only free slots, so a
        // conservative read index is safe for this single-producer SPSC queue.
        if (freeSlots() < reservedAfter + 1u)
            return false;
        if (!push(command))
            return false;

        if (command.kind == CommandKind::GestureBegin)
            *gestureOpen = true;
        else if (command.kind == CommandKind::GestureEnd)
            *gestureOpen = false;
        return true;
    }

    bool push(const Command &command) noexcept {
        const auto write = writeIndex_.load(std::memory_order_relaxed);
        const auto next = increment(write);
        if (next == readIndex_.load(std::memory_order_acquire))
            return false;
        commands_[write] = command;
        writeIndex_.store(next, std::memory_order_release);
        return true;
    }

    bool peek(Command &command) const noexcept {
        const auto read = readIndex_.load(std::memory_order_relaxed);
        if (read == writeIndex_.load(std::memory_order_acquire))
            return false;
        command = commands_[read];
        return true;
    }

    void pop() noexcept {
        const auto read = readIndex_.load(std::memory_order_relaxed);
        readIndex_.store(increment(read), std::memory_order_release);
    }

    static constexpr uint32_t increment(uint32_t index) noexcept {
        return (index + 1u) % static_cast<uint32_t>(kQueueCapacity);
    }

    static void initialiseHeader(clap_event_header_t &header,
                                 uint16_t type,
                                 uint32_t size) noexcept {
        header.size = size;
        header.time = 0;
        header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        header.type = type;
        header.flags = CLAP_EVENT_IS_LIVE;
    }

    static void initialiseValueEvent(clap_event_param_value_t &event,
                                     clap_id paramId,
                                     double value) noexcept {
        initialiseHeader(event.header, CLAP_EVENT_PARAM_VALUE, sizeof(event));
        event.param_id = paramId;
        event.cookie = nullptr;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
    }

    static bool emit(const clap_output_events_t *out, const Command &command) noexcept {
        if (command.kind == CommandKind::Value) {
            clap_event_param_value_t event{};
            initialiseValueEvent(event, command.paramId, command.value);
            return out->try_push(out, &event.header);
        }

        clap_event_param_gesture_t event{};
        initialiseHeader(event.header,
                         command.kind == CommandKind::GestureBegin
                             ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                             : CLAP_EVENT_PARAM_GESTURE_END,
                         sizeof(event));
        event.param_id = command.paramId;
        return out->try_push(out, &event.header);
    }

    const clap_host_t *host_ = nullptr;
    const clap_host_params_t *hostParams_ = nullptr;
    std::array<Command, kQueueCapacity> commands_{};
    std::atomic<uint32_t> writeIndex_{0};
    std::atomic<uint32_t> readIndex_{0};
    std::atomic<bool> active_{false};
    bool gainGestureOpen_ = false;
    bool bypassGestureOpen_ = false;
};

} // namespace webview_gui::examples::gain

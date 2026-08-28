#pragma once

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <variant>
#include <vector>

namespace webview_gui::examples::test_support {

class StereoFloatBlock {
public:
    explicit StereoFloatBlock(uint32_t frameCount)
        : frameCount_(frameCount),
          inputLeft_(frameCount),
          inputRight_(frameCount),
          outputLeft_(frameCount),
          outputRight_(frameCount) {
        inputChannels_ = {inputLeft_.data(), inputRight_.data()};
        outputChannels_ = {outputLeft_.data(), outputRight_.data()};

        inputBuffer_.data32 = inputChannels_.data();
        inputBuffer_.data64 = nullptr;
        inputBuffer_.channel_count = 2;
        inputBuffer_.latency = 0;
        inputBuffer_.constant_mask = 0;

        outputBuffer_.data32 = outputChannels_.data();
        outputBuffer_.data64 = nullptr;
        outputBuffer_.channel_count = 2;
        outputBuffer_.latency = 0;
        outputBuffer_.constant_mask = 0;
    }

    [[nodiscard]] uint32_t frames() const noexcept { return frameCount_; }

    void fillInput(float left, float right) noexcept {
        std::fill(inputLeft_.begin(), inputLeft_.end(), left);
        std::fill(inputRight_.begin(), inputRight_.end(), right);
    }

    void clearOutput() noexcept {
        std::fill(outputLeft_.begin(), outputLeft_.end(), 0.0f);
        std::fill(outputRight_.begin(), outputRight_.end(), 0.0f);
    }

    [[nodiscard]] float *inputChannel(std::size_t channel) noexcept {
        return inputChannels_.at(channel);
    }

    [[nodiscard]] float *outputChannel(std::size_t channel) noexcept {
        return outputChannels_.at(channel);
    }

    [[nodiscard]] clap_audio_buffer_t *input() noexcept { return &inputBuffer_; }
    [[nodiscard]] clap_audio_buffer_t *output() noexcept { return &outputBuffer_; }

private:
    uint32_t frameCount_ = 0;
    std::vector<float> inputLeft_;
    std::vector<float> inputRight_;
    std::vector<float> outputLeft_;
    std::vector<float> outputRight_;
    std::array<float *, 2> inputChannels_{};
    std::array<float *, 2> outputChannels_{};
    clap_audio_buffer_t inputBuffer_{};
    clap_audio_buffer_t outputBuffer_{};
};

class InputEvents {
public:
    InputEvents() noexcept {
        interface_.ctx = this;
        interface_.size = sizeCallback;
        interface_.get = getCallback;
    }

    bool pushParamValue(uint32_t time,
                        clap_id paramId,
                        double value,
                        int32_t noteId = -1,
                        int16_t portIndex = -1,
                        int16_t channel = -1,
                        int16_t key = -1) {
        clap_event_param_value_t event{};
        initialiseHeader(event.header, time, CLAP_EVENT_PARAM_VALUE, sizeof(event));
        event.param_id = paramId;
        event.cookie = nullptr;
        event.note_id = noteId;
        event.port_index = portIndex;
        event.channel = channel;
        event.key = key;
        event.value = value;
        return push(event);
    }

    bool pushParamMod(uint32_t time,
                      clap_id paramId,
                      double amount,
                      int32_t noteId = -1,
                      int16_t portIndex = -1,
                      int16_t channel = -1,
                      int16_t key = -1) {
        clap_event_param_mod_t event{};
        initialiseHeader(event.header, time, CLAP_EVENT_PARAM_MOD, sizeof(event));
        event.param_id = paramId;
        event.cookie = nullptr;
        event.note_id = noteId;
        event.port_index = portIndex;
        event.channel = channel;
        event.key = key;
        event.amount = amount;
        return push(event);
    }

    bool pushNote(uint16_t type,
                  uint32_t time,
                  int32_t noteId,
                  int16_t portIndex,
                  int16_t channel,
                  int16_t key,
                  double velocity) {
        if (type != CLAP_EVENT_NOTE_ON && type != CLAP_EVENT_NOTE_OFF &&
            type != CLAP_EVENT_NOTE_CHOKE && type != CLAP_EVENT_NOTE_END)
            return false;

        clap_event_note_t event{};
        initialiseHeader(event.header, time, type, sizeof(event));
        event.note_id = noteId;
        event.port_index = portIndex;
        event.channel = channel;
        event.key = key;
        event.velocity = velocity;
        return push(event);
    }

    bool pushNoteExpression(uint32_t time,
                            int32_t expressionId,
                            int32_t noteId,
                            int16_t portIndex,
                            int16_t channel,
                            int16_t key,
                            double value) {
        clap_event_note_expression_t event{};
        initialiseHeader(event.header, time, CLAP_EVENT_NOTE_EXPRESSION, sizeof(event));
        event.expression_id = expressionId;
        event.note_id = noteId;
        event.port_index = portIndex;
        event.channel = channel;
        event.key = key;
        event.value = value;
        return push(event);
    }

    void clear() noexcept { events_.clear(); }

    [[nodiscard]] const clap_input_events_t *clapInputEvents() const noexcept {
        return &interface_;
    }

private:
    using Event = std::variant<clap_event_param_value_t,
                               clap_event_param_mod_t,
                               clap_event_note_t,
                               clap_event_note_expression_t>;

    static void initialiseHeader(clap_event_header_t &header,
                                 uint32_t time,
                                 uint16_t type,
                                 uint32_t size) noexcept {
        header.size = size;
        header.time = time;
        header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        header.type = type;
        header.flags = 0;
    }

    template <typename EventType>
    bool push(const EventType &event) {
        if (!events_.empty() && event.header.time < headerOf(events_.back())->time)
            return false;
        events_.emplace_back(event);
        return true;
    }

    [[nodiscard]] static const clap_event_header_t *headerOf(const Event &event) noexcept {
        return std::visit(
            [](const auto &value) -> const clap_event_header_t * { return &value.header; },
            event);
    }

    static uint32_t CLAP_ABI sizeCallback(const clap_input_events_t *list) noexcept {
        const auto *self = static_cast<const InputEvents *>(list->ctx);
        return static_cast<uint32_t>(self->events_.size());
    }

    static const clap_event_header_t *CLAP_ABI getCallback(
        const clap_input_events_t *list, uint32_t index) noexcept {
        const auto *self = static_cast<const InputEvents *>(list->ctx);
        if (index >= self->events_.size())
            return nullptr;
        return headerOf(self->events_[index]);
    }

    std::vector<Event> events_;
    clap_input_events_t interface_{};
};

class CapturedOutputEvents {
public:
    CapturedOutputEvents() noexcept {
        interface_.ctx = this;
        interface_.try_push = tryPushCallback;
    }

    [[nodiscard]] clap_output_events_t *clapOutputEvents() noexcept {
        return &interface_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return events_.size(); }

    [[nodiscard]] const clap_event_header_t *header(std::size_t index) const noexcept {
        return index < events_.size() ? &events_[index].header : nullptr;
    }

    [[nodiscard]] const std::vector<std::byte> *bytes(std::size_t index) const noexcept {
        return index < events_.size() ? &events_[index].bytes : nullptr;
    }

    void clear() noexcept { events_.clear(); }

private:
    struct CapturedEvent {
        clap_event_header_t header{};
        std::vector<std::byte> bytes;
    };

    static constexpr uint32_t kMaxCapturedEventBytes = 4096;

    static bool CLAP_ABI tryPushCallback(const clap_output_events_t *list,
                                         const clap_event_header_t *event) noexcept {
        if (!event || event->size < sizeof(clap_event_header_t) ||
            event->size > kMaxCapturedEventBytes)
            return false;

        auto *self = static_cast<CapturedOutputEvents *>(list->ctx);
        try {
            CapturedEvent captured;
            captured.header = *event;
            captured.bytes.resize(event->size);
            std::memcpy(captured.bytes.data(), event, event->size);
            self->events_.push_back(std::move(captured));
            return true;
        } catch (...) {
            return false;
        }
    }

    std::vector<CapturedEvent> events_;
    clap_output_events_t interface_{};
};

} // namespace webview_gui::examples::test_support

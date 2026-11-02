// N5 (D2): saida nula, atras de IAudioOutput (headless e dispositivo ausente). A saida de cada sistema: audio_output_*.cpp.
#include "audio/audio.h"

namespace bs::native::audio {

namespace {

class NullOutput final : public IAudioOutput {
public:
    Result<void> open(uint32_t, uint8_t ch) override {
        channels_ = ch ? ch : 1;
        return {};
    }
    size_t       writable_frames() const override { return paused_ ? 0 : size_t(1) << 20; }
    Result<void> write(std::span<const int16_t> pcm) override {
        if (!paused_) played_ += pcm.size() / channels_;
        return {};
    }
    void     close() override {}
    void     pause(bool p) override { paused_ = p; }
    void     flush() override {}
    uint64_t played_frames() const override { return played_; }
    bool     healthy() const override { return true; }

private:
    uint32_t channels_ = 1;
    uint64_t played_   = 0;
    bool     paused_   = false;
};

} // namespace

std::unique_ptr<IAudioOutput> make_null_output() { return std::make_unique<NullOutput>(); }

} // namespace bs::native::audio

#include "sound.h"

namespace snd {

std::function<void(const Command&)> sink;

static void emit(Command::Kind k, u32 a = 0, u32 b = 0, u32 c = 0, u32 d = 0) {
    if (sink) sink(Command{k, {a, b, c, d}});
}

void init(u32 patches, u32 envelopes, u32 sequences, u32 samples) { emit(Command::Init, patches, envelopes, sequences, samples); }
void startSequence(u32 id) { emit(Command::StartSequence, id); }
void stopSequence(u32 id) { emit(Command::StopSequence, id); }
void pauseAll() { emit(Command::PauseAll); }
void resumeAll() { emit(Command::ResumeAll); }
void stopAll() { emit(Command::StopAll); }

}  // namespace snd

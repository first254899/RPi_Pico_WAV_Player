// Host stub: only the constants ReadBuffer needs
#pragma once
class PlayAudio
{
public:
    static constexpr int RDBUF_SIZE = 576 * 8;  // SAMPLES_PER_BUFFER * 8
    static constexpr int RDBUF_THRESHOLD = RDBUF_SIZE / 4;
};

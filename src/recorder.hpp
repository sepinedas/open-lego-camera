#pragma once

#include <string>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

namespace olc {

// Records a video file from frames pushed in the capture loop, plus audio from
// the default ALSA input (when present) which is muxed in on stop.
//
// Design: video frames are written with OpenCV's VideoWriter to a temp file
// while `arecord` captures a WAV alongside. On stop the two are combined with
// ffmpeg (`-c:v copy -c:a aac`) into the final .mp4. If a mic, arecord or
// ffmpeg is missing, it degrades gracefully to a video-only file.
class Recorder {
public:
    ~Recorder();

    bool recording() const { return recording_; }

    // Begin recording. `finalPath` is the .mp4 the user will end up with.
    // Returns false if the video writer could not be opened.
    bool start(const std::string& finalPath, cv::Size frameSize, double fps,
               bool wantAudio);

    // Push one BGR frame (no-op when not recording).
    void writeFrame(const cv::Mat& frame);

    // Finish: stop audio, release the writer and mux in the background.
    void stop();

private:
    void startAudio();
    void stopAudio();

    bool recording_ = false;
    bool audioActive_ = false;
    cv::VideoWriter writer_;
    std::string finalPath_;
    std::string tmpVideo_;
    std::string tmpAudio_;
    pid_t arecordPid_ = -1;
};

// True if `name` is an executable found on $PATH.
bool commandExists(const std::string& name);

} // namespace olc

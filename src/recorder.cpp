#include "recorder.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <thread>

#include <sys/wait.h>
#include <unistd.h>

#include <opencv2/imgproc.hpp>

namespace olc {

bool commandExists(const std::string& name) {
    // `command -v` is quiet and returns non-zero when not found.
    std::string cmd = "command -v " + name + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

Recorder::~Recorder() {
    if (recording_) stop();
}

bool Recorder::start(const std::string& finalPath, cv::Size frameSize, double fps,
                     bool wantAudio) {
    if (recording_) return false;

    finalPath_ = finalPath;
    // Even when we intend to mux audio we first write video to a temp file.
    const bool audioPossible = wantAudio && commandExists("arecord") &&
                               commandExists("ffmpeg");
    tmpVideo_ = audioPossible ? (finalPath + ".video.mp4") : finalPath;
    tmpAudio_ = finalPath + ".audio.wav";

    // mp4v keeps CPU within reach of the Zero 2 W at preview resolutions.
    int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    double writeFps = (fps > 1.0 && fps < 121.0) ? fps : 30.0;
    if (!writer_.open(tmpVideo_, fourcc, writeFps, frameSize, /*isColor=*/true)) {
        std::cerr << "could not open video writer for " << tmpVideo_ << "\n";
        return false;
    }

    recording_ = true;
    if (audioPossible) startAudio();
    return true;
}

void Recorder::startAudio() {
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "fork for audio failed; recording video only\n";
        return;
    }
    if (pid == 0) {
        // Child: capture CD-quality WAV from the default input until killed.
        execlp("arecord", "arecord", "-q", "-f", "cd", "-t", "wav",
               tmpAudio_.c_str(), (char*)nullptr);
        _exit(127); // exec failed
    }
    arecordPid_ = pid;
    audioActive_ = true;
}

void Recorder::stopAudio() {
    if (!audioActive_ || arecordPid_ <= 0) return;
    ::kill(arecordPid_, SIGINT); // let arecord flush the WAV header cleanly
    int status = 0;
    ::waitpid(arecordPid_, &status, 0);
    arecordPid_ = -1;
    audioActive_ = false;
}

void Recorder::writeFrame(const cv::Mat& frame) {
    if (!recording_ || frame.empty()) return;
    writer_.write(frame);
}

void Recorder::stop() {
    if (!recording_) return;
    recording_ = false;

    const bool hadAudio = audioActive_;
    stopAudio();
    writer_.release();

    if (!hadAudio) return; // tmpVideo_ == finalPath_, nothing to mux

    // Mux video+audio in the background so the UI doesn't stall on stop.
    std::string video = tmpVideo_;
    std::string audio = tmpAudio_;
    std::string out = finalPath_;
    std::thread([video, audio, out]() {
        std::string cmd = "ffmpeg -y -loglevel error -i '" + video +
                          "' -i '" + audio +
                          "' -c:v copy -c:a aac -shortest '" + out + "' 2>/dev/null";
        int rc = std::system(cmd.c_str());
        if (rc == 0) {
            ::unlink(video.c_str());
        } else {
            // Muxing failed: keep the video-only file as the final output.
            ::rename(video.c_str(), out.c_str());
        }
        ::unlink(audio.c_str());
    }).detach();
}

} // namespace olc

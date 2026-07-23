// Exercises the OpenCV tracker (YuNet detect + LBF landmarks) and the dog
// renderer on a real face image. Run from the repo root:
//   ./build/tracker_test build/portrait.jpg
#include <cstdio>

#include <opencv2/imgcodecs.hpp>

#include "../src/dogfilter.hpp"
#include "../src/facetracker.hpp"

using namespace olc;

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "build/portrait.jpg";
    cv::Mat img = cv::imread(path);
    if (img.empty()) { fprintf(stderr, "cannot read %s\n", path); return 1; }

    FaceTracker ft;
    if (!ft.init("", "models/lbfmodel.yaml",
                 "models/face_detection_yunet_2023mar.onnx")) {
        fprintf(stderr, "tracker init failed\n");
        return 2;
    }
    std::vector<cv::Point2f> lm;
    if (!ft.track(img, lm)) { fprintf(stderr, "no face\n"); return 3; }
    printf("landmarks: %zu\n", lm.size());
    printf("  nose(30) %.1f,%.1f  chin(8) %.1f,%.1f\n", lm[30].x, lm[30].y, lm[8].x, lm[8].y);
    printf("  eyeL(36) %.1f,%.1f  eyeR(45) %.1f,%.1f\n", lm[36].x, lm[36].y, lm[45].x, lm[45].y);

    DogFilter dog;
    bool ok = dog.apply(img, lm);
    printf("dog.apply -> %s\n", ok ? "rendered" : "pose failed");
    cv::imwrite("build/opencv-realface.png", img);
    printf("wrote build/opencv-realface.png\n");
    return ok ? 0 : 4;
}

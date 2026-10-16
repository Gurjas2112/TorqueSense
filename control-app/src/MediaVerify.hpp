#pragma once
// Sample media through perception (spec Section 12, the third use of a trace's expect: block):
//
//   torquesense --sample samples\wheel-hub-5lug --verify-media all [--frames-csv out.csv]
//   torquesense --sample samples\wheel-hub-5lug --verify-media adjacent_order
//   torquesense --sample samples\wheel-hub-5lug --classify-image samples\wheel-hub-5lug\media\correct\frame_0090.png
//
// Every frame of media/<trace>.mp4 is read, its points classified from the pixels, committed through
// the 2-frame debounce and fed to the interpreter, with the trace's plant signals and release request
// (a camera cannot see those). The recording must raise exactly the violations its trace expects.
// --verify-media also checks every keyframe image in media/<trace>/ against frames.json.
#include <filesystem>
#include <string>

namespace sunverify::app {

int run_verify_media(const std::filesystem::path& sample_dir, const std::string& which, const std::filesystem::path& frames_csv);
int run_classify_image(const std::filesystem::path& sample_dir, const std::filesystem::path& image);

}  // namespace sunverify::app

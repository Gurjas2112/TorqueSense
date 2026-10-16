// torquesense — one station's verification engine (spec Section 14).
// Phase 1: --version, which reports the build and which execution providers actually load.
#include <cstdio>
#include <cstring>
#include <string>

#ifdef SUNVERIFY_WITH_CUDA
#include <onnxruntime_cxx_api.h>
#endif

namespace {

// "Loads" means appending the provider succeeds — the check Section 30 trusts. GetAvailableProviders()
// only lists what was compiled in, so it passes on exactly the misconfiguration this must catch.
void print_providers() {
#ifdef SUNVERIFY_WITH_CUDA
  std::printf("onnxruntime %s\n", Ort::GetVersionString().c_str());
  const OrtApi& api = Ort::GetApi();
  {
    Ort::SessionOptions so;
    OrtTensorRTProviderOptionsV2* trt = nullptr;
    try {
      Ort::ThrowOnError(api.CreateTensorRTProviderOptions(&trt));
      so.AppendExecutionProvider_TensorRT_V2(*trt);
      std::printf("provider tensorrt: loads\n");
    } catch (const Ort::Exception& e) {
      std::printf("provider tensorrt: unavailable — %s\n", e.what());
    }
    if (trt) api.ReleaseTensorRTProviderOptions(trt);
  }
  {
    Ort::SessionOptions so;
    OrtCUDAProviderOptionsV2* cu = nullptr;
    try {
      Ort::ThrowOnError(api.CreateCUDAProviderOptions(&cu));
      so.AppendExecutionProvider_CUDA_V2(*cu);
      std::printf("provider cuda: loads\n");
    } catch (const Ort::Exception& e) {
      std::printf("provider cuda: unavailable — %s\n", e.what());
    }
    if (cu) api.ReleaseCUDAProviderOptions(cu);
  }
#else
  std::printf("onnxruntime: not built in (SUNVERIFY_WITH_CUDA=OFF) — perception runs as ReplayDetector\n");
  std::printf("provider replay: loads\n");
#endif
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--version") == 0) {
      std::printf("torquesense %s (GCC %d.%d.%d, __cplusplus %ld)\n", TORQUESENSE_VERSION, __GNUC__,
                  __GNUC_MINOR__, __GNUC_PATCHLEVEL__, static_cast<long>(__cplusplus));
      print_providers();
      return 0;
    }
  }
  std::fprintf(stderr, "usage: torquesense --version\n");
  return 2;
}

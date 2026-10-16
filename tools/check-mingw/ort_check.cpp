// Gate G2, C++ half: the engine's ONNX Runtime calls, built with your MSYS2 GCC against the
// official MSVC-built gpu_cuda13 package. Tested with MinGW-w64 GCC and ORT 1.28.0.
#include <onnxruntime_cxx_api.h>
#include <cstdio>
#include <string>
#include <vector>
int main(int argc, char** argv) {
  Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "torquesense");
  Ort::SessionOptions so;
  so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
  bool cuda = true;
  try {
    OrtCUDAProviderOptionsV2* cu = nullptr;
    Ort::ThrowOnError(Ort::GetApi().CreateCUDAProviderOptions(&cu));
    const char* k[] = {"device_id", "arena_extend_strategy"};
    const char* v[] = {"0", "kSameAsRequested"};
    Ort::ThrowOnError(Ort::GetApi().UpdateCUDAProviderOptions(cu, k, v, 2));
    so.AppendExecutionProvider_CUDA_V2(*cu);
    Ort::GetApi().ReleaseCUDAProviderOptions(cu);
  } catch (const Ort::Exception& e) {
    cuda = false;
    std::fprintf(stderr, "CUDA EP unavailable: %s\n", e.what());
  }
  std::printf("ORT %s, CUDA EP %s\n", Ort::GetVersionString().c_str(), cuda ? "appended" : "missing");
  if (argc < 2) return cuda ? 0 : 2;
  std::wstring path(argv[1], argv[1] + std::char_traits<char>::length(argv[1]));
  Ort::Session session(env, path.c_str(), so);
  Ort::AllocatorWithDefaultOptions alloc;
  auto in = session.GetInputNameAllocated(0, alloc);
  auto out = session.GetOutputNameAllocated(0, alloc);
  std::vector<float> x(4, 1.0f); std::vector<int64_t> shape{1, 4};
  auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  Ort::Value t = Ort::Value::CreateTensor<float>(mem, x.data(), x.size(), shape.data(), shape.size());
  const char* ins[] = {in.get()}; const char* outs[] = {out.get()};
  auto r = session.Run(Ort::RunOptions{nullptr}, ins, &t, 1, outs, 1);
  std::printf("out[0]=%f\n", r[0].GetTensorData<float>()[0]);
}

// Gate G2, TensorRT half: the engine's TensorRT-provider calls, built with your MSYS2 GCC against the
// official gpu_cuda13 package. The TensorRT provider first, the CUDA provider behind it — the order
// the engine uses. With a model argument it builds a TensorRT engine into .\trt-cache and runs it.
#include <onnxruntime_cxx_api.h>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
int main(int argc, char** argv) {
  Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "torquesense");
  Ort::SessionOptions so;
  so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
  const OrtApi& api = Ort::GetApi();
  try {
    OrtTensorRTProviderOptionsV2* raw = nullptr;
    Ort::ThrowOnError(api.CreateTensorRTProviderOptions(&raw));
    std::unique_ptr<OrtTensorRTProviderOptionsV2, decltype(api.ReleaseTensorRTProviderOptions)>
        trt{raw, api.ReleaseTensorRTProviderOptions};
    const char* k[] = {"device_id", "trt_fp16_enable", "trt_max_workspace_size",
                       "trt_engine_cache_enable", "trt_engine_cache_path",
                       "trt_timing_cache_enable", "trt_timing_cache_path"};
    const char* v[] = {"0", "1", "268435456", "1", "trt-cache", "1", "trt-cache"};
    Ort::ThrowOnError(api.UpdateTensorRTProviderOptions(trt.get(), k, v, 7));
    so.AppendExecutionProvider_TensorRT_V2(*trt);
    std::printf("TensorRT EP appended\n");
  } catch (const Ort::Exception& e) {
    std::fprintf(stderr, "TensorRT EP unavailable: %s\n", e.what());
    return 2;                                  // the engine refuses to start here, too
  }
  try {
    OrtCUDAProviderOptionsV2* cu = nullptr;
    Ort::ThrowOnError(api.CreateCUDAProviderOptions(&cu));
    so.AppendExecutionProvider_CUDA_V2(*cu);
    api.ReleaseCUDAProviderOptions(cu);
    std::printf("CUDA EP appended behind it\n");
  } catch (const Ort::Exception& e) {
    std::fprintf(stderr, "CUDA EP unavailable: %s\n", e.what());
    return 2;
  }
  if (argc < 2) return 0;
  std::wstring path(argv[1], argv[1] + std::char_traits<char>::length(argv[1]));
  Ort::Session session(env, path.c_str(), so);      // first run: TensorRT builds and caches an engine
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

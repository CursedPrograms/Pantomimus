// Pantomimus - one ONNX model through ONNX Runtime's C API (the library loaded at run time, so a missing DLL is a
// message, not a crash). Float inputs and outputs.
#pragma once
#include <string>
#include <vector>

namespace Net {

// ONNX Runtime itself: loaded from beside the program (onnxruntime.dll / libonnxruntime.so)
bool runtime(std::string& err);
std::string runtimeVersion();

class Model {
public:
    Model() = default;
    ~Model();
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    bool load(const std::string& path, int threads, std::string& err);
    bool loaded() const { return session_ != nullptr; }
    void unload();
    // one input tensor (NHWC or whatever the model takes); every output, flattened
    bool run(const float* input, const std::vector<long long>& shape, std::vector<std::vector<float>>& outputs, std::string& err);
    const std::vector<std::string>& outputNames() const { return outNames_; }
    const std::string& path() const { return path_; }

private:
    void* session_ = nullptr;
    std::string path_, inName_;
    std::vector<std::string> outNames_;
};

}  // namespace Net

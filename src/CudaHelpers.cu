#include "panim/CudaHelpers.hpp"
#include "panim/Log.hpp"

#ifdef PANIM_ENABLE_CUDA

#include <cuda_runtime.h>

namespace panim {

    namespace {

        __global__ void invert_kernel(uint8_t *data, int size) {
            int idx = blockIdx.x * blockDim.x + threadIdx.x;
            if (idx < size) {
                data[idx] = 255 - data[idx];
            }
        }

    } // namespace

    bool gpu_invert(Frame &frame) {
        const int size = frame.width * frame.height * 4;
        uint8_t *dev = nullptr;
        static bool logged = false;
        if (!logged) {
            int dev_id = 0;
            cudaGetDevice(&dev_id);
            cudaDeviceProp prop{};
            cudaGetDeviceProperties(&prop, dev_id);
            panim ::log_fallback(
                "INFO", panim::panim_format("CUDA invert using device {}: {}",
                                            dev_id, prop.name));
            logged = true;
        }
        if (cudaMalloc(&dev, size) != cudaSuccess)
            return false;
        if (cudaMemcpy(dev, frame.pixels.data(), size, cudaMemcpyHostToDevice) != cudaSuccess) {
            cudaFree(dev);
            return false;
        }

        int threads = 256;
        int blocks = (size + threads - 1) / threads;
        invert_kernel<<<blocks, threads>>>(dev, size);
        if (cudaDeviceSynchronize() != cudaSuccess) {
            cudaFree(dev);
            return false;
        }

        if (cudaMemcpy(frame.pixels.data(), dev, size, cudaMemcpyDeviceToHost) != cudaSuccess) {
            cudaFree(dev);
            return false;
        }
        cudaFree(dev);
        return true;
    }

} // namespace panim

#endif // PANIM_ENABLE_CUDA

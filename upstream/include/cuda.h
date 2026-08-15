#ifndef GSR_CUDA_H
#define GSR_CUDA_H

#include <stddef.h>
#include <stdbool.h>

// To prevent hwcontext_cuda.h from including cuda.h
#define CUDA_VERSION 11070

#define CU_CTX_SCHED_AUTO 0

#if defined(_WIN64) || defined(__LP64__)
typedef unsigned long long CUdeviceptr_v2;
#else
typedef unsigned int CUdeviceptr_v2;
#endif
typedef CUdeviceptr_v2 CUdeviceptr;

typedef int CUresult;
typedef int CUdevice_v1;
typedef CUdevice_v1 CUdevice;
typedef struct CUctx_st *CUcontext;
typedef struct CUstream_st *CUstream;
typedef struct CUarray_st *CUarray;

#define CUDA_SUCCESS 0

typedef enum CUgraphicsMapResourceFlags_enum {
    CU_GRAPHICS_MAP_RESOURCE_FLAGS_NONE          = 0x00,
    CU_GRAPHICS_MAP_RESOURCE_FLAGS_READ_ONLY     = 0x01,
    CU_GRAPHICS_MAP_RESOURCE_FLAGS_WRITE_DISCARD = 0x02
} CUgraphicsMapResourceFlags;

typedef enum CUgraphicsRegisterFlags_enum {
    CU_GRAPHICS_REGISTER_FLAGS_NONE           = 0x00,
    CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY      = 0x01,
    CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD  = 0x02,
    CU_GRAPHICS_REGISTER_FLAGS_SURFACE_LDST   = 0x04,
    CU_GRAPHICS_REGISTER_FLAGS_TEXTURE_GATHER = 0x08
} CUgraphicsRegisterFlags;

typedef enum CUmemorytype_enum {
    CU_MEMORYTYPE_HOST    = 0x01,    /**< Host memory */
    CU_MEMORYTYPE_DEVICE  = 0x02,    /**< Device memory */
    CU_MEMORYTYPE_ARRAY   = 0x03,    /**< Array memory */
    CU_MEMORYTYPE_UNIFIED = 0x04     /**< Unified device or host memory */
} CUmemorytype;

typedef struct CUDA_MEMCPY2D_st {
    size_t srcXInBytes;         /**< Source X in bytes */
    size_t srcY;                /**< Source Y */

    CUmemorytype srcMemoryType; /**< Source memory type (host, device, array) */
    const void *srcHost;        /**< Source host pointer */
    CUdeviceptr srcDevice;      /**< Source device pointer */
    CUarray srcArray;           /**< Source array reference */
    size_t srcPitch;            /**< Source pitch (ignored when src is array) */

    size_t dstXInBytes;         /**< Destination X in bytes */
    size_t dstY;                /**< Destination Y */

    CUmemorytype dstMemoryType; /**< Destination memory type (host, device, array) */
    void *dstHost;              /**< Destination host pointer */
    CUdeviceptr dstDevice;      /**< Destination device pointer */
    CUarray dstArray;           /**< Destination array reference */
    size_t dstPitch;            /**< Destination pitch (ignored when dst is array) */

    size_t WidthInBytes;        /**< Width of 2D memory copy in bytes */
    size_t Height;              /**< Height of 2D memory copy */
} CUDA_MEMCPY2D_v2;
typedef CUDA_MEMCPY2D_v2 CUDA_MEMCPY2D;

typedef struct CUgraphicsResource_st *CUgraphicsResource;

typedef struct gsr_cuda gsr_cuda;
struct gsr_cuda {
    void *library;
    CUcontext cu_ctx;

    CUresult (*cuInit)(unsigned int Flags);
    CUresult (*cuDeviceGetCount)(int *count);
    CUresult (*cuDeviceGet)(CUdevice *device, int ordinal);
    CUresult (*cuCtxCreate_v2)(CUcontext *pctx, unsigned int flags, CUdevice dev);
    CUresult (*cuCtxDestroy_v2)(CUcontext ctx);
    CUresult (*cuCtxPushCurrent_v2)(CUcontext ctx);
    CUresult (*cuCtxPopCurrent_v2)(CUcontext *pctx);
    CUresult (*cuGetErrorString)(CUresult error, const char **pStr);
    CUresult (*cuMemcpy2D_v2)(const CUDA_MEMCPY2D *pCopy);
    CUresult (*cuMemcpy2DAsync_v2)(const CUDA_MEMCPY2D *pcopy, CUstream hStream);
    CUresult (*cuStreamSynchronize)(CUstream hStream);

    CUresult (*cuGraphicsGLRegisterImage)(CUgraphicsResource *pCudaResource, unsigned int image, unsigned int target, unsigned int flags);
    CUresult (*cuGraphicsEGLRegisterImage)(CUgraphicsResource *pCudaResource, void *image, unsigned int flags);
    CUresult (*cuGraphicsResourceSetMapFlags)(CUgraphicsResource resource, unsigned int flags);
    CUresult (*cuGraphicsMapResources)(unsigned int count, CUgraphicsResource *resources, CUstream hStream);
    CUresult (*cuGraphicsUnmapResources)(unsigned int count, CUgraphicsResource *resources, CUstream hStream);
    CUresult (*cuGraphicsUnregisterResource)(CUgraphicsResource resource);
    CUresult (*cuGraphicsSubResourceGetMappedArray)(CUarray *pArray, CUgraphicsResource resource, unsigned int arrayIndex, unsigned int mipLevel);
};

bool gsr_cuda_load(gsr_cuda *self);
void gsr_cuda_unload(gsr_cuda *self);

#endif /* GSR_CUDA_H */

// Collapsar C ABI bridge.
//
// This header exposes a flat C interface around collapsar::Engine for
// consumption by managed callers (the WinUI GUI) via P/Invoke. Keeping the
// surface in C — rather than a C++/CLI assembly — means we don't depend on the
// Visual Studio C++/CLI workload, the build works the same on every Windows
// machine that has MSVC, and managed/native interop happens at a single,
// well-defined boundary.
//
// All strings are UTF-8. All sizes are byte counts. The handle types are
// opaque; create them with the corresponding *_create function and destroy
// them with the matching *_destroy.

#ifndef COLLAPSAR_C_BRIDGE_H
#define COLLAPSAR_C_BRIDGE_H

#include <stdint.h>

#if defined(_WIN32)
#  ifdef COLLAPSAR_BRIDGE_EXPORTS
#    define COLLAPSAR_BRIDGE_API __declspec(dllexport)
#  else
#    define COLLAPSAR_BRIDGE_API __declspec(dllimport)
#  endif
#  define COLLAPSAR_BRIDGE_CALL __cdecl
#else
#  define COLLAPSAR_BRIDGE_API  __attribute__((visibility("default")))
#  define COLLAPSAR_BRIDGE_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ---- Enums (mirror collapsar/format.hpp + error.hpp) -----------------------

typedef enum collapsar_format {
    COLLAPSAR_FORMAT_ZIP  = 0,
    COLLAPSAR_FORMAT_GZIP = 1,
    COLLAPSAR_FORMAT_ZSTD = 2
} collapsar_format_t;

typedef enum collapsar_algorithm {
    COLLAPSAR_ALGO_AUTO          = 0,
    COLLAPSAR_ALGO_CPU_DEFLATE   = 1,
    COLLAPSAR_ALGO_GPU_DEFLATE   = 2,
    COLLAPSAR_ALGO_GPU_GDEFLATE  = 3,
    COLLAPSAR_ALGO_CPU_ZSTD      = 4,
    COLLAPSAR_ALGO_GPU_ZSTD      = 5
} collapsar_algorithm_t;

typedef enum collapsar_level {
    COLLAPSAR_LEVEL_FAST     = 0,
    COLLAPSAR_LEVEL_BALANCED = 1,
    COLLAPSAR_LEVEL_BEST     = 2
} collapsar_level_t;

typedef enum collapsar_status {
    COLLAPSAR_OK                 = 0,
    COLLAPSAR_CANCELLED          = 1,
    COLLAPSAR_INVALID_ARGUMENT   = 2,
    COLLAPSAR_NOT_FOUND          = 3,
    COLLAPSAR_PERMISSION_DENIED  = 4,
    COLLAPSAR_IO                 = 5,
    COLLAPSAR_OUT_OF_MEMORY      = 6,
    COLLAPSAR_CUDA_ERROR         = 7,
    COLLAPSAR_NVCOMP_ERROR       = 8,
    COLLAPSAR_CODEC_ERROR        = 9,
    COLLAPSAR_FORMAT_ERROR       = 10,
    COLLAPSAR_UNSUPPORTED        = 11,
    COLLAPSAR_INTERNAL           = 12
} collapsar_status_t;

// ---- Capabilities ----------------------------------------------------------

typedef struct collapsar_capabilities {
    int32_t  has_cuda;             // 0/1
    int32_t  cuda_device_count;
    // device_names is a single utf-8 buffer containing `cuda_device_count`
    // null-terminated strings concatenated. The buffer is owned by the bridge
    // and freed by collapsar_capabilities_free. May be NULL when count==0.
    char*    cuda_device_names;
    uint64_t cuda_device_names_size;  // total byte length including all NULs
} collapsar_capabilities_t;

COLLAPSAR_BRIDGE_API void COLLAPSAR_BRIDGE_CALL
collapsar_probe_capabilities(collapsar_capabilities_t* out);

COLLAPSAR_BRIDGE_API void COLLAPSAR_BRIDGE_CALL
collapsar_capabilities_free(collapsar_capabilities_t* caps);

// ---- Job options ------------------------------------------------------------

typedef struct collapsar_job_options {
    int32_t  format;            // collapsar_format_t
    int32_t  algorithm;         // collapsar_algorithm_t
    int32_t  level;             // collapsar_level_t
    uint64_t gpu_threshold_bytes;
    uint64_t gpu_batch_bytes;
    uint64_t block_bytes;
    int32_t  recurse;           // 0/1
    int32_t  overwrite;         // 0/1
} collapsar_job_options_t;

COLLAPSAR_BRIDGE_API void COLLAPSAR_BRIDGE_CALL
collapsar_job_options_default(collapsar_job_options_t* out);

// ---- Progress callback ------------------------------------------------------

typedef struct collapsar_progress_event {
    uint64_t total_files;
    uint64_t total_bytes;
    uint64_t processed_files;
    uint64_t processed_bytes;
    uint64_t output_bytes;
    double   throughput_mibps;
    // current_file is owned by the engine for the duration of the callback.
    const char* current_file;
} collapsar_progress_event_t;

typedef void (COLLAPSAR_BRIDGE_CALL *collapsar_progress_cb)(
    const collapsar_progress_event_t* event,
    void*                              user_data);

// ---- Job result -------------------------------------------------------------

typedef struct collapsar_job_result {
    int32_t  status;             // collapsar_status_t
    // message is owned by the bridge and must be freed via
    // collapsar_job_result_free. Always non-null on out, possibly empty.
    char*    message;
    uint64_t files_processed;
    uint64_t input_bytes;
    uint64_t output_bytes;
    uint64_t cpu_blocks;
    uint64_t gpu_blocks;
    uint64_t elapsed_millis;
} collapsar_job_result_t;

COLLAPSAR_BRIDGE_API void COLLAPSAR_BRIDGE_CALL
collapsar_job_result_free(collapsar_job_result_t* result);

// ---- Engine -----------------------------------------------------------------

typedef struct collapsar_engine_handle collapsar_engine_handle;

COLLAPSAR_BRIDGE_API collapsar_engine_handle* COLLAPSAR_BRIDGE_CALL
collapsar_engine_create(int32_t  cuda_device,
                        int32_t  enable_gpu,
                        int32_t  io_threads,
                        int32_t  cpu_threads);

COLLAPSAR_BRIDGE_API void COLLAPSAR_BRIDGE_CALL
collapsar_engine_destroy(collapsar_engine_handle* engine);

// Synchronous pack. Blocks until the job completes. The progress callback (if
// non-null) is invoked from a background thread; consumers must marshal back
// to their UI thread themselves.
//
// `inputs` is an array of UTF-8 paths. `inputs_count` is its length.
COLLAPSAR_BRIDGE_API void COLLAPSAR_BRIDGE_CALL
collapsar_engine_pack(collapsar_engine_handle*       engine,
                      const char* const*             inputs,
                      uint64_t                       inputs_count,
                      const char*                    output,
                      const collapsar_job_options_t* options,
                      collapsar_progress_cb          on_progress,
                      void*                          user_data,
                      collapsar_job_result_t*        out_result);

// Request cancellation of the currently running pack call. Idempotent.
COLLAPSAR_BRIDGE_API void COLLAPSAR_BRIDGE_CALL
collapsar_engine_cancel(collapsar_engine_handle* engine);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // COLLAPSAR_C_BRIDGE_H

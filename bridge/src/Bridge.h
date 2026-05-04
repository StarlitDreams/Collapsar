// Collapsar.Bridge — managed surface around the native engine.
//
// This header is consumed by the GUI through C++/CLI marshalling. Keep it
// dependency-free: no native types in public signatures, only managed
// strings, arrays, and the cli-managed enums declared below.

#pragma once

#using <System.dll>

namespace Collapsar {
namespace Bridge {

public enum class Format {
    Zip  = 0,
    Gzip = 1,
    Zstd = 2,
};

public enum class Algorithm {
    Auto         = 0,
    CpuDeflate   = 1,
    GpuDeflate   = 2,
    GpuGDeflate  = 3,
    CpuZstd      = 4,
    GpuZstd      = 5,
};

public enum class Level {
    Fast     = 0,
    Balanced = 1,
    Best     = 2,
};

public enum class StatusCode {
    Ok               = 0,
    Cancelled        = 1,
    InvalidArgument  = 2,
    NotFound         = 3,
    PermissionDenied = 4,
    Io               = 5,
    OutOfMemory      = 6,
    CudaError        = 7,
    NvcompError      = 8,
    CodecError       = 9,
    FormatError      = 10,
    Unsupported      = 11,
    Internal         = 12,
};

public ref class CapabilitiesInfo sealed {
public:
    property bool                      HasCuda;
    property int                       CudaDeviceCount;
    property array<System::String^>^   CudaDeviceNames;
};

public ref class ProgressSnapshot sealed {
public:
    property System::UInt64 TotalFiles;
    property System::UInt64 TotalBytes;
    property System::UInt64 ProcessedFiles;
    property System::UInt64 ProcessedBytes;
    property System::UInt64 OutputBytes;
    property System::String^ CurrentFile;
    property double          ThroughputMiBps;
};

public delegate void ProgressDelegate(ProgressSnapshot^ snapshot);

public ref class JobOptions sealed {
public:
    property Format    Format    { Collapsar::Bridge::Format    get(); void set(Collapsar::Bridge::Format    v); }
    property Algorithm Algorithm { Collapsar::Bridge::Algorithm get(); void set(Collapsar::Bridge::Algorithm v); }
    property Level     Level     { Collapsar::Bridge::Level     get(); void set(Collapsar::Bridge::Level     v); }
    property System::UInt64 GpuThresholdBytes;
    property System::UInt64 GpuBatchBytes;
    property System::UInt64 BlockBytes;
    property bool           Recurse;
    property bool           Overwrite;

    JobOptions();

private:
    Collapsar::Bridge::Format    format_;
    Collapsar::Bridge::Algorithm algorithm_;
    Collapsar::Bridge::Level     level_;
};

public ref class JobResult sealed {
public:
    property StatusCode      Status;
    property System::String^ Message;

    property System::UInt64  FilesProcessed;
    property System::UInt64  InputBytes;
    property System::UInt64  OutputBytes;
    property System::UInt64  CpuBlocks;
    property System::UInt64  GpuBlocks;
    property System::UInt64  ElapsedMillis;
};

// CompressionEngine wraps a single native collapsar::Engine. Construct one
// per session — internally it owns thread pools and CUDA streams.
public ref class CompressionEngine sealed {
public:
    CompressionEngine();
    CompressionEngine(int cudaDevice, bool enableGpu, int ioThreads, int cpuThreads);
    !CompressionEngine();
    ~CompressionEngine();

    static CapabilitiesInfo^ ProbeCapabilities();

    JobResult^ Pack(array<System::String^>^ inputs,
                    System::String^         output,
                    JobOptions^             options,
                    ProgressDelegate^       onProgress);

    void Cancel();

private:
    // Forward-declared opaque holder for the native engine — pinned via a
    // GCHandle so it survives across managed/unmanaged transitions.
    System::IntPtr enginePtr_;
    System::IntPtr currentJobPtr_;
};

} // namespace Bridge
} // namespace Collapsar

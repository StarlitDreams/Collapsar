// P/Invoke wrappers around Collapsar.Bridge.dll. The DLL exposes a flat C
// ABI defined in bridge/src/c_bridge.h; this file mirrors those declarations
// and provides a small set of friendly managed types on top.

using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Collapsar.App.Bridge;

internal enum BridgeFormat
{
    Zip  = 0,
    Gzip = 1,
    Zstd = 2,
}

internal enum BridgeAlgorithm
{
    Auto         = 0,
    CpuDeflate   = 1,
    GpuDeflate   = 2,
    GpuGDeflate  = 3,
    CpuZstd      = 4,
    GpuZstd      = 5,
}

internal enum BridgeLevel
{
    Fast     = 0,
    Balanced = 1,
    Best     = 2,
}

internal enum BridgeStatus
{
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
}

internal sealed class CapabilitiesInfo
{
    public bool     HasCuda          { get; init; }
    public int      CudaDeviceCount  { get; init; }
    public string[] CudaDeviceNames  { get; init; } = Array.Empty<string>();
}

internal sealed class JobOptions
{
    public BridgeFormat    Format             { get; set; } = BridgeFormat.Zip;
    public BridgeAlgorithm Algorithm          { get; set; } = BridgeAlgorithm.Auto;
    public BridgeLevel     Level              { get; set; } = BridgeLevel.Balanced;
    public ulong           GpuThresholdBytes  { get; set; } = 1UL * 1024 * 1024;
    public ulong           GpuBatchBytes      { get; set; } = 256UL * 1024 * 1024;
    public ulong           BlockBytes         { get; set; } = 8UL * 1024 * 1024;
    public bool            Recurse            { get; set; } = true;
    public bool            Overwrite          { get; set; }
}

internal sealed class ProgressSnapshot
{
    public ulong  TotalFiles      { get; init; }
    public ulong  TotalBytes      { get; init; }
    public ulong  ProcessedFiles  { get; init; }
    public ulong  ProcessedBytes  { get; init; }
    public ulong  OutputBytes     { get; init; }
    public string CurrentFile     { get; init; } = string.Empty;
    public double ThroughputMiBps { get; init; }
}

internal sealed class JobResult
{
    public BridgeStatus Status          { get; init; }
    public string       Message         { get; init; } = string.Empty;
    public ulong        FilesProcessed  { get; init; }
    public ulong        InputBytes      { get; init; }
    public ulong        OutputBytes     { get; init; }
    public ulong        CpuBlocks       { get; init; }
    public ulong        GpuBlocks       { get; init; }
    public ulong        ElapsedMillis   { get; init; }
}

internal delegate void ProgressHandler(ProgressSnapshot snapshot);

internal static class NativeBridge
{
    private const string Dll = "Collapsar.Bridge";

    [StructLayout(LayoutKind.Sequential)]
    private struct CapabilitiesNative
    {
        public int    has_cuda;
        public int    cuda_device_count;
        public IntPtr cuda_device_names;
        public ulong  cuda_device_names_size;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct JobOptionsNative
    {
        public int   format;
        public int   algorithm;
        public int   level;
        public ulong gpu_threshold_bytes;
        public ulong gpu_batch_bytes;
        public ulong block_bytes;
        public int   recurse;
        public int   overwrite;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct ProgressEventNative
    {
        public ulong  total_files;
        public ulong  total_bytes;
        public ulong  processed_files;
        public ulong  processed_bytes;
        public ulong  output_bytes;
        public double throughput_mibps;
        public IntPtr current_file;       // utf-8 c string
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct JobResultNative
    {
        public int    status;
        public IntPtr message;            // utf-8 c string, owned by bridge
        public ulong  files_processed;
        public ulong  input_bytes;
        public ulong  output_bytes;
        public ulong  cpu_blocks;
        public ulong  gpu_blocks;
        public ulong  elapsed_millis;
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void ProgressCallbackNative(ref ProgressEventNative ev, IntPtr userData);

    // ---- DllImports (kept as raw IntPtrs/structs so we can manage strings) -

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "collapsar_probe_capabilities")]
    private static extern void ProbeCapabilitiesNative(out CapabilitiesNative outCaps);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "collapsar_capabilities_free")]
    private static extern void CapabilitiesFreeNative(ref CapabilitiesNative caps);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "collapsar_engine_create")]
    internal static extern IntPtr EngineCreate(int cudaDevice, int enableGpu, int ioThreads, int cpuThreads);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "collapsar_engine_destroy")]
    internal static extern void EngineDestroy(IntPtr engine);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "collapsar_engine_cancel")]
    internal static extern void EngineCancel(IntPtr engine);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "collapsar_engine_pack")]
    private static extern void EnginePackNative(
        IntPtr                  engine,
        IntPtr[]                inputs,
        ulong                   inputsCount,
        IntPtr                  output,
        ref JobOptionsNative    options,
        ProgressCallbackNative? onProgress,
        IntPtr                  userData,
        out JobResultNative     outResult);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, EntryPoint = "collapsar_job_result_free")]
    private static extern void JobResultFreeNative(ref JobResultNative result);

    // ---- Friendly wrappers --------------------------------------------------

    public static CapabilitiesInfo ProbeCapabilities()
    {
        ProbeCapabilitiesNative(out var native);
        try
        {
            var info = new CapabilitiesInfo
            {
                HasCuda          = native.has_cuda != 0,
                CudaDeviceCount  = native.cuda_device_count,
                CudaDeviceNames  = ReadEmbeddedStrings(native.cuda_device_names,
                                                       native.cuda_device_names_size,
                                                       native.cuda_device_count),
            };
            return info;
        }
        finally
        {
            CapabilitiesFreeNative(ref native);
        }
    }

    public static JobResult Pack(IntPtr             engine,
                                  string[]          inputs,
                                  string            output,
                                  JobOptions        options,
                                  ProgressHandler?  onProgress)
    {
        if (engine == IntPtr.Zero)
            throw new InvalidOperationException("Engine handle is null");

        // Marshal each input path to UTF-8 once and hand the array of pointers
        // to the bridge.
        var inputPtrs = new IntPtr[inputs.Length];
        var allocated = new IntPtr[inputs.Length];
        IntPtr outputPtr = IntPtr.Zero;
        try
        {
            for (int i = 0; i < inputs.Length; ++i)
            {
                inputPtrs[i]  = AllocUtf8(inputs[i]);
                allocated[i]  = inputPtrs[i];
            }
            outputPtr = AllocUtf8(output);

            var nativeOptions = new JobOptionsNative
            {
                format               = (int)options.Format,
                algorithm            = (int)options.Algorithm,
                level                = (int)options.Level,
                gpu_threshold_bytes  = options.GpuThresholdBytes,
                gpu_batch_bytes      = options.GpuBatchBytes,
                block_bytes          = options.BlockBytes,
                recurse              = options.Recurse  ? 1 : 0,
                overwrite            = options.Overwrite ? 1 : 0,
            };

            // Wrap the managed handler in an unmanaged function pointer that
            // marshals the snapshot. Keep a strong reference for the lifetime
            // of the call so the GC doesn't collect it while native code runs.
            ProgressCallbackNative? cb = null;
            if (onProgress != null)
            {
                cb = (ref ProgressEventNative ev, IntPtr userData) =>
                {
                    onProgress(new ProgressSnapshot
                    {
                        TotalFiles      = ev.total_files,
                        TotalBytes      = ev.total_bytes,
                        ProcessedFiles  = ev.processed_files,
                        ProcessedBytes  = ev.processed_bytes,
                        OutputBytes     = ev.output_bytes,
                        ThroughputMiBps = ev.throughput_mibps,
                        CurrentFile     = ReadUtf8(ev.current_file) ?? string.Empty,
                    });
                };
            }

            EnginePackNative(engine,
                              inputPtrs,
                              (ulong)inputs.Length,
                              outputPtr,
                              ref nativeOptions,
                              cb,
                              IntPtr.Zero,
                              out var nativeResult);

            // Touch `cb` after the call so the JIT doesn't elide it earlier.
            GC.KeepAlive(cb);

            try
            {
                return new JobResult
                {
                    Status         = (BridgeStatus)nativeResult.status,
                    Message        = ReadUtf8(nativeResult.message) ?? string.Empty,
                    FilesProcessed = nativeResult.files_processed,
                    InputBytes     = nativeResult.input_bytes,
                    OutputBytes    = nativeResult.output_bytes,
                    CpuBlocks      = nativeResult.cpu_blocks,
                    GpuBlocks      = nativeResult.gpu_blocks,
                    ElapsedMillis  = nativeResult.elapsed_millis,
                };
            }
            finally
            {
                JobResultFreeNative(ref nativeResult);
            }
        }
        finally
        {
            foreach (var p in allocated) if (p != IntPtr.Zero) Marshal.FreeHGlobal(p);
            if (outputPtr != IntPtr.Zero) Marshal.FreeHGlobal(outputPtr);
        }
    }

    // ---- Helpers ------------------------------------------------------------

    private static IntPtr AllocUtf8(string? s)
    {
        if (s is null) return IntPtr.Zero;
        var bytes = Encoding.UTF8.GetBytes(s);
        var ptr   = Marshal.AllocHGlobal(bytes.Length + 1);
        Marshal.Copy(bytes, 0, ptr, bytes.Length);
        Marshal.WriteByte(ptr, bytes.Length, 0);
        return ptr;
    }

    private static string? ReadUtf8(IntPtr ptr)
    {
        if (ptr == IntPtr.Zero) return null;
        // Walk for the null terminator without doing two passes.
        int len = 0;
        while (Marshal.ReadByte(ptr, len) != 0) len++;
        if (len == 0) return string.Empty;
        var bytes = new byte[len];
        Marshal.Copy(ptr, bytes, 0, len);
        return Encoding.UTF8.GetString(bytes);
    }

    private static string[] ReadEmbeddedStrings(IntPtr buffer, ulong totalBytes, int count)
    {
        if (buffer == IntPtr.Zero || count <= 0 || totalBytes == 0)
            return Array.Empty<string>();

        var raw = new byte[(int)totalBytes];
        Marshal.Copy(buffer, raw, 0, raw.Length);

        var result = new string[count];
        int cursor = 0;
        for (int i = 0; i < count && cursor < raw.Length; ++i)
        {
            int end = cursor;
            while (end < raw.Length && raw[end] != 0) end++;
            result[i] = Encoding.UTF8.GetString(raw, cursor, end - cursor);
            cursor    = end + 1;  // skip the NUL
        }
        return result;
    }
}

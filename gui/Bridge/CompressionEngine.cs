// CompressionEngine — high-level wrapper around the native Collapsar engine.
//
// Owns a native engine handle for its lifetime. Construction is cheap (the
// native side spins up its thread pools lazily inside `Pack`) so it's fine to
// create one per ViewModel. Dispose to release native resources promptly;
// otherwise the finalizer will free them.

using System;

namespace Collapsar.App.Bridge;

internal sealed class CompressionEngine : IDisposable
{
    private IntPtr _handle;

    public CompressionEngine() : this(0, true, 4, 0) {}

    public CompressionEngine(int cudaDevice, bool enableGpu, int ioThreads, int cpuThreads)
    {
        _handle = NativeBridge.EngineCreate(cudaDevice, enableGpu ? 1 : 0, ioThreads, cpuThreads);
        if (_handle == IntPtr.Zero)
            throw new InvalidOperationException("Failed to create native Collapsar engine");
    }

    ~CompressionEngine() => Dispose(false);

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    private void Dispose(bool disposing)
    {
        if (_handle == IntPtr.Zero) return;
        NativeBridge.EngineDestroy(_handle);
        _handle = IntPtr.Zero;
    }

    public static CapabilitiesInfo ProbeCapabilities() => NativeBridge.ProbeCapabilities();

    public JobResult Pack(string[]         inputs,
                           string            output,
                           JobOptions        options,
                           ProgressHandler?  onProgress)
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(CompressionEngine));
        return NativeBridge.Pack(_handle, inputs, output, options, onProgress);
    }

    public void Cancel()
    {
        if (_handle != IntPtr.Zero) NativeBridge.EngineCancel(_handle);
    }
}

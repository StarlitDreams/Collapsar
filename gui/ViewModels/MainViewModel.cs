using System;
using System.Collections.ObjectModel;
using System.Threading.Tasks;
using Collapsar.Bridge;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Dispatching;

namespace Collapsar.App.ViewModels;

// MVVM façade between the WinUI views and the Collapsar.Bridge engine.
// All long-running work runs off the UI thread; progress callbacks marshal
// back via the captured DispatcherQueue so UI bindings stay on-thread.
public partial class MainViewModel : ObservableObject
{
    private readonly DispatcherQueue _dispatcher = DispatcherQueue.GetForCurrentThread();
    private CompressionEngine? _engine;

    public ObservableCollection<string> Inputs { get; } = new();

    public string[] AvailableFormats { get; } = { "Zip", "Gzip", "Zstd" };
    public string[] AvailableLevels  { get; } = { "Fast", "Balanced", "Best" };

    [ObservableProperty]
    private string? _outputPath;

    [ObservableProperty]
    private string _selectedFormat = "Zip";

    [ObservableProperty]
    private string _selectedLevel = "Balanced";

    [ObservableProperty]
    private bool _useGpu = true;

    [ObservableProperty]
    private double _progressPercent;

    [ObservableProperty]
    private string _statusLine = "Ready";

    public bool   IsEmpty       => Inputs.Count == 0;
    public bool   GpuAvailable  { get; }
    public string GpuStatus     { get; }

    public IRelayCommand PackCommand   { get; }
    public IRelayCommand CancelCommand { get; }

    public MainViewModel()
    {
        var caps = CompressionEngine.ProbeCapabilities();
        GpuAvailable = caps.HasCuda && caps.CudaDeviceCount > 0;
        GpuStatus    = GpuAvailable
            ? $"GPU: {caps.CudaDeviceNames[0]}"
            : "GPU: not available";
        UseGpu = GpuAvailable;

        Inputs.CollectionChanged += (_, _) =>
        {
            OnPropertyChanged(nameof(IsEmpty));
            (PackCommand as IRelayCommand)?.NotifyCanExecuteChanged();
        };

        PackCommand   = new AsyncRelayCommand(PackAsync, CanPack);
        CancelCommand = new RelayCommand(Cancel);
    }

    public void AddInput(string path)
    {
        if (!Inputs.Contains(path)) Inputs.Add(path);
    }

    private bool CanPack() => Inputs.Count > 0 && !string.IsNullOrEmpty(OutputPath);

    partial void OnOutputPathChanged(string? value) => PackCommand.NotifyCanExecuteChanged();

    private async Task PackAsync()
    {
        StatusLine      = "Starting…";
        ProgressPercent = 0;

        // Construct the engine on demand; reuse across runs in the same session.
        _engine ??= new CompressionEngine(
            cudaDevice: 0,
            enableGpu:  UseGpu && GpuAvailable,
            ioThreads:  4,
            cpuThreads: 0);

        var inputs = new string[Inputs.Count];
        Inputs.CopyTo(inputs, 0);

        var options = new JobOptions
        {
            Format    = ParseFormat(SelectedFormat),
            Algorithm = UseGpu && GpuAvailable ? Algorithm.Auto : Algorithm.CpuDeflate,
            Level     = ParseLevel(SelectedLevel),
            Overwrite = true,
        };

        var progress = new ProgressDelegate(snap =>
        {
            // Marshal back to the UI thread before touching bound properties.
            _dispatcher.TryEnqueue(() =>
            {
                ProgressPercent = snap.TotalBytes == 0
                    ? 0
                    : 100.0 * snap.ProcessedBytes / snap.TotalBytes;
                StatusLine = $"{snap.ProcessedFiles}/{snap.TotalFiles}  "
                           + $"{snap.ProcessedBytes / (1024 * 1024)} MiB → "
                           + $"{snap.OutputBytes  / (1024 * 1024)} MiB  "
                           + $"({snap.ThroughputMiBps:F0} MiB/s)";
            });
        });

        var result = await Task.Run(() => _engine!.Pack(inputs, OutputPath!, options, progress));

        _dispatcher.TryEnqueue(() =>
        {
            ProgressPercent = 100;
            StatusLine = result.Status == StatusCode.Ok
                ? $"Done — {result.OutputBytes / 1024} KiB written, "
                  + $"{result.GpuBlocks} GPU / {result.CpuBlocks} CPU blocks, "
                  + $"{result.ElapsedMillis} ms"
                : $"Error: {result.Status} — {result.Message}";
        });
    }

    private void Cancel()
    {
        _engine?.Cancel();
        StatusLine = "Cancelling…";
    }

    private static Format ParseFormat(string s) => s switch
    {
        "Gzip" => Format.Gzip,
        "Zstd" => Format.Zstd,
        _      => Format.Zip,
    };

    private static Level ParseLevel(string s) => s switch
    {
        "Fast" => Level.Fast,
        "Best" => Level.Best,
        _      => Level.Balanced,
    };
}

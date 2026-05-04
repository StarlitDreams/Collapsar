# Collapsar.App — WinUI 3 GUI

The desktop front-end. Built with WinAppSDK 1.5+ and the Community Toolkit
MVVM source generators.

## Layout

```
gui/
├── Collapsar.App.csproj   # WinAppSDK csproj, references Collapsar.Bridge
├── App.xaml(.cs)          # Application bootstrap
├── MainWindow.xaml(.cs)   # Single window: input list, options, progress
├── ViewModels/
│   └── MainViewModel.cs   # Drives the engine off the UI thread
└── Properties/launchSettings.json
```

## Build prerequisites

1. Build the native engine + bridge first:
   ```powershell
   cmake --preset windows-cuda
   cmake --build --preset windows-cuda --config Release
   ```
   This produces `build\windows-cuda\bridge\Release\Collapsar.Bridge.dll`,
   which `Collapsar.App.csproj` references via `<HintPath>`.

2. Then build the GUI:
   ```powershell
   dotnet restore gui\Collapsar.App.csproj
   dotnet build  gui\Collapsar.App.csproj -c Release
   ```

   Or open the project in Visual Studio 2022 with the WinAppSDK workload
   installed and hit F5.

## Threading

`MainViewModel.PackAsync` runs on a Task thread; the bridge invokes the
progress callback from the native pipeline thread; the callback hops back
onto the UI dispatcher via `DispatcherQueue.TryEnqueue` before mutating any
bound property. Don't touch UI state from the bridge thread directly — WinUI
will throw a thread-affinity exception.

# Collapsar.App — WinUI 3 GUI

The desktop front-end. Built with WinAppSDK 1.5+ and the Community Toolkit
MVVM source generators. Talks to the native engine through the
`Collapsar.Bridge.dll` flat C-ABI shared library via P/Invoke.

## Layout

```
gui/
├── Collapsar.App.csproj   # WinAppSDK csproj, copies Collapsar.Bridge.dll alongside the exe
├── App.xaml(.cs)          # Application bootstrap
├── MainWindow.xaml(.cs)   # Single window: input list, options, progress
├── Bridge/
│   ├── NativeBridge.cs    # P/Invoke declarations for Collapsar.Bridge.dll
│   └── CompressionEngine.cs  # IDisposable wrapper around the native handle
├── ViewModels/
│   └── MainViewModel.cs   # Drives the engine off the UI thread
└── Properties/launchSettings.json
```

## Build prerequisites

1. Build the native engine + bridge first:
   ```powershell
   cmake --preset windows
   cmake --build --preset windows
   ```
   This produces `build\windows\bin\Release\Collapsar.Bridge.dll` (and the
   accompanying `z.dll`), which `Collapsar.App.csproj` copies into the GUI
   output directory at build time.

2. Then build the GUI:
   ```powershell
   dotnet restore gui\Collapsar.App.csproj
   dotnet build  gui\Collapsar.App.csproj -c Release -p:Platform=x64
   ```

   Run:
   ```powershell
   .\gui\bin\x64\Release\net8.0-windows10.0.19041.0\Collapsar.App.exe
   ```

   Or open the project in Visual Studio with the WinAppSDK workload installed
   and hit F5.

## Threading

`MainViewModel.PackAsync` runs on a `Task.Run` thread; the bridge invokes the
progress callback from the native pipeline thread; the callback hops back
onto the UI dispatcher via `DispatcherQueue.TryEnqueue` before mutating any
bound property. Don't touch UI state from the bridge thread directly — WinUI
will throw a thread-affinity exception.

## Why P/Invoke instead of C++/CLI?

The earlier `Collapsar.Bridge` was a C++/CLI assembly. It had two practical
problems on a fresh Windows install:

1. C++/CLI requires a separate Visual Studio workload that isn't installed
   by default and is fiddly to keep in sync between VS major versions.
2. `/clr` is incompatible with several of the project's usual compile flags
   (`/EHsc`, `/RTCx`), so its CMake target had to override them.

A flat C ABI sidesteps both problems and adds maybe 40 lines of marshalling
code on the managed side. See `bridge/src/c_bridge.h` for the surface and
`Bridge/NativeBridge.cs` for the P/Invoke declarations.

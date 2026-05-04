using System;
using Collapsar.App.ViewModels;
using Microsoft.UI.Xaml;
using Windows.Storage.Pickers;
using WinRT.Interop;

namespace Collapsar.App;

public sealed partial class MainWindow : Window
{
    public MainViewModel ViewModel { get; } = new MainViewModel();

    public MainWindow()
    {
        InitializeComponent();
    }

    // The Win32-style file pickers need an HWND to anchor against. Grab it
    // off the WindowNative interop hook and feed it into InitializeWithWindow.
    private IntPtr GetHwnd() => WindowNative.GetWindowHandle(this);

    private async void OnAddFilesClicked(object sender, RoutedEventArgs e)
    {
        var picker = new FileOpenPicker
        {
            ViewMode               = PickerViewMode.List,
            SuggestedStartLocation = PickerLocationId.Downloads,
        };
        picker.FileTypeFilter.Add("*");
        InitializeWithWindow.Initialize(picker, GetHwnd());

        var files = await picker.PickMultipleFilesAsync();
        if (files == null) return;
        foreach (var file in files)
        {
            ViewModel.AddInput(file.Path);
        }
    }

    private async void OnChooseOutputClicked(object sender, RoutedEventArgs e)
    {
        var picker = new FileSavePicker
        {
            SuggestedStartLocation = PickerLocationId.Downloads,
            SuggestedFileName      = "archive",
        };
        picker.FileTypeChoices.Add("ZIP archive",  new[] { ".zip" });
        picker.FileTypeChoices.Add("Gzip stream",  new[] { ".gz"  });
        picker.FileTypeChoices.Add("Zstd stream",  new[] { ".zst" });
        InitializeWithWindow.Initialize(picker, GetHwnd());

        var file = await picker.PickSaveFileAsync();
        if (file == null) return;
        ViewModel.OutputPath = file.Path;
    }
}

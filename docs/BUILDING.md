# Building the official Windows filter

Requires Visual Studio 2022 Build Tools, Windows SDK, CMake 3.24+, a compatible OBS development SDK and Qt 6.8+ Widgets. VST3 host dependency source is pinned and included under third_party.

```powershell
./scripts/build-release.ps1 -ObsSdkRoot C:/SDK/obs `
  -ObsDependenciesRoot C:/SDK/obs-deps `
  -ObsQtRoot C:/SDK/obs-qt `
  -CMake C:/Tools/CMake/bin/cmake.exe
```

Adjust the paths to your SDKs. This builds and installs **obs-vst3-nexus.dll** and **nexus_vst3_worker.exe** into a staging directory. It disables internal tools and testing for the release build. Do not distribute internal fixture modules or historical rack targets.

The historical rack and equalizer UI targets remain available for regression development but are excluded from the default build unless required by a requested target. Some shared DSP/state code remains necessary for the single-effect host; directory names alone do not establish dead code.

The source package can be built independently of the author's workstation. No commercial VST3 effects are needed to compile the product. Qt and libobs runtimes come from OBS and are not redistributed in the installer.

# Building and publishing

## Supported build route

The supported initial route is MSVC / Visual Studio 2022 on Windows. Target
Windows 10 version 1607 or later, including Windows 11. Native x64 and ARM64
configurations are provided. No Python, Conan, SDL, Vulkan SDK, Node.js or external
C++ package download is needed for this diagnostic.

On the **build PC**, supply:

- Visual Studio 2022 or its Build Tools with Desktop development with C++.
- MSVC v143 x64/x86 tools for an x64 build and host tests.
- MSVC v143 ARM64 tools for an ARM64 target build.
- A Windows 10/11 SDK and CMake 3.24 or newer available on PATH, including CTest.
- PowerShell (the build helper uses `Compress-Archive`).

No prerequisite was installed as part of creating this repository. The helper
only checks for CMake and invokes installed tools; it does not run installers.

## Build and package

From the repository root:

```powershell
./scripts/verify-source.ps1
./scripts/build.ps1 -Architecture ARM64
```

For an Intel/AMD target:

```powershell
./scripts/build.ps1 -Architecture x64
```

If local execution policy blocks scripts, use an approved shell configuration or
the manual commands below; this project does not change execution policy.

The output is `out/PenTraceLab-ARM64/` and `out/PenTraceLab-ARM64.zip` (or x64).
Copy the ZIP to the drawing device, extract it and launch `PenTraceLab.exe`.
The release uses the static MSVC runtime and system Windows graphics/input DLLs.
No admin rights or app installer are required. Unsigned builds may trigger
SmartScreen; inspect the source/build origin and follow your organization's policy.

If building ARM64 on x64, the helper also builds and runs an **x64 core test
executable**. It does not pretend that this runs the ARM64 GUI. Native ARM64 GUI
and pen/touch testing must still happen on the target device.

Manual equivalent for x64:

```powershell
cmake -S . -B build-x64 -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build-x64 --config Release --parallel
ctest --test-dir build-x64 -C Release --output-on-failure
cmake --install build-x64 --config Release --prefix out/PenTraceLab-x64
```

For ARM64, use a separate `build-ARM64` directory and `-A ARM64`. Do not reuse an
x64 CMake build directory for ARM64. If the generator cannot find ARM64 tools,
add that component on the authorized build PC; do not change the target to x64
and label it ARM64. Other Visual Studio generators can be passed through the
helper's `-Generator` option, but have not been verified.

## Automated checks

The included GitHub Actions workflow builds Windows x64 and ARM64 packages and
runs the portable core tests. ARM64 cross-builds use host x64 tests. A Linux job
runs the core with AddressSanitizer and UndefinedBehaviorSanitizer.

The workflow has not been run merely by writing this repository. Runner images
must contain the selected Windows architecture toolsets. A green workflow proves
only what its jobs execute, not real pen accuracy or UI correctness.

To run the core on a non-Windows machine with a C++20 compiler:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Publish when ready

This directory is a standalone local Git repository. No remote has been created,
no files have been uploaded and no Git identity is assigned by the project.

1. Review the MIT license and source; configure your own Git author identity if needed.
2. Commit the source (`git add .`, then `git commit`). Recordings/builds are ignored.
3. Create your desired remote repository and add its actual URL with `git remote add origin ...`.
4. Push `main`, check the CI results, then perform the hardware acceptance checklist.
5. Publish tested portable ZIPs with architecture, version/commit and known limits.

Do not label a release "100% wobble fixed." This tool measures and compares input;
it does not establish intent or guarantee a hardware-independent fix.

cmake -B build -A Win32 -S . ^
  --toolchain "d:\vcpkg\scripts\buildsystems\vcpkg.cmake" ^
  -DCMAKE_MODULE_PATH:PATH="d:\TC\third_party\chromebase;d:\TC\third_party\promise.hpp"
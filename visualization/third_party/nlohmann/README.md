If `find_package(nlohmann_json)` doesn't find an installed copy on your
system, download the single-header release from:

  https://github.com/nlohmann/json/releases

and place `json.hpp` directly in this folder (`third_party/nlohmann/json.hpp`).
CMakeLists.txt automatically falls back to this path when the package
isn't found via find_package/pkg-config.

-- 
-- @ Author: Amélie Heinrich (amelie@dayiii.com)
-- @ Create Time: 2026-08-02 18:59:01
-- @ Copyright: Day III Digital - All rights reserved
-- 

package("compressonator")
    set_sourcedir(os.scriptdir())
    add_deps("cmake")
    add_links("CMP_Core", "CMP_Core_AVX512", "CMP_Core_AVX", "CMP_Core_SSE")

    on_install(function (package)
        local configs = {}
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:debug() and "Debug" or "Release"))
        import("package.tools.cmake").install(package, configs)
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            void test() {
                CompressBlockBC7(nullptr, 0, nullptr, nullptr);
            }
        ]]}, {configs = {languages = "c++11"}, includes = "cmp_core.h"}))
    end)
package_end()

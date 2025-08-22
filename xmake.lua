add_rules("mode.debug","mode.release")
add_requires("raylib")

-- Main visualizer target
target("jidi-player")
    set_kind("binary")
    set_languages("c++23")
    add_files("src/Mains/visualizer.cpp")
    if is_plat("windows") then
        if os.isfile("resources/icon.rc") then
            add_files("resources/icon.rc")
        end
    end
    add_packages("raylib")
    add_includedirs("external", "header")
    add_linkdirs("external")
    add_defines("RAYGUI_STANDALONE")
    add_links("OmniMIDI_Win64.lib")
    add_syslinks("winmm")
    set_optimize("fastest")

-- MIDI core player target
target("midicore")
    set_kind("binary")
    set_languages("c++23")
    add_files("src/Mains/midicore.cpp")
    add_includedirs("external", "header")
    add_linkdirs("external")
    add_links("OmniMIDI_Win64.lib")
    add_syslinks("winmm")
    set_optimize("fastest")

-- Timing test utility
target("timing-test")
    set_kind("binary")
    set_languages("c++23")
    add_files("src/Mains/timing_test.cpp")
    add_includedirs("header")
    set_optimize("fastest")

-- MIDI hex dump utility
target("midi-hex-dump")
    set_kind("binary")
    set_languages("c++23")
    add_files("src/Mains/midi_hex_dump.cpp")
    set_optimize("fastest")

-- MIDI file analyzer
target("midi-analyzer")
    set_kind("binary")
    set_languages("c++23")
    add_files("src/Mains/midi_analyzer.cpp")
    add_includedirs("header")
    set_optimize("fastest")

-- Track loading test
target("track-test")
    set_kind("binary")
    set_languages("c++23")
    add_files("src/Mains/track_test.cpp")
    add_includedirs("header")
    set_optimize("fastest")
--
-- If you want to known more usage about xmake, please see https://xmake.io
--
-- ## FAQ
--
-- You can enter the project directory firstly before building project.
--
--   $ cd projectdir
--
-- 1. How to build project?
--
--   $ xmake
--
-- 2. How to configure project?
--
--   $ xmake f -p [macosx|linux|iphoneos ..] -a [x86_64|i386|arm64 ..] -m [debug|release]
--
-- 3. Where is the build output directory?
--
--   The default output directory is `./build` and you can configure the output directory.
--
--   $ xmake f -o outputdir
--   $ xmake
--
-- 4. How to run and debug target after building project?
--
--   $ xmake run [targetname]
--   $ xmake run -d [targetname]
--
-- 5. How to install target to the system directory or other output directory?
--
--   $ xmake install
--   $ xmake install -o installdir
--
-- 6. Add some frequently-used compilation flags in xmake.lua
--
-- @code
--    -- add debug and release modes
--    add_rules("mode.debug", "mode.release")
--
--    -- add macro definition
--    add_defines("NDEBUG", "_GNU_SOURCE=1")
--
--    -- set warning all as error
--    set_warnings("all", "error")
--
--    -- set language: c99, c++11
--    set_languages("c99", "c++11")
--
--    -- set optimization: none, faster, fastest, smallest
--    set_optimize("fastest")
--
--    -- add include search directories
--    add_includedirs("/usr/include", "/usr/local/include")
--
--    -- add link libraries and search directories
--    add_links("tbox")
--    add_linkdirs("/usr/local/lib", "/usr/lib")
--
--    -- add system link libraries
--    add_syslinks("z", "pthread")
--
--    -- add compilation and link flags
--    add_cxflags("-stdnolib", "-fno-strict-aliasing")
--    add_ldflags("-L/usr/local/lib", "-lpthread", {force = true})
--
-- @endcode
--


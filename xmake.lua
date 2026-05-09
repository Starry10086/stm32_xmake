-- requires xmake v2.8.5+
---@diagnostic disable: undefined-global
add_rules("plugin.compile_commands.autoupdate", { lsp = "clangd", outputdir = "." })
set_policy("build.warning", true)
set_policy("check.auto_ignore_flags", false)
set_languages("c11", "c++20")

set_config("plat", "cross")
set_config("cross", "arm-none-eabi-")
set_toolchains("gnu-rm")

local function setup_common()
    set_kind("binary")
    add_rules("c++", "asm")
    set_extension(".elf")

    if is_mode("debug") then
        set_optimize("none")
    elseif is_mode("release") then
        set_optimize("fastest")
        add_cxflags("-DNDEBUG")
    end

    on_load("script.read_hal_makefile")

    add_includedirs(".", "Component/ota", "Component/ringbuffer", "Common", "BootLoader/Inc", "App/Inc")

    add_cxflags("-g", "-gdwarf-2")
    add_cxflags("-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard")
    add_asflags("-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard")
    add_ldflags("-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard")

    add_cxflags("-Wall", "-Wextra")
    add_cxflags("-Wno-error=unused", "-Wno-error=unused-variable")
    add_cxflags("-Wno-error=unused-but-set-variable", "-Wno-error=unused-function", "-Wno-unused-parameter")
    add_cxxflags("-Wno-error=unused-local-typedefs")
    add_cxflags("-pedantic-errors")
    add_cxflags("-Wno-strict-prototypes")

    add_defines("USE_HAL_DRIVER", "STM32F407xx")

    add_cxflags("-fdata-sections", "-ffunction-sections")
    add_ldflags("-Wl,--gc-sections")

    add_cxxflags("-fno-exceptions", "-fno-rtti")
    add_cxxflags("-fno-threadsafe-statics")

    add_ldflags("-lc", "-lm", "-lstdc++")
    add_ldflags("-Wl,--print-memory-usage")
end

local function add_bin_postbuild(bin_name)
    after_build(function(target)
        local cross = get_config("cross") or ""
        local objcopy = cross .. "objcopy"
        local sdk = get_config("sdk")
        if sdk then
            objcopy = path.join(sdk, "bin", objcopy)
        end

        local elf_file = target:targetfile()
        local outdir = path.directory(elf_file)
        local bin_file = path.join(outdir, bin_name .. ".bin")

        os.execv(objcopy, { "-O", "binary", elf_file, bin_file })
        cprint("${green}[bin]${clear} %s", bin_file)
    end)
end

target("bootloader", function()
    setup_common()
    set_values("hal_root", "bsp/HAL_boot")
    add_files(
        "BootLoader/Src/*.c",
        "App/Src/debug_uart.c",
        "Common/*.c"
    )
    add_includedirs("Common", "BootLoader/Inc", "bsp/HAL_boot/Core/Inc")
    add_ldflags("-T bsp/HAL_boot/STM32F407XX_BOOT.ld")
    add_ldflags("-Wl,-Map=bootloader.map")
    add_bin_postbuild("bootloader")
end)

target("app", function()
    setup_common()
    set_values("hal_root", "bsp/HAL_app")
    add_files(
        "App/Src/*.c",
        "Common/*.c",
        "Component/ota/*.c"
    )
    add_includedirs("Common", "App/Inc", "BootLoader/Inc", "Component/ota", "bsp/HAL_app/Core/Inc")
    add_ldflags("-T bsp/HAL_app/STM32F407XX_APP1.ld")
    add_ldflags("-Wl,-Map=app.map")
    add_bin_postbuild("app")
end)

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

    -- 从 CubeMX Makefile 自动导入 HAL/CMSIS 源文件和头文件
    on_load("script.read_hal_makefile")

    -- 公共头文件路径
    add_includedirs(".", "Component", "BootLoader/Inc", "App/Inc", "bsp/**")

    -- 调试信息
    add_cxflags("-g", "-gdwarf-2")

    -- Cortex-M4 平台参数
    add_cxflags("-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard")
    add_asflags("-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard")
    add_ldflags("-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard")

    -- 告警策略
    add_cxflags("-Wall", "-Wextra")
    add_cxflags("-Wno-error=unused", "-Wno-error=unused-variable")
    add_cxflags("-Wno-error=unused-but-set-variable", "-Wno-error=unused-function", "-Wno-unused-parameter")
    add_cxxflags("-Wno-error=unused-local-typedefs")
    add_cxflags("-pedantic-errors")

    -- HAL 宏
    add_defines("USE_HAL_DRIVER", "STM32F407xx")

    -- 减小体积
    add_cxflags("-fdata-sections", "-ffunction-sections")
    add_ldflags("-Wl,--gc-sections")

    -- C++ 运行时限制
    add_cxxflags("-fno-exceptions", "-fno-rtti")
    add_cxxflags("-fno-threadsafe-statics")

    -- 链接基础库
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

    -- 仅 BootLoader 需要的宏和源码
    add_defines("BOOT_IMAGE")
    add_files("BootLoader/Src/*.c", "Component/ringbuffer.c")

    -- BootLoader 链接地址: 0x08000000, 64K
    add_ldflags("-T bsp/HAL/STM32F407XX_BOOT.ld")
    add_ldflags("-Wl,-Map=bootloader.map")

    add_bin_postbuild("bootloader")
end)

target("app", function()
    setup_common()

    -- 仅 APP 需要的宏
    add_defines("APP_IMAGE", "APP_START_ADDR=0x08010000")

    -- APP 业务源码（可选，有文件再加入）
    local app_c = os.files("App/Src/*.c")
    local app_cpp = os.files("App/Src/*.cpp")
    if #app_c > 0 then
        add_files("App/Src/*.c")
    end
    if #app_cpp > 0 then
        add_files("App/Src/*.cpp")
    end

    -- APP 链接地址: 0x08010000, 960K
    add_ldflags("-T bsp/HAL/STM32F407XX_APP.ld")
    add_ldflags("-Wl,-Map=app.map")

    add_bin_postbuild("app")
end)

target("factory", function()
    set_kind("phony")
    add_deps("bootloader", "app")

    -- 合并成最终单文件 factory.bin
    on_build(function()
        local mode = get_config("mode") or "release"
        local outdir = path.join("build", "cross", "arm", mode)

        local boot_bin = path.join(outdir, "bootloader.bin")
        local app_bin  = path.join(outdir, "app.bin")
        local out_bin  = path.join(outdir, "factory.bin")

        assert(os.isfile(boot_bin), "missing file: " .. boot_bin)
        assert(os.isfile(app_bin), "missing file: " .. app_bin)

        os.execv("powershell", {
            "-ExecutionPolicy", "Bypass",
            "-File", "script/merge_bins.ps1",
            "-BootBin", boot_bin,
            "-AppBin", app_bin,
            "-OutBin", out_bin,
            "-AppOffset", "0x10000"
        })

        cprint("${green}[factory]${clear} %s", out_bin)
    end)
end)
# XMake STM32 开发常用指令手册

> 适用于本项目：`stm32_xmake`  基于 xmake + arm-none-eabi-gcc + STM32F407

---

## 目录

- 环境配置
- 构建指令
- 模式切换
- 辅助文件生成
- 烧录与调试
- 信息查询
- 典型工作流
- xmake.lua 关键配置说明
- 常见问题

---

## 环境配置

### 首次配置（指定工具链路径）

```powershell
# 指定 arm-gnu-toolchain 的安装路径（不含 bin 目录）
xmake f --sdk="C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/13.2 Rel1"
```

### 完整配置（平台 + 工具链）

```powershell
xmake f -p cross -a arm --cross=arm-none-eabi- --sdk="<arm-gnu-toolchain-path>"
```

### 查看当前配置

```powershell
xmake f -v
```

---

## 构建指令

```powershell
# 构建项目（输出 .elf 和 .bin）
xmake

# 强制重新构建（全量编译）
xmake -r

# 清理所有构建产物
xmake clean

# 详细输出构建日志（查看完整编译命令）
xmake -v

# 输出诊断信息（排查构建错误时使用）
xmake -D

# 预览编译命令（不实际执行编译）
xmake -n
```

构建产物输出路径：

- Debug：`build/cross/arm/application.elf` / `.bin`
- Release：`build/cross/arm/release/application.elf` / `.bin`

---

## 模式切换

```powershell
# 切换为 Debug 模式（关闭优化 -O0，保留调试信息）
xmake f -m debug

# 切换为 Release 模式（最高优化 -O3，定义 NDEBUG）
xmake f -m release

# 切换模式后重新构建
xmake f -m debug ; xmake
```

| 模式      | 优化等级 | 调试信息       | NDEBUG |
| --------- | -------- | -------------- | ------ |
| `debug`   | `-O0`    | `-g -gdwarf-2` | 否     |
| `release` | `-O3`    | `-g -gdwarf-2` | 是     |

---

## 辅助文件生成

```powershell
# 生成 compile_commands.json（供 clangd / VSCode IntelliSense 使用）
# 本项目已在 xmake.lua 中配置自动更新，构建时自动生成于项目根目录
xmake project -k compile_commands -o .

# 生成 VSCode 工程文件（.vscode 配置）
xmake project -k vsxmake

# 生成 CMakeLists.txt
xmake project -k cmake

# 生成 Makefile
xmake project -k makefile
```

---

## 烧录与调试

### 使用 OpenOCD + ST-Link 烧录

```powershell
# 烧录 Release 版本 .bin 文件（STM32F4 Flash 起始地址 0x08000000）
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg `
  -c "program build/cross/arm/release/application.bin 0x08000000 verify reset exit"

# 烧录 Debug 版本 .bin 文件
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg `
  -c "program build/cross/arm/application.bin 0x08000000 verify reset exit"

# 烧录 .elf 文件（自动识别地址）
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg `
  -c "program build/cross/arm/release/application.elf verify reset exit"
```

### 使用 OpenOCD + GDB 调试

```powershell
# 终端1：启动 OpenOCD GDB Server
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg

# 终端2：连接 GDB
arm-none-eabi-gdb build/cross/arm/application.elf
# GDB 内执行：
# (gdb) target remote localhost:3333
# (gdb) monitor reset halt
# (gdb) load
# (gdb) continue
```

### 使用 STM32CubeProgrammer CLI 烧录

```powershell
# ST-Link 烧录
STM32_Programmer_CLI -c port=SWD -d build/cross/arm/release/application.bin 0x08000000 -v -rst

# 擦除芯片
STM32_Programmer_CLI -c port=SWD -e all
```

### 使用 JLink 烧录

```powershell
# 通过 JFlash 烧录 .elf
JFlash -openprj xxx.jflash -open build/cross/arm/release/application.elf -auto -exit

# 通过 JLink Commander 烧录 .bin
JLink -device STM32F407VG -if SWD -speed 4000 -autoconnect 1 `
  -CommanderScript flash.jlink
```

---

## 信息查询

```powershell
# 查看所有 target
xmake show

# 查看 xmake 版本
xmake --version

# 查看支持的工具链
xmake show -l toolchains

# 查看支持的平台列表
xmake f --list

# 查看 gnu-rm 工具链信息
xmake show -l toolchains | findstr gnu

# 查看编译产物大小（arm-none-eabi-size）
arm-none-eabi-size build/cross/arm/release/application.elf

# 反汇编 .elf 文件
arm-none-eabi-objdump -d build/cross/arm/release/application.elf | more

# 查看链接 map 文件（内存占用分析）
cat application.map | more
```

---

## 典型工作流

### 首次克隆后初始化

```powershell
# 1. 配置工具链（只需执行一次）
xmake f --sdk="<arm-gnu-toolchain-path>"

# 2. 构建项目
xmake

# 3. compile_commands.json 自动生成于根目录，VSCode clangd 即可工作
```

### 日常开发循环

```powershell
# 编辑代码后，增量编译
xmake

# 烧录到板子
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg `
  -c "program build/cross/arm/application.bin 0x08000000 verify reset exit"
```

### CubeMX 重新生成代码后

```powershell
# CubeMX 重新生成 bsp/HAL 中的代码后，直接重新构建即可
# xmake.lua 中的 on_load 会自动重新读取 HAL Makefile
xmake -r
```

### 发布构建

```powershell
# 切换到 Release 模式并构建
xmake f -m release
xmake

# 查看内存占用（构建时自动打印，也可手动查看）
arm-none-eabi-size build/cross/arm/release/application.elf
```

---

## xmake.lua 关键配置说明

| 配置项                                            | 说明                                        |
| ------------------------------------------------- | ------------------------------------------- |
| `set_config("plat", "cross")`                     | 固定为交叉编译平台                          |
| `set_config("cross", "arm-none-eabi-")`           | 使用 ARM GCC 工具链前缀                     |
| `set_toolchains("gnu-rm")`                        | 使用 gnu-rm 工具链定义                      |
| `-mcpu=cortex-m4`                                 | 目标 CPU：Cortex-M4（STM32F407）            |
| `-mfpu=fpv4-sp-d16 -mfloat-abi=hard`              | 硬件浮点 FPU                                |
| `on_load("script.read_hal_makefile")`             | 自动从 CubeMX 的 Makefile 读取 HAL 源文件   |
| `add_rules("plugin.compile_commands.autoupdate")` | 构建时自动更新 compile_commands.json        |
| `after_build`                                     | 构建后自动生成 .bin 并打印 FLASH/RAM 占用   |

---

## 常见问题

### Q: 构建时提示找不到 arm-none-eabi-gcc

```powershell
# 检查工具链路径是否正确
xmake f --sdk="<正确的工具链根目录>"
xmake -r
```

### Q: clangd 报头文件找不到

```powershell
# 重新生成 compile_commands.json
xmake project -k compile_commands -o .
# 或者直接重新构建（xmake.lua 中已配置自动更新）
xmake -r
```

### Q: 查看详细报错信息

```powershell
xmake -v -D
```

---

*文档生成时间：2026年3月*
*适用芯片：STM32F407xx | 工具链：arm-none-eabi-gcc | 构建工具：xmake v2.8.5+*

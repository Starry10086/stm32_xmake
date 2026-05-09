#include "bl_core.h"
#include "bl_flash_if.h"
#include "bl_partition.h"
#include "bl_param.h"
#include "debug_uart.h"
#include "stm32f4xx.h"  // Provide NVIC_SystemReset and other CMSIS/HAL definitions
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static uint8_t s_param_page_cache[BL_PARAM_PAGE_SIZE];

static uint32_t bl_crc32_calc(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    uint32_t j;

    for (i = 0UL; i < len; i++) {
        crc ^= data[i];
        for (j = 0UL; j < 8UL; j++) {
            if ((crc & 1UL) != 0UL) {
                crc = (crc >> 1UL) ^ 0xEDB88320UL;
            } else {
                crc >>= 1UL;
            }
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

static uint32_t bl_param_calc_crc(const bl_param_t *param)
{
    return bl_crc32_calc((const uint8_t *)param, (uint32_t)offsetof(bl_param_t, param_crc32));
}

static uint32_t bl_log_calc_crc(const bl_log_entry_t *entry)
{
    return bl_crc32_calc((const uint8_t *)entry, (uint32_t)offsetof(bl_log_entry_t, crc32));
}

static void bl_param_set_default(bl_param_t *param)
{
    memset(param, 0, sizeof(*param));
    param->magic = BL_PARAM_MAGIC;
    param->version = BL_PARAM_VERSION;
    param->update_flag = BL_UPDATE_FLAG_IDLE;
    param->app1_addr = BL_APP1_START_ADDR;
    param->app2_addr = BL_APP2_START_ADDR;
    param->tail_magic = BL_PARAM_TAIL_MAGIC;
    param->param_crc32 = bl_param_calc_crc(param);
}

static void bl_param_dump(const char *tag, const bl_param_t *param)
{
    if (param == NULL) {
        my_printf("BL: %s param is NULL\r\n", tag);
        return;
    }

    my_printf(
        "BL: %s magic=0x%08lX ver=0x%08lX flag=0x%08lX size=%lu crc=0x%08lX app1=0x%08lX app2=0x%08lX pc=0x%08lX tail=0x%08lX\r\n",
        tag,
        param->magic,
        param->version,
        param->update_flag,
        param->app_size,
        param->app_crc32,
        param->app1_addr,
        param->app2_addr,
        param->param_crc32,
        param->tail_magic
    );
}

static bool bl_param_is_valid_ex(const bl_param_t *param, const char *tag)
{
    uint32_t calc_crc;

    if (param == NULL) {
        my_printf("BL: %s invalid: null param\r\n", tag);
        return false;
    }

    if (param->magic != BL_PARAM_MAGIC) {
        my_printf("BL: %s invalid: magic expect=0x%08lX got=0x%08lX\r\n",
                  tag, (uint32_t)BL_PARAM_MAGIC, param->magic);
        return false;
    }
    if (param->tail_magic != BL_PARAM_TAIL_MAGIC) {
        my_printf("BL: %s invalid: tail expect=0x%08lX got=0x%08lX\r\n",
                  tag, (uint32_t)BL_PARAM_TAIL_MAGIC, param->tail_magic);
        return false;
    }
    if (param->version != BL_PARAM_VERSION) {
        my_printf("BL: %s invalid: version expect=0x%08lX got=0x%08lX\r\n",
                  tag, (uint32_t)BL_PARAM_VERSION, param->version);
        return false;
    }
    if ((param->app1_addr != BL_APP1_START_ADDR) || (param->app2_addr != BL_APP2_START_ADDR)) {
        my_printf("BL: %s invalid: addr expect app1=0x%08lX app2=0x%08lX got app1=0x%08lX app2=0x%08lX\r\n",
                  tag,
                  (uint32_t)BL_APP1_START_ADDR,
                  (uint32_t)BL_APP2_START_ADDR,
                  param->app1_addr,
                  param->app2_addr);
        return false;
    }
    if ((param->app_size > BL_APP1_SIZE) || (param->app_size > BL_APP2_SIZE)) {
        my_printf("BL: %s invalid: app_size=%lu exceeds app1=%lu or app2=%lu\r\n",
                  tag, param->app_size, (uint32_t)BL_APP1_SIZE, (uint32_t)BL_APP2_SIZE);
        return false;
    }

    calc_crc = bl_param_calc_crc(param);
    if (calc_crc != param->param_crc32) {
        my_printf("BL: %s invalid: param_crc expect=0x%08lX calc=0x%08lX\r\n",
                  tag, param->param_crc32, calc_crc);
        return false;
    }

    return true;
}

static bool bl_param_is_valid(const bl_param_t *param)
{
    return bl_param_is_valid_ex(param, "param");
}

static bool bl_log_is_valid(const bl_log_entry_t *entry)
{
    if (entry == NULL) {
        return false;
    }

    if (entry->magic != BL_LOG_MAGIC) {
        return false;
    }

    return (bl_log_calc_crc(entry) == entry->crc32);
}

static void bl_log_prepare(bl_log_entry_t *entry, uint32_t seq, uint32_t event_id, uint32_t result,
                           uint32_t value0, uint32_t value1, uint32_t value2)
{
    memset(entry, 0, sizeof(*entry));
    entry->magic = BL_LOG_MAGIC;
    entry->seq = seq;
    entry->event_id = event_id;
    entry->result = result;
    entry->value0 = value0;
    entry->value1 = value1;
    entry->value2 = value2;
    entry->crc32 = bl_log_calc_crc(entry);
}

static uint32_t bl_next_log_seq(const bl_param_t *param)
{
    return param->update_counter + param->fail_counter + param->log_write_index + 1UL;
}

static bool bl_commit_param_page(const bl_param_t *param, const bl_log_entry_t *log_entry, bool append_log)
{
    bl_param_t main_copy;
    bl_param_t backup_copy;
    uint32_t log_index;
    uint32_t log_offset;

    memcpy(s_param_page_cache, (const void *)BL_PARAM_PAGE_ADDR, BL_PARAM_PAGE_SIZE);

    memcpy(&main_copy, param, sizeof(main_copy));
    main_copy.param_crc32 = bl_param_calc_crc(&main_copy);
    memcpy(&backup_copy, &main_copy, sizeof(backup_copy));

    memcpy(&s_param_page_cache[BL_PARAM_MAIN_ADDR - BL_PARAM_PAGE_ADDR], &main_copy, sizeof(main_copy));
    memcpy(&s_param_page_cache[BL_PARAM_BACKUP_ADDR - BL_PARAM_PAGE_ADDR], &backup_copy, sizeof(backup_copy));

    if (append_log && (log_entry != NULL)) {
        log_index = main_copy.log_write_index % BL_LOG_ENTRY_COUNT;
        log_offset = (BL_LOG_ADDR - BL_PARAM_PAGE_ADDR) + (log_index * BL_LOG_ENTRY_SIZE);
        memcpy(&s_param_page_cache[log_offset], log_entry, sizeof(*log_entry));
    }

    return bl_flash_program_param(s_param_page_cache, BL_PARAM_PAGE_SIZE);
}

bool bl_commit_param(bl_param_t *param)
{
    if (param == NULL) {
        return false;
    }

    if (!bl_param_is_valid(param)) {
        bl_param_t repaired;
        bl_param_set_default(&repaired);
        repaired.update_flag = param->update_flag;
        repaired.app_size = param->app_size;
        repaired.app_crc32 = param->app_crc32;
        repaired.update_counter = param->update_counter;
        repaired.fail_counter = param->fail_counter;
        repaired.last_error = param->last_error;
        repaired.log_write_index = param->log_write_index;
        *param = repaired;
    }

    param->magic = BL_PARAM_MAGIC;
    param->version = BL_PARAM_VERSION;
    param->app1_addr = BL_APP1_START_ADDR;
    param->app2_addr = BL_APP2_START_ADDR;
    param->tail_magic = BL_PARAM_TAIL_MAGIC;
    param->param_crc32 = bl_param_calc_crc(param);

    return bl_commit_param_page(param, NULL, false);
}

static uint32_t bl_crc32_flash(uint32_t start_addr, uint32_t size)
{
    return bl_crc32_calc((const uint8_t *)start_addr, size);
}

static bool bl_copy_app2_to_app1(uint32_t app_size)
{
    uint8_t buffer[BL_COPY_CHUNK_SIZE];
    uint32_t copied = 0UL;
    uint32_t chunk_size;

    if ((app_size == 0UL) || (app_size > BL_APP1_SIZE) || (app_size > BL_APP2_SIZE)) {
        return false;
    }

    if (!bl_flash_erase(BL_APP1_START_ADDR, app_size)) {
        return false;
    }

    while (copied < app_size) {
        uint32_t left = app_size - copied;
        const uint8_t *src = (const uint8_t *)(BL_APP2_START_ADDR + copied);

        chunk_size = (left > BL_COPY_CHUNK_SIZE) ? BL_COPY_CHUNK_SIZE : left;
        memcpy(buffer, src, chunk_size);

        if (!bl_flash_program(BL_APP1_START_ADDR + copied, buffer, chunk_size)) {
            return false;
        }

        copied += chunk_size;
    }

    return true;
}

static void bl_record_and_commit(bl_param_t *param, uint32_t event_id, uint32_t result,
                                 uint32_t value0, uint32_t value1, uint32_t value2)
{
    bl_log_entry_t log_entry;

    bl_log_prepare(&log_entry, bl_next_log_seq(param), event_id, result, value0, value1, value2);
    param->log_write_index = (param->log_write_index + 1UL) % BL_LOG_ENTRY_COUNT;
    (void)bl_commit_param_page(param, &log_entry, true);
}

void bootloader_run(void)
{
    // 定义参数结构体。Bootloader采用"双备份"机制存储参数防止掉电丢失
    bl_param_t main_param;    // 主区参数
    bl_param_t backup_param;  // 备份区参数
    bl_param_t working_param; // 经过校验后，实际决定在这个流程中使用的"有效参数"
    
    // 标记主区和备份区参数的校验结果（是否损坏）
    bool main_valid;
    bool backup_valid;
    
    // 标记是否需要"参数修复"（比如主区损坏，需要用备份区恢复它）
    bool need_repair = false; 
    
    // 用于保存APP固件的CRC校验值，做完整性比对
    uint32_t app_crc;

    // 从Flash中直接把主区和备份区的数据拷贝到RAM里的结构体中
    memcpy(&main_param, (const void *)BL_PARAM_MAIN_ADDR, sizeof(main_param));
    memcpy(&backup_param, (const void *)BL_PARAM_BACKUP_ADDR, sizeof(backup_param));

    // 调用校验函数，检查参数魔术字(Magic)、版本号、校验和(CRC)是否正确
    main_valid = bl_param_is_valid_ex(&main_param, "main");
    backup_valid = bl_param_is_valid_ex(&backup_param, "backup");
    my_printf("BL: main_valid=%u backup_valid=%u\r\n", main_valid ? 1U : 0U, backup_valid ? 1U : 0U);
    if (!main_valid) {
        bl_param_dump("main", &main_param);
    }
    if (!backup_valid) {
        bl_param_dump("backup", &backup_param);
    }

    // 如果主区和备区的数据都是完好的
    if (main_valid && backup_valid) {
        // 这几行定义本意是想读取日志(log)，但代码里似乎写了一半没真正用上
        const bl_log_entry_t *main_log;
        const bl_log_entry_t *backup_log;
        bool main_log_valid;
        bool backup_log_valid;

        main_log = (const bl_log_entry_t *)BL_LOG_ADDR;
        backup_log = (const bl_log_entry_t *)BL_LOG_ADDR;
        main_log_valid = bl_log_is_valid(main_log);
        backup_log_valid = bl_log_is_valid(backup_log);
        (void)main_log_valid; // 强制转换消除编译器"变量未使用"的警告
        (void)backup_log_valid;

        // 比较两者的更新计数器，哪个数字大，说明哪个数据更新。以新的为准。
        if (main_param.update_counter >= backup_param.update_counter) {
            working_param = main_param; // 主区更新，用主区
        } else {
            working_param = backup_param; // 备区更新，用备区
            need_repair = true; // 既然备区比较新，说明主区的数据落后了，需要触发修复覆盖主区
        }
    } 
    // 如果只有主区完好，备份区坏了
    else if (main_valid) {
        working_param = main_param; // 当然只能用主区参数
        need_repair = true;         // 备区坏了，需要触发修复
    } 
    // 如果只有备份区完好，主区坏了
    else if (backup_valid) {
        working_param = backup_param; // 用备区参数
        need_repair = true;           // 主区坏了，需要触发修复
    } 
    // 最惨的情况：主区和备份区全坏了（或者是新出厂的单片机，里面完全没数据）
    else {
        bl_param_set_default(&working_param); // 强制初始化一套默认的出厂配置
        need_repair = true;                   // 必须把默认配置重新写回Flash
        working_param.last_error = BL_ERR_PARAM_INVALID; // 记录一个"参数无效"的错误代码
    }

    // 如果前面的逻辑判断需要修复（比如出现数据不一致、损坏或空白）
    if (need_repair) {
        my_printf("BL: repairing parameter page, last_error=%lu\r\n", working_param.last_error);
        // 这一步是将 working_param 的内容重新写入 Flash 中保存，保证主备区重新一致
        // 同时向日志区写入一条 BL_LOG_EVENT_PARAM_RECOVER（参数恢复） 的记录
        bl_record_and_commit(&working_param, BL_LOG_EVENT_PARAM_RECOVER, 1UL,
                             main_valid ? 1UL : 0UL, backup_valid ? 1UL : 0UL, working_param.last_error);
    }

    // 如果当前参数标志为：有固件等待更新 (PENDING：待处理)
    if (working_param.update_flag == BL_UPDATE_FLAG_PENDING) {
        my_printf("BL: pending update size=%lu crc=0x%08lX\r\n",
                  working_param.app_size, working_param.app_crc32);
        
        /* --- 步骤 1：初步检查新固件的合法性 --- */
        // 如果文件大小是0、或者大小超出了APP2的最大容量、或者APP2起始地址里的中断向量格式不合法
        if ((working_param.app_size == 0UL) || (working_param.app_size > BL_APP2_SIZE) ||
            !bl_is_app_vector_valid(BL_APP2_START_ADDR)) {
            
            working_param.update_flag = BL_UPDATE_FLAG_FAILED; // 标记更新状态为：失败
            working_param.fail_counter++;                      // 失败计数器+1
            working_param.last_error = BL_ERR_APP2_INVALID;    // 错误原因：APP2区域数据无效
            // 把失败信息写进日志并提交保存
            bl_record_and_commit(&working_param, BL_LOG_EVENT_UPDATE_FAIL, 0UL,
                                 BL_ERR_APP2_INVALID, working_param.app_size, 0UL);
            NVIC_SystemReset(); // 执行完毕，直接硬重启单片机（重启后因为不再是PENDING，会跳过这块代码直接去引导APP1）
        }

        /* --- 步骤 2：严格校验新固件的数据完整性 --- */
        // 根据参数区记录的固件大小，计算 Flash 中 APP2 区数据的实际 CRC 校验值
        app_crc = bl_crc32_flash(BL_APP2_START_ADDR, working_param.app_size);
        my_printf("BL: APP2 crc calc=0x%08lX expect=0x%08lX\r\n", app_crc, working_param.app_crc32);
        // 如果实际算出来的 CRC 和 云端/上位机 下发的不一样，说明下载过程中数据损坏了
        if (app_crc != working_param.app_crc32) {
            working_param.update_flag = BL_UPDATE_FLAG_FAILED; // 失败
            working_param.fail_counter++;
            working_param.last_error = BL_ERR_APP2_INVALID;
            // 写入日志并提交
            bl_record_and_commit(&working_param, BL_LOG_EVENT_UPDATE_FAIL, 0UL,
                                 BL_ERR_APP2_INVALID, working_param.app_crc32, app_crc);
            NVIC_SystemReset(); // 重启
        }

        /* --- 步骤 3：把新固件从 APP2(下载区) 搬运覆盖到 APP1(运行区) --- */
        // 如果搬运失败（比如 Flash 擦除失败或写入失败）
        if (!bl_copy_app2_to_app1(working_param.app_size)) {
            my_printf("BL: APP2 -> APP1 copy failed\r\n");
            working_param.update_flag = BL_UPDATE_FLAG_FAILED;
            working_param.fail_counter++;
            working_param.last_error = BL_ERR_COPY_FAILED; // 错误原因：拷贝失败
            bl_record_and_commit(&working_param, BL_LOG_EVENT_UPDATE_FAIL, 0UL,
                                 BL_ERR_COPY_FAILED, 0UL, 0UL);
            NVIC_SystemReset(); // 重启
        }

        /* --- 步骤 4：搬运完成后，再次校验 APP1 里的数据，确保烧写准确无误 --- */
        // 计算目标区(APP1)里的数据 CRC
        app_crc = bl_crc32_flash(BL_APP1_START_ADDR, working_param.app_size);
        my_printf("BL: APP1 crc calc=0x%08lX expect=0x%08lX\r\n", app_crc, working_param.app_crc32);
        // 如果和预期的 CRC 仍不一致，说明 Flash 写坏了
        if (app_crc != working_param.app_crc32) {
            working_param.update_flag = BL_UPDATE_FLAG_FAILED;
            working_param.fail_counter++;
            working_param.last_error = BL_ERR_COPY_FAILED; // 仍然算作拷贝失败
            bl_record_and_commit(&working_param, BL_LOG_EVENT_UPDATE_FAIL, 0UL,
                                 BL_ERR_COPY_FAILED, working_param.app_crc32, app_crc);
            NVIC_SystemReset();
        }

        /* --- 步骤 5：更新大获成功！ --- */
        working_param.update_flag = BL_UPDATE_FLAG_IDLE; // 将标志位改回空闲以结束更新状态
        working_param.update_counter++;                  // 成功升级次数+1
        working_param.last_error = BL_ERR_NONE;          // 清除错误信息
        // 把成功的消息记入日志并同步到 Flash
        bl_record_and_commit(&working_param, BL_LOG_EVENT_UPDATE_OK, 1UL,
                             working_param.app_size, working_param.app_crc32, app_crc);
        NVIC_SystemReset(); // 重启，重启后系统就会跳过上面的所有代码，正式进入新 APP！
    }

    // 检查 APP1(主运行区) 开头的中断向量是否合法（判断栈指针和ResetHandler地址是否在正常范围）
    if (bl_is_app_vector_valid(BL_APP1_START_ADDR)) {
        my_printf("BL: jumping to APP1 @ 0x%08lX\r\n", (uint32_t)BL_APP1_START_ADDR);
        bl_jump_to_app(BL_APP1_START_ADDR); // 把 CPU 的控制权交给主应用 APP1
        // 注意：正常的 bl_jump_to_app 是一去不复返的，所以代码不会运行到下面
    }

    /* --- 如果运行到这里，说明非常悲剧：没有固件等我们更新，且原本的 APP1 也坏了，系统没东西可启动了 --- */
    working_param.last_error = BL_ERR_APP1_INVALID;
    my_printf("BL: APP1 invalid, stay in bootloader\r\n");
    // 记录跳转失败的黑历史
    bl_record_and_commit(&working_param, BL_LOG_EVENT_JUMP_FAIL, 0UL,
                         BL_ERR_APP1_INVALID, BL_APP1_START_ADDR, 0UL);

    // 只能死循环挂起（此处常被看门狗等看护复位，但这套代码就是死等）
    while (1) {
    }
}

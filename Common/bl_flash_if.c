#include "bl_flash_if.h"
#include "bl_partition.h"
#include "main.h"
#include <string.h>

typedef struct {
    uint32_t start_addr;
    uint32_t end_addr;
    uint32_t sector;
} flash_sector_info_t;

static const flash_sector_info_t s_sector_map[] = {
    {0x08000000, 0x08003FFF, FLASH_SECTOR_0},
    {0x08004000, 0x08007FFF, FLASH_SECTOR_1},
    {0x08008000, 0x0800BFFF, FLASH_SECTOR_2},
    {0x0800C000, 0x0800FFFF, FLASH_SECTOR_3},
    {0x08010000, 0x0801FFFF, FLASH_SECTOR_4},
    {0x08020000, 0x0803FFFF, FLASH_SECTOR_5},
    {0x08040000, 0x0805FFFF, FLASH_SECTOR_6},
    {0x08060000, 0x0807FFFF, FLASH_SECTOR_7},
    {0x08080000, 0x0809FFFF, FLASH_SECTOR_8},
    {0x080A0000, 0x080BFFFF, FLASH_SECTOR_9},
    {0x080C0000, 0x080DFFFF, FLASH_SECTOR_10},
    {0x080E0000, 0x080FFFFF, FLASH_SECTOR_11},
};

static int32_t bl_flash_get_sector(uint32_t addr)
{
    uint32_t i;
    for (i = 0; i < (sizeof(s_sector_map) / sizeof(s_sector_map[0])); i++) {
        if ((addr >= s_sector_map[i].start_addr) && (addr <= s_sector_map[i].end_addr)) {
            return (int32_t)s_sector_map[i].sector;
        }
    }
    return -1;
}

bool bl_flash_erase(uint32_t addr, uint32_t size)
{
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = 0U;
    uint32_t end_addr;
    int32_t first_sector;
    int32_t last_sector;
    HAL_StatusTypeDef status;

    if ((size == 0U) || (addr < BL_FLASH_BASE_ADDR)) {
        return false;
    }

    end_addr = addr + size - 1U;
    if (end_addr > BL_FLASH_END_ADDR) {
        return false;
    }

    first_sector = bl_flash_get_sector(addr);
    last_sector = bl_flash_get_sector(end_addr);
    if ((first_sector < 0) || (last_sector < 0) || (last_sector < first_sector)) {
        return false;
    }

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase_init.Sector = (uint32_t)first_sector;
    erase_init.NbSectors = (uint32_t)(last_sector - first_sector + 1);

    __disable_irq();
    status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
    __enable_irq();

    HAL_FLASH_Lock();
    return (status == HAL_OK);
}

bool bl_flash_program(uint32_t addr, const uint8_t *data, size_t size)
{
    uint32_t word;
    size_t i;
    size_t remain;
    uint8_t tmp[4];

    if ((data == NULL) || (size == 0U)) {
        return false;
    }
    if ((addr < BL_FLASH_BASE_ADDR) || ((addr + size - 1U) > BL_FLASH_END_ADDR)) {
        return false;
    }

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    i = 0U;
    while ((size - i) >= 4U) {
        memcpy(&word, &data[i], 4U);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word) != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
        if (*(__IO uint32_t *)addr != word) {
            HAL_FLASH_Lock();
            return false;
        }
        addr += 4U;
        i += 4U;
    }

    remain = size - i;
    if (remain > 0U) {
        tmp[0] = 0xFFU;
        tmp[1] = 0xFFU;
        tmp[2] = 0xFFU;
        tmp[3] = 0xFFU;
        memcpy(tmp, &data[i], remain);
        memcpy(&word, tmp, 4U);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word) != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
        if (*(__IO uint32_t *)addr != word) {
            HAL_FLASH_Lock();
            return false;
        }
    }

    HAL_FLASH_Lock();
    return true;
}

bool bl_flash_program_param(const void *param, size_t size)
{
    if ((param == NULL) || (size == 0U) || (size > BL_PARAM_SIZE)) {
        return false;
    }

    if (!bl_flash_erase(BL_PARAM_START_ADDR, BL_PARAM_SIZE)) {
        return false;
    }

    return bl_flash_program(BL_PARAM_START_ADDR, (const uint8_t *)param, size);
}

bool bl_is_app_vector_valid(uint32_t app_base)
{
    uint32_t msp;
    uint32_t reset;

    msp = *(__IO uint32_t *)app_base;
    reset = *(__IO uint32_t *)(app_base + 4U);

    if ((msp < BL_SRAM_START_ADDR) || (msp > BL_SRAM_TOP_ADDR)) {
        return false;
    }
    if ((reset < BL_FLASH_BASE_ADDR) || (reset > BL_FLASH_END_ADDR)) {
        return false;
    }

    return true;
}

__attribute__((noreturn)) 
static void bl_jump_raw(uint32_t msp, uint32_t reset)
{
    typedef void (*pFunction)(void);
    pFunction app_entry;
    
    app_entry = (void (*)(void))reset;
    __set_MSP(msp);
    __enable_irq();
    app_entry();

    while (1) {
    }
}

void bl_jump_to_app(uint32_t app_base)
{
    uint32_t msp;
    uint32_t reset;
    uint32_t i;

    msp = *(__IO uint32_t *)app_base;
    reset = *(__IO uint32_t *)(app_base + 4U);

    __disable_irq();
    HAL_DeInit();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    for (i = 0U; i < 8U; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    SCB->VTOR = app_base;
    __DSB();
    __ISB();

    bl_jump_raw(msp, reset);
}

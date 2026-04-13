#include "boot_flash.h"
#include "boot_config.h"

typedef struct{
    uint32_t start_addr;
    uint32_t end_addr;
    uint32_t sector_cnt;
} FlashSectorInfo_t;


// STM32F40X 12个扇区
static FlashSectorInfo_t flash_sector_info[12] = {
    {0x08000000, 0x08003FFF, 0},    // 16KB
    {0x08004000, 0x08007FFF, 1},    // 16KB
    {0x08008000, 0x0800BFFF, 2},    // 16KB
    {0x0800C000, 0x0800FFFF, 3},    // 16KB
    {0x08010000, 0x0801FFFF, 4},    // 64KB
    {0x08020000, 0x0803FFFF, 5},    // 128KB
    {0x08040000, 0x0805FFFF, 6},    // 128KB
    {0x08060000, 0x0807FFFF, 7},    // 128KB
    {0x08080000, 0x0809FFFF, 8},    // 128KB
    {0x080A0000, 0x080BFFFF, 9},    // 128KB
    {0x080C0000, 0x080DFFFF, 10},   // 128KB
    {0x080E0000, 0x080FFFFF, 11},   // 128KB
};
static uint32_t boot_flash_get_sector(uint32_t addr){
    for (size_t i = 0; i < sizeof(flash_sector_info) / sizeof(flash_sector_info[0]); i++){
        if (addr >= flash_sector_info[i].start_addr && addr <= flash_sector_info[i].end_addr){
            return flash_sector_info[i].sector_cnt;
        }
    }
    return -1;
}
int8_t Flash_Erase(){
    FLASH_EraseInitTypeDef erase_init;
    uint32_t SectorError;

    HAL_FLASH_Unlock();
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Banks = FLASH_BANK_1;
    erase_init.Sector = boot_flash_get_sector(APP_START_ADDR);
    erase_init.NbSectors = APP_SECTOR_CNT;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    __disable_irq();
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase_init, &SectorError);
    __enable_irq();

    return (status == HAL_OK) ? 0 : -1;
}
int8_t Flash_WriteData(uint32_t addr, const uint8_t *data, uint32_t len){
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR);

    for(uint16_t i = 0; i < len; i += 4){
        uint32_t data32 = ((uint32_t)data[i]         | (uint32_t)data[i+1] << 8 | 
                           (uint32_t)data[i+2] << 16 | (uint32_t)data[i+3] << 24);

        if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, data32) != HAL_OK){
            HAL_FLASH_Lock();
            return -1;
        }
        if(*(__IO uint32_t*)addr != data32){
            HAL_FLASH_Lock();
            return -1;
        }
        addr += 4;
    }
    HAL_FLASH_Lock();
    return 0;
}

__attribute__((noreturn))
static void boot_jump_app(uint32_t app_msp, uint32_t app_addr){
    typedef void (*pFunction)(void);
    pFunction JumpToApplication;

    JumpToApplication = (pFunction)app_addr;
    __set_MSP(app_msp);
    JumpToApplication();
}

void Jump_App(){
    uint32_t app_msp = *(__IO uint32_t*)APP_START_ADDR;
    if(app_msp >= SRAM_START_ADDR && app_msp <= SRAM_END_ADDR){
        uint32_t JumpAddr = *(__IO uint32_t*) (APP_START_ADDR + 4);

        __disable_irq();
        HAL_DeInit();
        SCB->VTOR = APP_START_ADDR;
        __DSB();
        __ISB();
        boot_jump_app(app_msp, JumpAddr);
    }
}

#ifndef __BOOT_FLASH_H__
#define __BOOT_FLASH_H__

#include "main.h"


int8_t Flash_Erase();
int8_t Flash_WriteData(uint32_t addr, const uint8_t *data, uint32_t len);
void Jump_App();

#endif
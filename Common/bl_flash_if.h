#ifndef BL_FLASH_IF_H
#define BL_FLASH_IF_H


#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

bool bl_flash_erase(uint32_t addr, uint32_t size);
bool bl_flash_program(uint32_t addr, const uint8_t* data, size_t size);
bool bl_flash_program_param(const void* param, size_t size);
void bl_jump_to_app(uint32_t app_addr);
bool bl_is_app_vector_valid(uint32_t app_base);

#endif
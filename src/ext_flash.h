/*
 * External Flash Access Routines
 *
 * Written by: Andrew Kilpatrick
 * Copyright 2016: Kilpatrick Audio
 *
 * This file is part of CARBON.
 *
 * CARBON is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CARBON is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CARBON.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef EXT_FLASH_H
#define EXT_FLASH_H

#include <inttypes.h>
#include "spi_flash.h"

// settings
#define EXT_FLASH_PAGE_SIZE (SPI_FLASH_PAGE_SIZE)
#define EXT_FLASH_SECTOR_SIZE (SPI_FLASH_SECTOR_SIZE)
#define EXT_FLASH_MEMORY_SIZE (SPI_FLASH_MEMORY_SIZE)

// top-level load / save state
#define EXT_FLASH_STATE_IDLE 0
#define EXT_FLASH_STATE_LOAD 1
#define EXT_FLASH_STATE_LOAD_ERROR 2
#define EXT_FLASH_STATE_LOAD_DONE 3
#define EXT_FLASH_STATE_SAVE 4
#define EXT_FLASH_STATE_SAVE_NOERASE 5
#define EXT_FLASH_STATE_SAVE_ERROR 6
#define EXT_FLASH_STATE_SAVE_DONE 7
#define EXT_FLASH_STATE_BUSY 8  // operation belongs to another caller

// init external flash
void ext_flash_init(void);

// run the external flash timer task to handle data transfer
void ext_flash_timer_task(void);

// Query/consume completion only for the original I/O buffer (caller identity).
// Non-owners get BUSY and cannot clear DONE/ERROR.
int ext_flash_get_state_for(uint8_t *owner);

// Inspect without consuming another caller's completion/error state.
int ext_flash_peek_state(void);

// start load from external flash - return -1 in case of error
int ext_flash_load(int32_t addr, int len, uint8_t *loadp);

// start save to external flash - return -1 in case of error
int ext_flash_save(int32_t addr, int len, uint8_t *savep);

// start save to external flash without performing a sector erase first
// returns -1 in case of error
int ext_flash_save_noerase(int32_t addr, int len, uint8_t *savep);

// get the size of the external flash
int32_t ext_flash_get_mem_size(void);

#endif

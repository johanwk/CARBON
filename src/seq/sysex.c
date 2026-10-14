/*
 * CARBON Sequencer SYSEX Handler
 *
 * Written by: Andrew Kilpatrick
 * Copyright 2015: Kilpatrick Audio
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
 * SYSEX Command Protocol:
 * - error response             - 0x01  <- from device
 *   - 0xf0 - sysex start
 *   - 0x00 - MMA ID
 *   - 0x01 - MMA ID
 *   - 0x72 - MMA ID
 *   - 0x49 - device type
 *   - 0x01 - error response
 *   - 0xaa - original cmd
 *   - 0xbb - error code
 *   - 0xf7 - sysex end
 *
 * - set config store data val  - 0x6d  -> to device (no response)
 *   - 0xf0 - sysex start
 *   - 0x00 - MMA ID
 *   - 0x01 - MMA ID
 *   - 0x72 - MMA ID
 *   - 0x49 - device type
 *   - 0x6d - set config store
 *   - 0xaa - addr - address stride is 4 bytes
 *   - 0x0b - data bits 31-28
 *   - 0x0c - data bits 27-24
 *   - 0x0d - data bits 23-20
 *   - 0x0e - data bits 19-16
 *   - 0x0f - data bits 15-12
 *   - 0x0g - data bits 11-8
 *   - 0x0h - data bits 7-4
 *   - 0x0i - data bits 3-0
 *   - 0xf7 - sysex end
 *
 * - set LCD display type       - 0x6e  -> to device
 *   - 0xf0 - sysex start
 *   - 0x00 - MMA ID
 *   - 0x01 - MMA ID
 *   - 0x72 - MMA ID
 *   - 0x49 - device type
 *   - 0x6e - set LCD type
 *   - 0xaa - LCD type number
 *   - 0xf7 - sysex end
 *
 * - wipe config store          - 0x6f  -> to device
 *   - 0xf0 - sysex start
 *   - 0x00 - MMA ID
 *   - 0x01 - MMA ID
 *   - 0x72 - MMA ID
 *   - 0x49 - device type
 *   - 0x6f - wipe config store
 *   - 0xf7 - sysex end
 *
 * - read ext flash data        - 0x70  -> to device
 *   - 0xf0 - sysex start
 *   - 0x00 - MMA ID
 *   - 0x01 - MMA ID
 *   - 0x72 - MMA ID
 *   - 0x49 - device type
 *   - 0x70 - read ext flash data
 *   - 0x0c - address bits 23-20
 *   - 0x0d - address bits 19-16
 *   - 0x0e - address bits 15-12
 *   - 0x0f - address bits 11-8
 *   - 0x0g - address bits 7-4
 *   - 0x0h - address bits 3-0
 *   - 0xii - read length (1-64 bytes)
 *   - 0xf7 - sysex end
 *
 * - readback ext flash data    - 0x71  <- from device
 *   - 0xf0 - sysex start
 *   - 0x00 - MMA ID
 *   - 0x01 - MMA ID
 *   - 0x72 - MMA ID
 *   - 0x49 - device type
 *   - 0x71 - readback ext flash data
 *   - 0x0c - address bits 23-20
 *   - 0x0d - address bits 19-16
 *   - 0x0e - address bits 15-12
 *   - 0x0f - address bits 11-8
 *   - 0x0g - address bits 7-4
 *   - 0x0h - address bits 3-0
 *   - 0xii - read length (1-64 bytes)
 *   - ...  - 2-128 data bytes (1-64 bytes in high/low nibbles LSB only)
 *   - 0xf7 - sysex end
 *
 * - write ext flash buf        - 0x72  -> to device
 *   - 0xf0 - sysex start
 *   - 0x00 - MMA ID
 *   - 0x01 - MMA ID
 *   - 0x72 - MMA ID
 *   - 0x49 - device type
 *   - 0x72 - write ext flash buf
 *   - 0x0c - offset bits 23-20
 *   - 0x0d - offset bits 19-16
 *   - 0x0e - offset bits 15-12
 *   - 0x0f - offset bits 11-8
 *   - 0x0g - offset bits 7-4
 *   - 0x0h - offset bits 3-0
 *   - 0xii - write length (1-64 bytes)
 *   - ...  - 2-128 data bytes (1-64 bytes in high/low nibbles LSB only)
 *   - 0xf7 - sysex end
 *
 * - write ext flash commit     - 0x73  -> to device
 *   - 0xf0 - sysex start
 *   - 0x00 - MMA ID
 *   - 0x01 - MMA ID
 *   - 0x72 - MMA ID
 *   - 0x49 - device type
 *   - 0x73 - write ext flash commit
 *   - 0x0c - address bits 23-20
 *   - 0x0d - address bits 19-16
 *   - 0x0e - address bits 15-12
 *   - 0x0f - address bits 11-8
 *   - 0x0g - address bits 7-4
 *   - 0x0h - address bits 3-0
 *   - 0x0i - write len bits 15-12
 *   - 0x0j - write len bits 11-8
 *   - 0x0k - write len bits 7-4
 *   - 0x0l - write len bits 3-0
 *   - 0xf7 - sysex end
 *
 * - device type query          - 0x7c  -> to device
 *  - 0xf0	- sysex start
 *  - 0x00	- MMA ID
 *  - 0x01	- MMA ID
 *  - 0x72	- MMA ID
 *  - 0x7c	- device type query
 *  - 0xf7	- sysex end
 *
 * - device type response       - 0x7d  <- from device
 *   - 0xf0	- sysex start
 *   - 0x00	- MMA ID
 *   - 0x01	- MMA ID
 *   - 0x72	- MMA ID
 *   - 0xdd	- device type code (this is here due to legacy)
 *   - 0x7d	- device type response
 *   - 0xdd	- device type code
 *   - 0xf7	- sysex end
 *
 * - restart device             - 0x7e  -> to device
 *   - 0xf0	- sysex start
 *   - 0x00	- MMA ID
 *   - 0x01	- MMA ID
 *   - 0x72	- MMA ID
 *   - 0x7e	- restart device
 *   - 0xdd	- device type code
 *   - 0x4b	- 0x4b = 'K'
 *   - 0x49	- 0x49 = 'I'
 *   - 0x4c	- 0x4c = 'L'
 *   - 0x4c	- 0x4c = 'L'
 *   - 0xf7	- sysex end
 *
 * Hardened flash protocol (host must follow these rules):
 * - One request outstanding; BUSY=0x06 does not alter pending I/O metadata.
 * - 0x70 accepts 1..64 bytes within external flash; success is response0x71.
 * - 0x72 accepts exactly 1..64 data bytes as canonical high/low nibbles.
 *   Offset0 begins a fresh sector; send it FIRST, then fill all4096 bytes.
 *   Stage OK acknowledges RAM only; reads do not alter the staging buffer.
 * - 0x73 accepts exactly4096 bytes at a sector-aligned song-storage address.
 *   All bytes must be staged; INCOMPLETE=0x07 rejects missing coverage.
 *   Transport must be stopped for staging/commit. Successful commit has NO
 *   immediate ACK: cmd01/cmd73/OK follows SAVE_DONE; error05 follows failure.
 *   SAVE_DONE is flash-service completion, not readback verification. Host
 *   must read back and compare. Coverage is consumed on success or failure.
 * - Packet sizes including F0/F7: read14, stage14+2*N, commit17.
 * - Host must serialize all panel/config flash I/O and avoid restart/power-off.
 *   Five sector commits replace one0x5000 song; replacement is NOT atomic.
 * - 0x74 is not implemented. This tightens unsafe legacy write behavior.
 *
 * SYSEX Error Response Codes:
 * - ERROR_OK                   - 0x01
 * - ERROR_BAD_ADDRESS          - 0x02
 * - ERROR_BAD_LENGTH           - 0x03
 */
#include "sysex.h"
#include "seq_ctrl.h"
#include "../config.h"
#include "../config_store.h"
#include "../ext_flash.h"
#include "../midi/midi_stream.h"
#include "../midi/midi_protocol.h"
#include "../util/log.h"
#include "stm32f4xx_hal.h"
#include <inttypes.h>
#include <string.h>

//
// SYSEX commands / protocol
//
// CARBON commands
#define SYSEX_CMD_ERROR_CODE 0x01  // from device
// 0x60, 0x61 and 0x62 are used by the remlcd function
#define SYSEX_CMD_SET_CONFIG_STORE 0x6d  // to device - no response
#define SYSEX_CMD_SET_LCD_TYPE 0x6e  // to device
#define SYSEX_CMD_WIPE_CONFIG_STORE 0x6f  // to device
#define SYSEX_CMD_READ_EXT_FLASH 0x70  // to device
#define SYSEX_CMD_READBACK_EXT_FLASH 0x71  // from device
#define SYSEX_CMD_WRITE_EXT_FLASH_BUF 0x72  // to device
#define SYSEX_CMD_WRITE_EXT_FLASH_COMMIT 0x73  // to device
// global commands
#define SYSEX_CMD_DEV_TYPE 0x7c  // to device
#define SYSEX_CMD_DEV_RESPONSE 0x7d  // from device
#define SYSEX_CMD_RESTART 0x7e  // to device
// error response codes
#define SYSEX_ERROR_OK 0x01
#define SYSEX_ERROR_BAD_ADDRESS 0x02
#define SYSEX_ERROR_BAD_LENGTH 0x03
#define SYSEX_ERROR_MALFORMED_MSG 0x04
#define SYSEX_ERROR_EXT_FLASH_ERROR 0x05
#define SYSEX_ERROR_BUSY 0x06
#define SYSEX_ERROR_INCOMPLETE 0x07

// settings
#define SYSEX_MAX_LEN 200
#define SYSEX_MAX_READ_LEN 64

// processing states
#define SYSEX_STATE_IDLE 0
#define SYSEX_STATE_READ_EXT_FLASH 1
#define SYSEX_STATE_WRITE_EXT_FLASH 2

// sysex module data
struct sysex_state {
    int state;  // current command state
    uint8_t rx_buf[SYSEX_MAX_LEN];  // RX buffer for SYSEX reception
    int rx_len;  // length of RX message
    int32_t addr;  // the address of the thing we are reading or writing
    uint8_t io_buf[EXT_FLASH_SECTOR_SIZE];  // buffer for I/O to other modules
    int io_len;  // length of I/O
    uint8_t read_buf[SYSEX_MAX_READ_LEN];
    uint8_t staged[EXT_FLASH_SECTOR_SIZE / 8];  // byte coverage bitmap
};
struct sysex_state syxs;

// local functions
void sysex_process(void);
void sysex_send_read_ext_mem_result(void);
void sysex_send_devtype_response(void);
void sysex_send_error_response(int cmd, int errorcode);
void sysex_nibbles_to_bytes(uint8_t *inbuf, uint8_t *outbuf, int in_len);

// init the sysex handler
void sysex_init(void) {
    syxs.rx_len = 0;
    memset(syxs.staged, 0, sizeof(syxs.staged));
    syxs.state = SYSEX_STATE_IDLE;
}

// Only the owner of an active operation consumes flash completion state.
void sysex_timer_task(void) {
    if(syxs.state == SYSEX_STATE_IDLE) return;
    int state = ext_flash_get_state_for(syxs.state == SYSEX_STATE_READ_EXT_FLASH ? syxs.read_buf : syxs.io_buf);
    if(syxs.state == SYSEX_STATE_READ_EXT_FLASH) {
        if(state == EXT_FLASH_STATE_LOAD_DONE) {
            syxs.state = SYSEX_STATE_IDLE;
            sysex_send_read_ext_mem_result();
        }
        else if(state == EXT_FLASH_STATE_LOAD_ERROR) {
            syxs.state = SYSEX_STATE_IDLE;
            sysex_send_error_response(SYSEX_CMD_READBACK_EXT_FLASH,
                SYSEX_ERROR_EXT_FLASH_ERROR);
        }
    }
    else if(state == EXT_FLASH_STATE_SAVE_DONE || state == EXT_FLASH_STATE_SAVE_ERROR) {
        syxs.state = SYSEX_STATE_IDLE;
        // The buffer is consumed even on failure: a retry must stage anew.
        memset(syxs.staged, 0, sizeof(syxs.staged));
        sysex_send_error_response(SYSEX_CMD_WRITE_EXT_FLASH_COMMIT,
            state == EXT_FLASH_STATE_SAVE_DONE ? SYSEX_ERROR_OK : SYSEX_ERROR_EXT_FLASH_ERROR);
    }
}

// Assemble bounded frames; discard overflow until a fresh F0.
void sysex_handle_msg(struct midi_msg *msg) {
    uint8_t bytes[3] = {msg->status, msg->data0, msg->data1};
    if(msg->len < 1 || msg->len > 3) return;
    for(int i = 0; i < msg->len; i++) {
        uint8_t byte = bytes[i];
        if(byte >= 0xf8) continue;  // interleaved realtime bytes
        if(byte == MIDI_SYSEX_START) syxs.rx_len = 0;
        else if(syxs.rx_len == 0) continue;
        if(syxs.rx_len == SYSEX_MAX_LEN) {
            syxs.rx_len = 0;
            continue;
        }
        syxs.rx_buf[syxs.rx_len++] = byte;
        if(byte == MIDI_SYSEX_END) {
            sysex_process();
            syxs.rx_len = 0;
        }
    }
}

//
// local functions
//
// Decode only canonical nibbles, not silently masked malformed data.
static int sysex_decode_nibbles(int offset, int count) {
    int value = 0;
    for(int i = 0; i < count; i++) {
        if(syxs.rx_buf[offset + i] > 15) return -1;
        value = (value << 4) | syxs.rx_buf[offset + i];
    }
    return value;
}

static void sysex_process_flash(int cmd) {
    int addr, len;
    if(syxs.state != SYSEX_STATE_IDLE || ext_flash_peek_state() != EXT_FLASH_STATE_IDLE) {
        sysex_send_error_response(cmd, SYSEX_ERROR_BUSY);
        return;
    }
    if((cmd == SYSEX_CMD_READ_EXT_FLASH && syxs.rx_len != 14) ||
       (cmd == SYSEX_CMD_WRITE_EXT_FLASH_COMMIT && syxs.rx_len != 17) ||
       (cmd == SYSEX_CMD_WRITE_EXT_FLASH_BUF && syxs.rx_len < 16)) {
        sysex_send_error_response(cmd, SYSEX_ERROR_MALFORMED_MSG);
        return;
    }
    addr = sysex_decode_nibbles(6, 6);
    len = cmd == SYSEX_CMD_WRITE_EXT_FLASH_COMMIT ?
        sysex_decode_nibbles(12, 4) : syxs.rx_buf[12];
    if(addr < 0 || len < 0) {
        sysex_send_error_response(cmd, SYSEX_ERROR_MALFORMED_MSG);
        return;
    }
    if(cmd == SYSEX_CMD_READ_EXT_FLASH) {
        if(len < 1 || len > SYSEX_MAX_READ_LEN) {
            sysex_send_error_response(cmd, SYSEX_ERROR_BAD_LENGTH);
            return;
        }
        if(addr > ext_flash_get_mem_size() - len) {
            sysex_send_error_response(cmd, SYSEX_ERROR_BAD_ADDRESS);
            return;
        }
        if(ext_flash_load(addr, len, syxs.read_buf) == -1) {
            sysex_send_error_response(cmd, SYSEX_ERROR_EXT_FLASH_ERROR);
            return;
        }
        syxs.addr = addr;
        syxs.io_len = len;
        syxs.state = SYSEX_STATE_READ_EXT_FLASH;
        return;
    }
    // Write commands require stopped transport; never change it implicitly.
    if(seq_ctrl_get_run_state()) {
        sysex_send_error_response(cmd, SYSEX_ERROR_BUSY);
        return;
    }
    if(cmd == SYSEX_CMD_WRITE_EXT_FLASH_BUF) {
        if(len < 1 || len > SYSEX_MAX_READ_LEN || addr > EXT_FLASH_SECTOR_SIZE - len) {
            sysex_send_error_response(cmd, SYSEX_ERROR_BAD_LENGTH);
            return;
        }
        if(syxs.rx_len != 14 + 2 * len) {
            sysex_send_error_response(cmd, SYSEX_ERROR_MALFORMED_MSG);
            return;
        }
        for(int i = 13; i < 13 + 2 * len; i++) {
            if(syxs.rx_buf[i] > 15) {
                sysex_send_error_response(cmd, SYSEX_ERROR_MALFORMED_MSG);
                return;
            }
        }
        // Offset zero explicitly starts a fresh sector; send it first.
        if(addr == 0) memset(syxs.staged, 0, sizeof(syxs.staged));
        sysex_nibbles_to_bytes(syxs.rx_buf + 13, syxs.io_buf + addr, len);
        for(int i = addr; i < addr + len; i++)
            syxs.staged[i / 8] |= (uint8_t)(1u << (i % 8));
        sysex_send_error_response(cmd, SYSEX_ERROR_OK);
        return;
    }
    if(len != EXT_FLASH_SECTOR_SIZE) {
        sysex_send_error_response(cmd, SYSEX_ERROR_BAD_LENGTH);
        return;
    }
    if(addr < EXT_FLASH_SONG_OFFSET ||
       addr % EXT_FLASH_SECTOR_SIZE != 0 ||
       addr > EXT_FLASH_SONG_OFFSET + SEQ_NUM_SONGS * EXT_FLASH_SONG_SIZE - len ||
       addr > ext_flash_get_mem_size() - len) {
        sysex_send_error_response(cmd, SYSEX_ERROR_BAD_ADDRESS);
        return;
    }
    for(unsigned i = 0; i < sizeof(syxs.staged); i++) {
        if(syxs.staged[i] != 0xff) {
            sysex_send_error_response(cmd, SYSEX_ERROR_INCOMPLETE);
            return;
        }
    }
    if(ext_flash_save(addr, len, syxs.io_buf) == -1) {
        sysex_send_error_response(cmd, SYSEX_ERROR_EXT_FLASH_ERROR);
        return;
    }
    syxs.state = SYSEX_STATE_WRITE_EXT_FLASH;
    // No immediate OK: cmd73 response follows flash-service completion/error.
}

// process a SYSEX message that has been completely received
void sysex_process(void) {
    int32_t addr, val;
    if(syxs.rx_len < 6 || syxs.rx_buf[0] != MIDI_SYSEX_START ||
       syxs.rx_buf[syxs.rx_len - 1] != MIDI_SYSEX_END) {
        return;
    }
    // check MMA ID
    if(syxs.rx_buf[1] != SYSEX_MMA_ID0) {
        return;
    }
    if(syxs.rx_buf[2] != SYSEX_MMA_ID1) {
        return;
    }
    if(syxs.rx_buf[3] != SYSEX_MMA_ID2) {
        return;
    }
    // parse global command / device type
    switch(syxs.rx_buf[4]) {
        // global Kilpatrick commands
        case SYSEX_CMD_DEV_TYPE:
            if(syxs.rx_len != 6) {
                sysex_send_error_response(syxs.rx_buf[4],
                    SYSEX_ERROR_MALFORMED_MSG);
                return;
            }
            sysex_send_devtype_response();
            break;
        case SYSEX_CMD_RESTART:
            if(syxs.state != SYSEX_STATE_IDLE) {
                sysex_send_error_response(SYSEX_CMD_RESTART, SYSEX_ERROR_BUSY);
                return;
            }
            if(syxs.rx_len != 11) {
                sysex_send_error_response(syxs.rx_buf[4],
                    SYSEX_ERROR_MALFORMED_MSG);
                return;
            }
            // check that the dev type and reset message matches
            if(syxs.rx_buf[5] != MIDI_DEV_TYPE) return;
            if(syxs.rx_buf[6] != 'K') return;
            if(syxs.rx_buf[7] != 'I') return;
            if(syxs.rx_buf[8] != 'L') return;
            if(syxs.rx_buf[9] != 'L') return;
            HAL_NVIC_SystemReset();
            break;
        case MIDI_DEV_TYPE:  // CARBON message
            if(syxs.rx_len < 7) {
                sysex_send_error_response(syxs.rx_buf[4],
                    SYSEX_ERROR_MALFORMED_MSG);
                return;
            }
            // process each command
            switch(syxs.rx_buf[5]) {
                case SYSEX_CMD_SET_CONFIG_STORE:
                    if(syxs.rx_len != 16) {
                        // no response
                        return;
                    }
                    addr = syxs.rx_buf[6];
                    val = (syxs.rx_buf[7] & 0x0f) << 28;
                    val |= (syxs.rx_buf[8] & 0x0f) << 24;
                    val |= (syxs.rx_buf[9] & 0x0f) << 20;
                    val |= (syxs.rx_buf[10] & 0x0f) << 16;
                    val |= (syxs.rx_buf[11] & 0x0f) << 12;
                    val |= (syxs.rx_buf[12] & 0x0f) << 8;
                    val |= (syxs.rx_buf[13] & 0x0f) << 4;
                    val |= syxs.rx_buf[14] & 0x0f;
                    config_store_set_val(addr, val);
                    break;
                case SYSEX_CMD_SET_LCD_TYPE:
                    if(syxs.rx_len != 8) {
                        sysex_send_error_response(syxs.rx_buf[5],
                            SYSEX_ERROR_MALFORMED_MSG);
                        return;
                    }
                    config_store_set_val(CONFIG_STORE_GUI_DISP_TYPE,
                        syxs.rx_buf[6]);
                    sysex_send_error_response(syxs.rx_buf[5],
                        SYSEX_ERROR_OK);
                    break;
                case SYSEX_CMD_WIPE_CONFIG_STORE:
                    if(syxs.rx_len != 7) {
                        sysex_send_error_response(syxs.rx_buf[5],
                            SYSEX_ERROR_MALFORMED_MSG);
                        return;
                    }
                    config_store_wipe_flash();
                    sysex_send_error_response(syxs.rx_buf[5],
                        SYSEX_ERROR_OK);
                    break;
                case SYSEX_CMD_READ_EXT_FLASH:
                case SYSEX_CMD_WRITE_EXT_FLASH_BUF:
                case SYSEX_CMD_WRITE_EXT_FLASH_COMMIT:
                    sysex_process_flash(syxs.rx_buf[5]);
                    break;
            }
            break;
    }
}


// send a flash read mem result
void sysex_send_read_ext_mem_result(void) {
    uint8_t tx_buf[SYSEX_MAX_LEN];
    int i, tx_count = 0;
    tx_buf[tx_count++] = MIDI_SYSEX_START;
    tx_buf[tx_count++] = SYSEX_MMA_ID0;
    tx_buf[tx_count++] = SYSEX_MMA_ID1;
    tx_buf[tx_count++] = SYSEX_MMA_ID2;
    tx_buf[tx_count++] = MIDI_DEV_TYPE;
    tx_buf[tx_count++] = SYSEX_CMD_READBACK_EXT_FLASH;
    tx_buf[tx_count++] = (syxs.addr & 0xf00000) >> 20;
    tx_buf[tx_count++] = (syxs.addr & 0x0f0000) >> 16;
    tx_buf[tx_count++] = (syxs.addr & 0x00f000) >> 12;
    tx_buf[tx_count++] = (syxs.addr & 0x000f00) >> 8;
    tx_buf[tx_count++] = (syxs.addr & 0x0000f0) >> 4;
    tx_buf[tx_count++] = (syxs.addr & 0x00000f);
    tx_buf[tx_count++] = syxs.io_len & 0x7f;
    for(i = 0; i < syxs.io_len; i ++) {
        tx_buf[tx_count++] = (syxs.read_buf[i] & 0xf0) >> 4;
        tx_buf[tx_count++] = syxs.read_buf[i] & 0x0f;
    }
    tx_buf[tx_count++] = MIDI_SYSEX_END;
    midi_stream_send_sysex_msg(MIDI_PORT_SYSEX_OUT, tx_buf, tx_count);
}

// send the devtype response
void sysex_send_devtype_response(void) {
    uint8_t tx_buf[SYSEX_MAX_LEN];
    int tx_count = 0;
    tx_buf[tx_count++] = MIDI_SYSEX_START;
    tx_buf[tx_count++] = SYSEX_MMA_ID0;
    tx_buf[tx_count++] = SYSEX_MMA_ID1;
    tx_buf[tx_count++] = SYSEX_MMA_ID2;
    tx_buf[tx_count++] = MIDI_DEV_TYPE;
    tx_buf[tx_count++] = SYSEX_CMD_DEV_RESPONSE;
    tx_buf[tx_count++] = MIDI_DEV_TYPE;
    tx_buf[tx_count++] = MIDI_SYSEX_END;
    midi_stream_send_sysex_msg(MIDI_PORT_SYSEX_OUT, tx_buf, tx_count);
}

// send an error code response
void sysex_send_error_response(int cmd, int errorcode) {
    uint8_t tx_buf[SYSEX_MAX_LEN];
    int tx_count = 0;
    tx_buf[tx_count++] = MIDI_SYSEX_START;
    tx_buf[tx_count++] = SYSEX_MMA_ID0;
    tx_buf[tx_count++] = SYSEX_MMA_ID1;
    tx_buf[tx_count++] = SYSEX_MMA_ID2;
    tx_buf[tx_count++] = MIDI_DEV_TYPE;
    tx_buf[tx_count++] = SYSEX_CMD_ERROR_CODE;
    tx_buf[tx_count++] = cmd & 0x7f;
    tx_buf[tx_count++] = errorcode & 0x7f;
    tx_buf[tx_count++] = MIDI_SYSEX_END;
    midi_stream_send_sysex_msg(MIDI_PORT_SYSEX_OUT, tx_buf, tx_count);
}

// Decode byte_count bytes from an already validated high/low nibble payload.
void sysex_nibbles_to_bytes(uint8_t *inbuf, uint8_t *outbuf, int byte_count) {
    for(int i = 0; i < byte_count; i++)
        outbuf[i] = (uint8_t)((inbuf[2 * i] << 4) | inbuf[2 * i + 1]);
}

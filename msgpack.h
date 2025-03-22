/* Copyright 2021 Seth Bonner <fl3tching101@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>
#include <vector>
#include <string>
#include <optional>

#define MSGPACK_UNKNOWN         0   
#define MSGPACK_CURRENT_KEYCODE 1
#define MSGPACK_CURRENT_LAYER   2
#define MSGPACK_CURRENT_LEDSTATE 3


#define MSGPACK_PAIR_ARRAY_SIZE 10
#define RAW_EPSIZE 64

// Define data structure
typedef struct {
    uint8_t key;
    uint16_t value;
    std::string name;
} msgpack_pair_t;

typedef struct {
    uint8_t count;
    msgpack_pair_t pairs[MSGPACK_PAIR_ARRAY_SIZE];
} msgpack_t;

std::optional<uint8_t> msgpack_getValue(msgpack_t* km, uint8_t key);
bool add_msgpack_add(msgpack_t *msgpack, uint8_t key, uint16_t value);
void init_msgpack(msgpack_t *msgpack);
bool read_msgpack(msgpack_t* km, std::vector<uint8_t>& data);
bool msgpack_log(msgpack_t* km);
bool make_msgpack(msgpack_t* km, std::vector<uint8_t>& data);
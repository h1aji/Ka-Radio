#!/usr/bin/env python3
#
# File	: gen_appbin.py
# This file is part of Espressif's generate bin script.
# Copyright (C) 2013 - 2016, Espressif Systems
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of version 3 of the GNU General Public License as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program.  If not, see <http://www.gnu.org/licenses/>.

"""This file is part of Espressif's generate bin script.
    argv[1] is elf file name
    argv[2] is version num

Python 3 port of the original Python 2 script.
"""

import sys
import os
import re
import struct
import binascii


TEXT_ADDRESS = 0x40100000

CHECKSUM_INIT = 0xEF

chk_sum = CHECKSUM_INIT
blocks = 0


def write_file(file_name, data):
    if file_name is None:
        print('file_name cannot be none')
        sys.exit(1)

    with open(file_name, 'ab') as fp:
        fp.write(data)


def combine_bin(file_name, dest_file_name, start_offset_addr, need_chk):
    global chk_sum
    global blocks
    if dest_file_name is None:
        print('dest_file_name cannot be none')
        sys.exit(1)

    if not file_name:
        return

    try:
        fp = open(file_name, 'rb')
    except OSError:
        print('!!!Open %s fail!!!' % file_name)
        return

    with fp:
        ########## write text ##########
        fp.seek(0, os.SEEK_END)
        data_len = fp.tell()
        if data_len:
            if need_chk:
                tmp_len = (data_len + 3) & (~3)
            else:
                tmp_len = (data_len + 15) & (~15)
            data_bin = struct.pack('<II', start_offset_addr, tmp_len)
            write_file(dest_file_name, data_bin)
            fp.seek(0, os.SEEK_SET)
            data_bin = fp.read(data_len)
            write_file(dest_file_name, data_bin)
            if need_chk:
                for b in data_bin:      # iterating bytes yields ints in Python 3
                    chk_sum ^= b
            tmp_len = tmp_len - data_len
            if tmp_len:
                data_bin = b'\x00' * tmp_len
                write_file(dest_file_name, data_bin)
                # padding bytes are zero, so they do not change the checksum
            blocks = blocks + 1


def getFileCRC(_path):
    try:
        blocksize = 1024 * 64
        crc = 0
        with open(_path, 'rb') as f:
            chunk = f.read(blocksize)
            while len(chunk) != 0:
                crc = binascii.crc32(chunk, crc)
                chunk = f.read(blocksize)
    except OSError:
        print('get file crc error!')
        return 0
    # Python 2 returned a signed value, Python 3 returns unsigned.
    # Convert to signed so the later "< 0" logic behaves exactly as before.
    if crc >= 0x80000000:
        crc -= 0x100000000
    return crc


def gen_appbin():
    global chk_sum
    global blocks
    if len(sys.argv) != 6:
        print('Usage: gen_appbin.py eagle.app.out boot_mode flash_mode flash_clk_div flash_size_map')
        sys.exit(1)

    elf_file = sys.argv[1]
    boot_mode = sys.argv[2]
    flash_mode = sys.argv[3]
    flash_clk_div = sys.argv[4]
    flash_size_map = sys.argv[5]

    flash_data_line = 16
    data_line_bits = 0xf

    irom0text_bin_name = 'eagle.app.v6.irom0text.bin'
    text_bin_name = 'eagle.app.v6.text.bin'
    data_bin_name = 'eagle.app.v6.data.bin'
    rodata_bin_name = 'eagle.app.v6.rodata.bin'
    flash_bin_name = 'eagle.app.flash.bin'

    BIN_MAGIC_FLASH = 0xE9
    BIN_MAGIC_IROM = 0xEA
    sum_size = 0

    if os.getenv('COMPILE') == 'xcc':
        cmd = 'xt-nm -g ' + elf_file + ' > eagle.app.sym'
    else:
        cmd = 'xtensa-lx106-elf-nm -g ' + elf_file + ' > eagle.app.sym'

    os.system(cmd)

    try:
        with open('./eagle.app.sym', 'r') as fp:
            lines = fp.readlines()
    except OSError:
        print('open sym file error')
        sys.exit(1)

    entry_addr = None
    p = re.compile(r'(\w*)(\sT\s)(call_user_start)$')
    for line in lines:
        m = p.search(line.rstrip())
        if m is not None:
            entry_addr = m.group(1)

    if entry_addr is None:
        print('no entry point!!')
        sys.exit(1)

    data_start_addr = '0'
    p = re.compile(r'(\w*)(\sA\s)(_data_start)$')
    for line in lines:
        m = p.search(line.rstrip())
        if m is not None:
            data_start_addr = m.group(1)

    rodata_start_addr = '0'
    p = re.compile(r'(\w*)(\sA\s)(_rodata_start)$')
    for line in lines:
        m = p.search(line.rstrip())
        if m is not None:
            rodata_start_addr = m.group(1)

    # write flash bin header
    #============================
    #  SPI FLASH PARAMS
    #-------------------
    #flash_mode=
    #     0: QIO
    #     1: QOUT
    #     2: DIO
    #     3: DOUT
    #-------------------
    #flash_clk_div=
    #     0 :  80m / 2
    #     1 :  80m / 3
    #     2 :  80m / 4
    #    0xf:  80m / 1
    #-------------------
    #flash_size_map=
    #     0 : 512 KB (256 KB + 256 KB)
    #     1 : 256 KB
    #     2 : 1024 KB (512 KB + 512 KB)
    #     3 : 2048 KB (512 KB + 512 KB)
    #     4 : 4096 KB (512 KB + 512 KB)
    #     5 : 2048 KB (1024 KB + 1024 KB)
    #     6 : 4096 KB (1024 KB + 1024 KB)
    #-------------------
    #   END OF SPI FLASH PARAMS
    #============================
    byte2 = int(flash_mode) & 0xff
    byte3 = ((int(flash_size_map) << 4) | int(flash_clk_div)) & 0xff

    if boot_mode == '2':
        # write irom bin head
        data_bin = struct.pack('<BBBBI', BIN_MAGIC_IROM, 4, byte2, byte3, int(entry_addr, 16))
        sum_size = len(data_bin)
        write_file(flash_bin_name, data_bin)

        # irom0.text.bin
        combine_bin(irom0text_bin_name, flash_bin_name, 0x0, 0)

    data_bin = struct.pack('<BBBBI', BIN_MAGIC_FLASH, 3, byte2, byte3, int(entry_addr, 16))
    sum_size = len(data_bin)
    write_file(flash_bin_name, data_bin)

    # text.bin
    combine_bin(text_bin_name, flash_bin_name, TEXT_ADDRESS, 1)

    # data.bin
    if data_start_addr:
        combine_bin(data_bin_name, flash_bin_name, int(data_start_addr, 16), 1)

    # rodata.bin
    combine_bin(rodata_bin_name, flash_bin_name, int(rodata_start_addr, 16), 1)

    # write checksum header
    sum_size = os.path.getsize(flash_bin_name) + 1
    sum_size = flash_data_line - (data_line_bits & sum_size)
    if sum_size:
        write_file(flash_bin_name, b'\x00' * sum_size)
    write_file(flash_bin_name, bytes([chk_sum & 0xFF]))

    if boot_mode == '1':
        sum_size = os.path.getsize(flash_bin_name)
        write_file(flash_bin_name, b'\xFF' * (0x10000 - sum_size))

        try:
            with open(irom0text_bin_name, 'rb') as fp:
                write_file(flash_bin_name, fp.read())
        except OSError:
            print('!!!Open %s fail!!!' % irom0text_bin_name)
            sys.exit(1)

    if boot_mode == '1' or boot_mode == '2':
        all_bin_crc = getFileCRC(flash_bin_name)
        if all_bin_crc < 0:
            all_bin_crc = abs(all_bin_crc) - 1
        else:
            all_bin_crc = abs(all_bin_crc) + 1
        print("bin crc: %x" % all_bin_crc)
        write_file(flash_bin_name, struct.pack('<I', all_bin_crc & 0xFFFFFFFF))

    try:
        os.remove('eagle.app.sym')
    except OSError:
        pass


if __name__ == '__main__':
    gen_appbin()


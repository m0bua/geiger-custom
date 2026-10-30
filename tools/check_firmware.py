"""Reject firmware whose interrupt vectors resolve to missing/weak handlers."""

from pathlib import Path
import struct


FLASH_BASE = 0x08000000
FLASH_SIZE = 65536


def check_firmware(elf_path):
    data = Path(elf_path).read_bytes()
    if data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected an ELF32 little-endian firmware image')
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    sections = [struct.unpack_from('<10I', data, header[6] + i * header[11])
                for i in range(header[12])]

    def contents(section):
        return data[section[4]:section[4] + section[5]]

    def string(table, offset):
        return table[offset:table.index(b'\0', offset)].decode('utf-8')

    names = contents(sections[header[13]])
    by_name = {string(names, section[0]): section for section in sections}
    symbols = {}
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        strings = contents(sections[section[6]])
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, value, _, info, _, index = struct.unpack_from('<IIIBBH', data, offset)
            symbols[string(strings, name)] = (value, info >> 4, index)

    vector_section = by_name['.isr_vector']
    if vector_section[3] != FLASH_BASE:
        raise ValueError('Interrupt vectors must start at the Flash base')
    vectors = contents(vector_section)
    stack, = struct.unpack_from('<I', vectors)
    if not 0x20000000 < stack <= 0x20002000 or stack % 8:
        raise ValueError('Invalid initial stack pointer')
    default = symbols['Default_Handler'][0] | 1
    for slot, name in ((1, 'Reset_Handler'), (15, 'SysTick_Handler'),
                       (23, 'EXTI4_15_IRQHandler'), (43, 'USART1_IRQHandler')):
        value, binding, index = symbols[name]
        vector, = struct.unpack_from('<I', vectors, slot * 4)
        if binding != 1 or index == 0 or vector != (value | 1) or vector == default:
            raise ValueError(f'{name} must be a strong handler referenced by vector {slot}')
        if not FLASH_BASE <= (value & ~1) < FLASH_BASE + FLASH_SIZE:
            raise ValueError(f'{name} is outside Flash')

    end = FLASH_BASE
    for i in range(header[10]):
        kind, _, _, address, size, _, _, _ = struct.unpack_from(
            '<8I', data, header[5] + i * header[9])
        if kind == 1 and size and FLASH_BASE <= address < 0x10000000:
            end = max(end, address + size)
    if end > FLASH_BASE + FLASH_SIZE:
        raise ValueError(f'Firmware exceeds Flash by {end - FLASH_BASE - FLASH_SIZE} bytes')
    print(f'Verified startup/IRQ vectors; Flash image: {end - FLASH_BASE} / {FLASH_SIZE} bytes')

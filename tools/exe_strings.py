"""exe_strings.py - printable strings of a PE file with their virtual address: python tools/exe_strings.py spel.dat [minlen]"""
import re, sys, pefile

pe = pefile.PE(sys.argv[1])
minlen = int(sys.argv[2]) if len(sys.argv) > 2 else 5
base = pe.OPTIONAL_HEADER.ImageBase
for s in pe.sections:
    name = s.Name.rstrip(b'\0').decode()
    if name == '.text':
        continue
    data = s.get_data()
    for m in re.finditer(rb'[\x20-\x7e\xa0-\xff\t\r\n]{%d,}' % minlen, data):
        txt = m.group().decode('latin-1').replace('\r', '\r').replace('\n', '\n').replace('\t', '\t')
        print(f'{base + s.VirtualAddress + m.start():#x} {name:6} {txt}')

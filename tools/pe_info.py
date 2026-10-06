"""pe_info.py - sections, imports and timestamp of a PE file (A2 Racer II: spel.dat, menu.dat, a2_II.exe, qrace.exe)."""
import sys, time, pefile

for path in sys.argv[1:]:
    pe = pefile.PE(path)
    fh, oh = pe.FILE_HEADER, pe.OPTIONAL_HEADER
    print(f"== {path}  built {time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(fh.TimeDateStamp))}  "
          f"base {oh.ImageBase:#x} entry {oh.ImageBase + oh.AddressOfEntryPoint:#x} linker {oh.MajorLinkerVersion}.{oh.MinorLinkerVersion}")
    for s in pe.sections:
        print(f"  {s.Name.rstrip(b'\0').decode():8} va {oh.ImageBase + s.VirtualAddress:#x} vsize {s.Misc_VirtualSize:#8x} raw {s.SizeOfRawData:#8x}")
    for e in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
        names = [(i.name or b'#%d' % i.ordinal).decode() for i in e.imports]
        print(f"  {e.dll.decode()}: {' '.join(names)}")

# Resource archives: `.IND` + `.IMG`

Four archives on the CD (`data\`), each an index file and a data file. All little endian. Tool: `tools/imgpack.py`
(`list`, `unpack`); engine: `src/res.c`.

```
.IND   u16 count
       named   (RCS, Mrcs):     count x { char name[20]; u32 offset; }   name NUL-terminated, the rest is junk
       unnamed (RC, MENURC):    count x { u32 offset; }
.IMG   the files back to back; a file's size is the next offset (or the .IMG size) minus its own offset
```

The two kinds are told apart by the index size (`2 + count*24` or `2 + count*4`).

| Archive | Entries | Used by | Contents |
|---|---|---|---|
| `RCS` | 806 | spel.dat | 448 `.d3d` meshes (447 names, `DEFAULT.d3d` twice), 106 `.tga`, 147 `.pcx`, 74 `.wav`, 31 `.fnt` |
| `Mrcs` | 123 | menu.dat | menu screens (`.pcx`), fonts (`.img`), music and spoken help (`.wav`) |
| `RC` | 453 | spel.dat | HUD sprites, see [FORMAT_IMAGES.md](FORMAT_IMAGES.md) |
| `MENURC` | 482 | menu.dat | menu sprites |

The sprite loader of both programs (`sprite_load_bank`, spel.dat `0x46c1c0`) seeks to `id*4-2` in the unnamed index, so
sprite id N is entry N-1.

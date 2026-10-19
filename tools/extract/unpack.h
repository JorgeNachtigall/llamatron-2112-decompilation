/* Executable unpackers. Every shipped EXE is LZEXE 0.91 on the outside with Microsoft EXEPACK
 * inside; removing both yields the program Ghidra works on. Both decoders reproduce
 * tools/unlzexe.py and tools/unexepack.py byte for byte (checked by ctest). */
#ifndef EXTRACT_UNPACK_H
#define EXTRACT_UNPACK_H

#include "mz.h"

bool lzexe_detect(const uint8_t *data, size_t size);
bool lzexe_unpack(const uint8_t *data, size_t size, MzProgram *out);

bool exepack_detect(const uint8_t *data, size_t size);
bool exepack_unpack(const uint8_t *data, size_t size, MzProgram *out);

/* Peels off LZEXE and EXEPACK layers until neither is present; writes the final MZ file.
 * `layers` (optional) receives how many layers were removed. */
bool unpack_all(const uint8_t *data, size_t size, ByteBuf *out, int *layers);

#endif

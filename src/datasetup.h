/* datasetup.h - where the game files come from (PORT EXTRA; the original installs them with InstallShield and reads
 * the rest from the CD): finding a copy of the CD's data folder, and at the first start copying it from the CD, a
 * mounted or unmounted ISO image of it, or a folder with a copy, checked against src/datafiles.h.
 *
 * This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE). */
#ifndef A2_DATASETUP_H
#define A2_DATASETUP_H

/* The data folder to use. `arg` is a folder or an .iso from the command line (or NULL). Looks in this order: arg,
 * A2_DATA, data/ next to the program, extract/data (a development checkout), <user dir>/data. Nothing there: looks for
 * the CD in every drive / under /media, /run/media, /mnt, then asks for an ISO image or a folder, and copies the files
 * into <user dir>/data (or data/ next to the program when that folder is writable and holds the program alone).
 * Returns the folder, or NULL when the user gave up (a message was shown). */
const char *data_find(const char *arg);

/* --verify: compares the files of `dir` with the manifest; prints and returns the number of missing / different
 * required files (0 = an exact copy of the Dutch 1.01 CD). */
int data_verify(const char *dir);

/* the copy from an ISO 9660 image (the CD's data folder) into dest; 0 = failed */
int data_copy_iso(const char *iso_path, const char *dest);
int data_copy_iso_fd(int fd, const char *dest);
#endif

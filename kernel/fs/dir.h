#ifndef DIR_H
#define DIR_H

/*
 * HoneyOS Directory Operations
 *
 * Directories use the same FAT-chained block structure as files.
 * The root directory is a fixed region; sub-directories occupy one
 * 128 KB block each. All operations act on the current working directory.
 */

/* Print the names and sizes of all entries in the current directory */
void dir_list  (void);

/* Create a new sub-directory in the current directory */
void dir_create(const char *name);

/* Change the current working directory ("cd name" or "cd ..") */
void dir_change(const char *name);

/* Remove an empty directory and free its block */
void dir_delete(const char *name);

#endif /* DIR_H */

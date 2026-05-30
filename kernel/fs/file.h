#ifndef FILE_H
#define FILE_H

/*
 * HoneyOS File Operations
 *
 * All operations act on the current working directory (cwd_sector).
 * File data is stored in 128 KB FAT-chained blocks on the disk.
 */

/* Print a file's contents to the VGA screen */
void file_read  (const char *name);

/* Create or overwrite a file — prompts the user line by line, ends with "." */
void file_write (const char *name);

/* Show a file's current contents then allow the user to rewrite it */
void file_edit  (const char *name);

/* Free the file's FAT chain and clear its directory entry */
void file_delete(const char *name);

#endif /* FILE_H */

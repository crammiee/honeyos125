#ifndef SHELL_H
#define SHELL_H

#define SHELL_LINE_MAX  256
#define SHELL_ARGC_MAX  8

/* Read one line, parse it, and execute the matching command */
void shell_run(void);

#endif /* SHELL_H */

#ifndef DEBUG_H
#define DEBUG_H

/* 0 = no debug output, 1 = debug output */
#define DEBUG 0

#if DEBUG
#define DEBUGPRINT(fmtstr, ...) printf("[%s:%d]: " fmtstr, __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define DEBUGPRINT(...)
#endif /* DEBUG */

#endif /* DEBUG_H */

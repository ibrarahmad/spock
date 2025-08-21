/*
 * spock_replay_spill.h
 *    Spill-to-disk API for large transaction replay.
 */

#ifndef SPOCK_REPLAY_SPILL_H
#define SPOCK_REPLAY_SPILL_H

#include "c.h"
#include "lib/stringinfo.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Lifecycle */
extern bool spock_spill_begin(void);
extern void spock_spill_end(void);

/* State */
extern bool spock_spill_active(void);

/* Writer side */
extern bool spock_spill_write_frame(const char *data, Size len);
extern void spock_spill_flush_buffer(void);

/* Reader side */
extern bool spock_spill_reader_start(void);
/* Reads next frame into 'out'; returns false on EOF or error. */
extern bool spock_spill_reader_next(StringInfo out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPOCK_REPLAY_SPILL_H */
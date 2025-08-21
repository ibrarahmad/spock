/*-------------------------------------------------------------------------
 *
 * spock_replay_spill.c
 *      Spill-to-disk helper using PostgreSQL BufFile (temp files).
 *
 * Frame format: [uint32 length][payload bytes]
 *
 * Safety/features:
 *  - Dedicated MemoryContext to own spill state for easy cleanup.
 *  - All BufFile I/O wrapped in PG_TRY/PG_CATCH with clear logs.
 *  - Strict bounds checks for uint32 header and StringInfo limits.
 *  - Reader is explicitly primed via spock_spill_reader_start().
 *  - Extra LOG instrumentation for tracing flows and sizes.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <limits.h>
#include <errno.h>

#include "lib/stringinfo.h"
#include "miscadmin.h"
#include "storage/buffile.h"
#include "utils/memutils.h"
#include "access/xact.h"    /* StartTransactionCommand, CommitTransactionCommand */

#include "spock_replay_spill.h"

/* ---------- Internal state ---------- */

typedef struct SpillState
{
    MemoryContext	ctx;          /* dedicated spill context */
    BufFile		   *file;         /* temp file handle */
    bool			reader_ready; /* set after reader_start */

    /* Observability */
    uint64			frames_written;
    uint64			bytes_written;
    uint64			frames_read;
    uint64			bytes_read;
} SpillState;

static SpillState *Spill = NULL;

/*
 * Upper bound for a single frame payload such that:
 * - It fits in a uint32 length header
 * - It can be represented in StringInfo (int length, +1 for '\0')
 * - It does not exceed MaxAllocSize
 */
static inline uint32
max_frame_len(void)
{
    Size cap = MaxAllocSize;

    /* StringInfo uses 'int' for lengths and ensures +1 for '\0' */
    if (cap > (Size) (INT_MAX - 1))
        cap = (Size) (INT_MAX - 1);

    if (cap > (Size) UINT32_MAX)
        cap = (Size) UINT32_MAX;

    /* Keep a safety margin of 1 for the StringInfo terminator */
    if (cap > 0)
        cap -= 1;

    return (uint32) cap;
}

/* ---------- Small helpers ---------- */

static bool
buf_write_all(BufFile *f, const void *ptr, size_t nbytes)
{
    	/* BufFileWrite returns void and writes all data or throws ERROR */
    PG_TRY();
    {
        BufFileWrite(f, ptr, nbytes);
    }
    PG_CATCH();
    {
        		ereport(WARNING,
				(errmsg("SPOCK spill: write failed (nbytes=%zu)", nbytes)));
		FlushErrorState();
		return false;
    }
    PG_END_TRY();

    	return true;
}

static bool
buf_read_all(BufFile *f, void *ptr, size_t nbytes, bool *eof)
{
    char   *p = (char *) ptr;

    	if (eof != NULL)
        *eof = false;

    while (nbytes > 0)
    {
        size_t	n = 0;

        CHECK_FOR_INTERRUPTS();

        PG_TRY();
        {
            n = BufFileRead(f, p, nbytes);
        }
        PG_CATCH();
        {
            ereport(WARNING,
                    (errmsg("SPOCK spill: read failed (remaining=%zu)", nbytes)));
            FlushErrorState();
            return false;
        }
        PG_END_TRY();

        if (n == 0)
        {
            if (eof != NULL)
                *eof = true;
            return false;		/* EOF before we satisfied request */
        }

        p += n;
        nbytes -= n;
    }

    	return true;
}

static void
spill_cleanup(void)
{
    MemoryContext	ctx;

    	if (Spill == NULL)
		return;

    /* Close temp file; BufFileClose removes the temp under the hood */
    if (Spill->file != NULL)
    {
        PG_TRY();
        {
            (void) BufFileClose(Spill->file);
        }
        PG_CATCH();
        {
            /*
             * Ignore I/O errors on close during cleanup; file will be removed
             * by temp file machinery at process exit anyway.
             */
            FlushErrorState();
        }
        PG_END_TRY();

        Spill->file = NULL;
    }

    /* Delete the memory context last; invalidate global first to avoid UAF */
    ctx = Spill->ctx;
    Spill = NULL;

    if (ctx != NULL)
    {
        PG_TRY();
        {
            MemoryContextDelete(ctx);
        }
        PG_CATCH();
        {
            /* Best-effort cleanup; don't rethrow from destructor path */
            FlushErrorState();
        }
        PG_END_TRY();
    }

    }

/* ---------- Public API ---------- */

bool
spock_spill_begin(void)
{
	MemoryContext	oldcxt;
	MemoryContext	spillcxt;
	bool			started_tx = false;

    	/* Best-effort clear any previous state */
    spill_cleanup();

    oldcxt = CurrentMemoryContext;
    spillcxt = AllocSetContextCreate(TopMemoryContext,
                                     "SpockSpillContext",
                                     ALLOCSET_DEFAULT_SIZES);
    MemoryContextSwitchTo(spillcxt);

    Spill = palloc0(sizeof(SpillState));
    Spill->ctx = spillcxt;
    Spill->file = NULL;
    Spill->reader_ready = false;
    Spill->frames_written = 0;
    Spill->bytes_written = 0;
    Spill->frames_read = 0;
    Spill->bytes_read = 0;

    	/* Ensure required backend/xact state exists for temp-file creation */
	if (!IsTransactionState())
	{
		started_tx = true;
		StartTransactionCommand();
	}

	PG_TRY();
	{
		/* interXact=true so the file survives across tx boundaries */
		Spill->file = BufFileCreateTemp(true);
	}
	PG_CATCH();
	{
		if (started_tx)
			AbortOutOfAnyTransaction();
		MemoryContextSwitchTo(oldcxt);
		spill_cleanup();
		ereport(WARNING,
				(errmsg("SPOCK spill: failed to create temp file")));
		FlushErrorState();
		return false;
	}
	PG_END_TRY();

	if (started_tx)
		CommitTransactionCommand();

    if (Spill->file == NULL)
    {
        MemoryContextSwitchTo(oldcxt);
        spill_cleanup();
        ereport(WARNING,
                (errmsg("SPOCK spill: failed to create temp file (NULL)")));
        return false;
    }

    	MemoryContextSwitchTo(oldcxt);
	elog(LOG, "SPOCK spill: initialized spill temp file");
	return true;
}

bool
spock_spill_active(void)
{
    	return (Spill != NULL &&
			Spill->ctx != NULL &&
			Spill->file != NULL);
}

bool
spock_spill_write_frame(const char *data, Size len)
{
    uint32		len32;
    uint32		limit;

    	if (!spock_spill_active())
	{
		elog(LOG, "SPOCK spill: write skipped, spill inactive");
		return false;
	}

    if (data == NULL && len > 0)
    {
        ereport(WARNING,
                (errmsg("SPOCK spill: NULL data with non-zero length")));
        return false;
    }

    /* Guard: frame must fit both uint32 and StringInfo limits */
    limit = max_frame_len();
    if (len > (Size) limit)
    {
        ereport(WARNING,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("SPOCK spill: frame too large to write: %zu bytes (max %u)",
                        (size_t) len, limit)));
        return false;
    }

    len32 = (uint32) len;

    elog(LOG, "SPOCK spill: writing frame header len=%u", len32);

    /* Write length header then payload */
    if (!buf_write_all(Spill->file, &len32, sizeof(uint32)))
        return false;

    if (len32 > 0)
    {
        elog(LOG, "SPOCK spill: writing frame payload bytes=%u", len32);
        if (!buf_write_all(Spill->file, data, (size_t) len32))
            return false;
    }

    Spill->frames_written++;
    Spill->bytes_written += len32 + (uint32) sizeof(uint32);

    	return true;
}

void
spock_spill_flush_buffer(void)
{
    	/*
	 * BufFileWrite pushes through its own buffers as needed, and BufFile
     * flushes on direction changes / seek. Force a no-op seek to ensure
     * buffered data is committed to the underlying VFD layer.
     */
    	if (!spock_spill_active())
		return;

    PG_TRY();
    {
        if (BufFileSeek(Spill->file, 0, 0, SEEK_CUR) != 0)
            ereport(WARNING,
                    (errmsg("SPOCK spill: flush seek failed")));
    }
    PG_CATCH();
    {
        ereport(WARNING,
                (errmsg("SPOCK spill: flush failed")));
        FlushErrorState();
    }
    PG_END_TRY();

    }

bool
spock_spill_reader_start(void)
{
    	if (!spock_spill_active())
		return false;

    spock_spill_flush_buffer(); /* ensure writes are visible to reads */

    PG_TRY();
    {
        if (BufFileSeek(Spill->file, 0, 0, SEEK_SET) != 0)
        {
            ereport(WARNING,
                    (errmsg("SPOCK spill: seek to start failed")));
            return false;
        }
    }
    PG_CATCH();
    {
        ereport(WARNING,
                (errmsg("SPOCK spill: seek to start failed (I/O error)")));
        FlushErrorState();
        return false;
    }
    PG_END_TRY();

    Spill->reader_ready = true;
    	elog(LOG, "SPOCK spill: reader initialized (frames_written=" UINT64_FORMAT ", bytes_written=" UINT64_FORMAT ")",
		 Spill->frames_written, Spill->bytes_written);
	return true;
}

bool
spock_spill_reader_next(StringInfo out)
{
    uint32		len32;
    bool		eof = false;
    uint32		limit;

    	if (!spock_spill_active() || !Spill->reader_ready || out == NULL)
	{
		elog(LOG, "SPOCK spill: reader_next: inactive=%d, ready=%d, out=%p",
			 spock_spill_active(), Spill ? Spill->reader_ready : 0, (void *) out);
		return false;
	}

    /* Prepare StringInfo */
    if (out->data == NULL)
        initStringInfo(out);
    else
        resetStringInfo(out);

    /* Read the length header */
    {
        size_t	n = 0;

        PG_TRY();
        {
            n = BufFileRead(Spill->file, &len32, sizeof(uint32));
        }
        PG_CATCH();
        {
            ereport(WARNING,
                    (errmsg("SPOCK spill: failed to read frame length")));
            FlushErrorState();
            return false;
        }
        PG_END_TRY();

        if (n == 0)
        {
            elog(LOG, "SPOCK spill: reader_next: EOF on header "
                      "(frames_read=" UINT64_FORMAT ", bytes_read=" UINT64_FORMAT ")",
                 Spill->frames_read, Spill->bytes_read);
            return false; /* clean EOF: no more frames */
        }

        if (n != sizeof(uint32))
        {
            ereport(WARNING,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("SPOCK spill: short read on frame length, expected %zu got %zu",
                            (sizeof(uint32)), n)));
            return false;
        }

        elog(LOG, "SPOCK spill: reader_next: header len32=%u", len32);
    }

    /* Sanity/overflow checks before allocating/reading payload */
    limit = max_frame_len();
    if (len32 > limit)
    {
        ereport(WARNING,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("SPOCK spill: frame too large: %u (max %u)", len32, limit)));
        return false;
    }

    if (len32 > 0)
    {
        /* Ensure there is room for payload plus terminating '\0' */
        if (len32 > (uint32) (INT_MAX - 1))
        {
            ereport(WARNING,
                    (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                     errmsg("SPOCK spill: frame length exceeds StringInfo capacity: %u",
                            len32)));
            return false;
        }

        enlargeStringInfo(out, (int) len32);
        out->len = (int) len32;

        elog(LOG, "SPOCK spill: reader_next: reading payload bytes=%u", len32);

        if (!buf_read_all(Spill->file, out->data, (size_t) len32, &eof))
        {
            if (eof)
                ereport(WARNING,
                        (errcode(ERRCODE_DATA_CORRUPTED),
                         errmsg("SPOCK spill: unexpected EOF in frame payload")));
            else
                ereport(WARNING,
                        (errmsg("SPOCK spill: failed to read frame payload")));
            return false;
        }

        out->data[out->len] = '\0'; /* keep StringInfo contract */
        elog(LOG, "SPOCK spill: reader_next: payload read ok len=%d", out->len);
    }
    else
    {
        /* Zero-length frame: valid, keep out empty but terminated */
        if (out->maxlen == 0)
            enlargeStringInfo(out, 1);
        out->len = 0;
        out->data[0] = '\0';
        elog(LOG, "SPOCK spill: reader_next: zero-length frame");
    }

    Spill->frames_read++;
    Spill->bytes_read += (uint64) len32 + (uint64) sizeof(uint32);

    	return true;
}

void
spock_spill_end(void)
{
    	elog(LOG, "SPOCK spill: end (frames_written=" UINT64_FORMAT ", bytes_written=" UINT64_FORMAT
				   ", frames_read=" UINT64_FORMAT ", bytes_read=" UINT64_FORMAT ")",
		 Spill ? Spill->frames_written : 0,
		 Spill ? Spill->bytes_written : 0,
		 Spill ? Spill->frames_read : 0,
		 Spill ? Spill->bytes_read : 0);
	spill_cleanup();
}
// vim: set noet sw=4 ts=4 :
#include <systemd/sd-journal.h>
#include <syslog.h>

#include "postgres.h"
#include "fmgr.h"
#include "tcop/tcopprot.h"
#include "utils/elog.h"
#include "utils/memutils.h"

/**** Declarations */

PG_MODULE_MAGIC;

void _PG_init(void);
void _PG_fini(void);

static void do_emit_log(ErrorData *edata);
static void journal_emit_log(ErrorData *edata);

/**** Constants */

#define MAX_LOGLEVEL 23
#define MAX_FIELDS	 10 /* NB! Keep this in sync when adding fields! */

/* Maps pg log levels to syslog levels */
static const int loglevel_map[MAX_LOGLEVEL] = {
	[DEBUG5]	= LOG_DEBUG,
	[DEBUG4]	= LOG_DEBUG,
	[DEBUG3]	= LOG_DEBUG,
	[DEBUG2]	= LOG_DEBUG,
	[DEBUG1]	= LOG_DEBUG,
	[LOG]		= LOG_INFO,
	[COMMERROR]	= LOG_INFO,
	[INFO]		= LOG_INFO,
	[NOTICE]	= LOG_NOTICE,
	[WARNING]	= LOG_WARNING,
	[ERROR]		= LOG_ERR,
	[FATAL]		= LOG_CRIT,
	[PANIC]		= LOG_ALERT,
};

/**** Globals */

static emit_log_hook_type prev_emit_log_hook = NULL;
/* Just a safety measure to prevent recursive calls */
static int in_hook = 0;
/* If a failure occurs, just report the 1st time */
static int reported_failure = 0;

/**** Implementation */

void
_PG_init(void)
{
	prev_emit_log_hook = emit_log_hook;
	emit_log_hook = do_emit_log;
}

void
_PG_fini(void)
{
	if (emit_log_hook == do_emit_log)
		emit_log_hook = prev_emit_log_hook;
	/*
	 * If not, someone else didn't clean up properly. Better not to mess with
	 * it.
	 */
}

static void
do_emit_log(ErrorData *edata)
{
	/* Call any previous hooks */
	if (prev_emit_log_hook)
		prev_emit_log_hook(edata);

	if (in_hook == 0)
	{
		in_hook = 1;
		journal_emit_log(edata);
		in_hook = 0;
	}
}

static int
map_priority(int pg_prio)
{
	if (pg_prio >= MAX_LOGLEVEL || pg_prio < 0)
		/* Out of range -- log with EMERG so the message is noticed */
		return LOG_EMERG;

	/* Unknown log levels initialized to 0 which is LOG_EMERG */
	return loglevel_map[pg_prio];
}

static void
append_string(struct iovec *field, const char *key, const char *value)
{
	StringInfoData buf;

	initStringInfo(&buf);
	appendStringInfoString(&buf, key);
	appendStringInfoString(&buf, value);

	field->iov_base = buf.data;
	field->iov_len  = buf.len;
}

static void
append_fmt(struct iovec *field, const char *fmt, ...)
/* This extension allows gcc to check the format string */
__attribute__((format(PG_PRINTF_ATTRIBUTE, 2, 3)));

static void
append_fmt(struct iovec *field, const char *fmt, ...)
{
	va_list		args;
	StringInfoData buf;

	initStringInfo(&buf);
	va_start(args, fmt);
	appendStringInfoVA(&buf, fmt, args);
	va_end(args);

	field->iov_base = buf.data;
	field->iov_len  = buf.len;
}

static void
journal_emit_log(ErrorData *edata)
{
	MemoryContext oldcontext;
	int			ret;
	char	   *message_id = NULL;
	struct iovec fields[MAX_FIELDS];
	int			n = 0;

	if (!edata->output_to_server)
		return;

	oldcontext = MemoryContextSwitchTo(ErrorContext);

	/* Assign a MESSAGE_ID to statement logging */
	if (edata->hide_stmt && debug_query_string != NULL &&
		memcmp(edata->message, "statement: ", 11) == 0)
	{
		message_id = "a63699368b304b4cb51bce5644736306";
	}

	append_fmt(&fields[n++], "PRIORITY=%d", map_priority(edata->elevel));
	append_fmt(&fields[n++], "PGLEVEL=%d", edata->elevel);

	if (message_id)
		append_string(&fields[n++], "MESSAGE_ID=", message_id);

	if (edata->sqlerrcode)
		append_string(&fields[n++], "SQLSTATE=",
										 unpack_sql_state(edata->sqlerrcode));

	if (edata->message)
		append_string(&fields[n++], "MESSAGE=", edata->message);

	if (edata->detail_log)
		append_string(&fields[n++], "DETAIL=", edata->detail_log);
	else if (edata->detail)
		append_string(&fields[n++], "DETAIL=", edata->detail);

	if (edata->hint)
		append_string(&fields[n++], "HINT=", edata->hint);

	if (edata->internalquery)
		append_string(&fields[n++], "QUERY=", edata->internalquery);

	if (edata->context)
		append_string(&fields[n++], "CONTEXT=", edata->context);

	if (!edata->hide_stmt && debug_query_string)
		append_string(&fields[n++], "STATEMENT=", debug_query_string);

	if (n >= MAX_FIELDS)
	{
		/*
		 * Oops, we've probably overwritten something else on the stack!
		 * Report error and die.
		 */
		ereport(FATAL,
				(errmsg("pg_journal: too many log fields (%d >= %d)",
						n, MAX_FIELDS)));
	}

	ret = sd_journal_sendv(fields, n);

	if (ret < 0)
	{
		if (reported_failure == 0)
		{
			ereport(WARNING,
					(errmsg("pg_journal: failed logging message with "
							"%d fields: %s",
							n, strerror(-ret))));
			/* Prevent spamming the log if journal is failing */
			reported_failure = 1;
		}
	}

	MemoryContextSwitchTo(oldcontext);
}

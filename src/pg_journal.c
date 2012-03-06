// vim: set noet sw=4 ts=4 :
#include <systemd/sd-journal.h>
#include <syslog.h>

#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "libpq/libpq-be.h"
#include "tcop/tcopprot.h"
#include "utils/elog.h"
#include "utils/memutils.h"

/**** Declarations */

PG_MODULE_MAGIC;

void _PG_init(void);
void _PG_fini(void);

static void do_emit_log(ErrorData *edata);
static void journal_emit_log(ErrorData *edata);

/**** Globals */

static emit_log_hook_type prev_emit_log_hook = NULL;
/* If a failure occurs, report it to the server log the first time */
static bool reported_failure = false;
/* GUC pg_journal.omit_server_log = off */
static bool skip_server_log = false;

/**** Implementation */

/* Convinience wrapper for DefineCustomBoolVariable */
static void
DefineBoolVariable(const char *name, const char *short_desc, bool *value_addr)
{
	DefineCustomBoolVariable(name,
			short_desc,
			NULL,
			value_addr,
#if PG_VERSION_NUM >= 80400
			false,				/* bootValue since 8.4 */
			PGC_SUSET,
			0,
#else
			PGC_USERSET,		/* 8.3 only allows USERSET custom params */
#endif
#if PG_VERSION_NUM >= 90100
			NULL,				/* check_hook parameter since 9.1 */
#endif
			NULL,
			NULL);
}

void
_PG_init(void)
{
	prev_emit_log_hook = emit_log_hook;
	emit_log_hook = do_emit_log;

	DefineBoolVariable("pg_journal.skip_server_log",
			"Skip messages from server log if journal logging succeeds",
			&skip_server_log);
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
	static bool in_hook = false;

	/* Call any previous hooks */
	if (prev_emit_log_hook)
		prev_emit_log_hook(edata);

	/* Protect from recursive calls */
	if (! in_hook)
	{
		in_hook = true;
		journal_emit_log(edata);
		in_hook = false;
	}
}

static int
elevel_to_syslog(int elevel)
{
	/* See utils/error/elog.c function send_message_to_server_log */
	switch (elevel)
	{
		case DEBUG5:
		case DEBUG4:
		case DEBUG3:
		case DEBUG2:
		case DEBUG1:
			return LOG_DEBUG;
		case LOG:
		case COMMERROR:
		case INFO:
			return LOG_INFO;
		case NOTICE:
		case WARNING:
			return LOG_NOTICE;
		case ERROR:
			return LOG_WARNING;
		case FATAL:
			return LOG_ERR;
		case PANIC:
		default:
			return LOG_CRIT;
	}
}

/*
 * This is a slight abuse of the StringInfo system. We're simply concatenating
 * together lots of fields and taking their pointers and lengths.
 *
 * This is better than using a separate StringInfo for each field, since
 * each StringInfo consumes 1024 bytes by default. A typical user message, 12
 * fields, would then consume 12 kilobytes minimum!
 */
static void
append_string(StringInfo str, struct iovec *field, const char *key, const char *value)
{
	size_t old_len = str->len;

	appendStringInfoString(str, key);
	appendStringInfoString(str, value);

	field->iov_base = &str->data[old_len];
	field->iov_len  = str->len - old_len;
}

static void
append_fmt(StringInfo str, struct iovec *field, const char *fmt, ...)
/* This extension allows gcc to check the format string */
__attribute__((format(PG_PRINTF_ATTRIBUTE, 3, 4)));

static void
append_fmt(StringInfo str, struct iovec *field, const char *fmt, ...)
{
	size_t old_len = str->len;
	va_list args;
	bool success;

	/* appendStringInfoVA can fail due to insufficient space */
	while (1) {
		va_start(args, fmt);
		success = appendStringInfoVA(str, fmt, args);
		va_end(args);

		if (success)
			break;

		enlargeStringInfo(str, 256);
	}

	field->iov_base = &str->data[old_len];
	field->iov_len  = str->len - old_len;
}

#define MAX_FIELDS	 17 /* NB! Keep this in sync when adding fields! */

static void
journal_emit_log(ErrorData *edata)
{
	struct iovec fields[MAX_FIELDS];
	MemoryContext oldcontext;
	StringInfoData buf;
	int			ret;
	int			n = 0;

	if (!edata->output_to_server)
		return;

	/* We should already be in ErrorContext, but just make 100% sure */
	oldcontext = MemoryContextSwitchTo(ErrorContext);

	initStringInfo(&buf);

	/* Assign a MESSAGE_ID to statement logging */
	if (edata->hide_stmt && debug_query_string != NULL &&
		memcmp(edata->message, "statement: ", 11) == 0)
	{
		append_string(&buf, &fields[n++], "MESSAGE_ID=",
				"a63699368b304b4cb51bce5644736306");
	}

	append_fmt(&buf, &fields[n++], "PRIORITY=%d", elevel_to_syslog(edata->elevel));
	append_fmt(&buf, &fields[n++], "PGLEVEL=%d", edata->elevel);

	if (edata->sqlerrcode)
		append_string(&buf, &fields[n++], "SQLSTATE=",
										 unpack_sql_state(edata->sqlerrcode));

	if (edata->message)
		append_string(&buf, &fields[n++], "MESSAGE=", edata->message);

	if (edata->detail_log)
		append_string(&buf, &fields[n++], "DETAIL=", edata->detail_log);
	else if (edata->detail)
		append_string(&buf, &fields[n++], "DETAIL=", edata->detail);

	if (edata->hint)
		append_string(&buf, &fields[n++], "HINT=", edata->hint);

	if (edata->internalquery)
		append_string(&buf, &fields[n++], "QUERY=", edata->internalquery);

	if (edata->context)
		append_string(&buf, &fields[n++], "CONTEXT=", edata->context);

	if (!edata->hide_stmt && debug_query_string)
		append_string(&buf, &fields[n++], "STATEMENT=", debug_query_string);

	/*
	 * These field names are also used by systemd itself. Not sure how useful
	 * they are in practice.
	 */
	if (edata->filename)
		append_string(&buf, &fields[n++], "CODE_FILE=", edata->filename);
	if (edata->lineno > 0)
		append_fmt(&buf, &fields[n++],    "CODE_LINE=%d", edata->lineno);
	if (edata->funcname)
		append_string(&buf, &fields[n++], "CODE_FUNCTION=", edata->funcname);

	/*
	 * Non-ErrorData fields. These field names are modeled after libpq
	 * environment vars:
	 * http://www.postgresql.org/docs/current/static/libpq-envars.html
	 */
	if (MyProcPort)
	{
		if (MyProcPort->user_name)
			append_string(&buf, &fields[n++], "PGUSER=", MyProcPort->user_name);

		if (MyProcPort->database_name)
			append_string(&buf, &fields[n++], "PGDATABASE=", MyProcPort->database_name);

		if (MyProcPort->remote_host && MyProcPort->remote_port &&
			MyProcPort->remote_port[0] != '\0')
		{
			append_fmt(&buf, &fields[n++], "PGHOST=%s:%s", MyProcPort->remote_host,
					   MyProcPort->remote_port);
		}
		else if (MyProcPort->remote_host)
			append_string(&buf, &fields[n++], "PGHOST=", MyProcPort->remote_host);
	}

	if (application_name)
		append_string(&buf, &fields[n++], "PGAPPNAME=", application_name);

	/* Done collecting fields. */
	if (n > MAX_FIELDS)
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

	if (ret >= 0)
	{
		/* Successfully logged */
		if (skip_server_log)
			edata->output_to_server = false;
	}
	else
	{
		if (! reported_failure)
		{
			ereport(WARNING,
					(errmsg("pg_journal: failed logging message with "
							"%d fields: %s",
							n, strerror(-ret))));
			/* Prevent spamming the log if journal is failing */
			reported_failure = true;
		}
	}

	MemoryContextSwitchTo(oldcontext);
}

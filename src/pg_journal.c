// vim: set noet sw=4 ts=4 :
/* We override CODE_FILE= etc fields, don't let systemd add these */
#define SD_JOURNAL_SUPPRESS_LOCATION 1

#include <systemd/sd-journal.h>
#include <syslog.h>

#include "postgres.h"
#include "miscadmin.h"
#include "lib/stringinfo.h"
#include "libpq/libpq-be.h"
#include "tcop/tcopprot.h"
#include "utils/elog.h"
#include "utils/memutils.h"

/**** Version detection */

#ifdef __GNUC__
# if PG_VERSION_NUM < 90200
/*
 * There's no way to detect whether the patch was already applied, so this is
 * just a warning.
 */
#  warning "Building on PostgreSQL version earlier than 9.2. If the build fails, you"
#  warning "need to patch the PostgreSQL server first. You can get the patch from:"
#  warning "https://raw.github.com/intgr/pg_journal/master/patches/logging-hooks.patch"
# endif
#endif

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
/* GUC pg_journal.passthrough_server_log = off */
static bool passthrough_server_log = false;
/* Cache syslog_ident */
static char *syslog_ident = NULL;

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
	MemoryContext oldcontext;

	prev_emit_log_hook = emit_log_hook;
	emit_log_hook = do_emit_log;

	DefineBoolVariable("pg_journal.passthrough_server_log",
			"Duplicate messages to the server log even if journal logging succeeds",
			&passthrough_server_log);

	/*
	 * We don't want to perform this GUC lookup for each log message. Sadly
	 * there is no nice way to get notified when this changes.
	 */
	oldcontext = MemoryContextSwitchTo(TopMemoryContext);
	syslog_ident = strdup(GetConfigOption("syslog_ident", false, false));
	MemoryContextSwitchTo(oldcontext);
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
 * error_severity --- get localized string representing elevel
 * See utils/error/elog.c function error_severity
 */
static const char *
error_severity(int elevel)
{
	switch (elevel)
	{
		case DEBUG1:
		case DEBUG2:
		case DEBUG3:
		case DEBUG4:
		case DEBUG5:
			return _("DEBUG");
		case LOG:
		case COMMERROR:
			return _("LOG");
		case INFO:
			return _("INFO");
		case NOTICE:
			return _("NOTICE");
		case WARNING:
			return _("WARNING");
		case ERROR:
			return _("ERROR");
		case FATAL:
			return _("FATAL");
		case PANIC:
			return _("PANIC");
		default:
			return "???";
	}
}

/*
 * This is a slight abuse of the StringInfo system. We're simply concatenating
 * together lots of fields and storing their lengths. Once the whole string
 * is ready, we get pointers based on the lengths.
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

	field->iov_len = str->len - old_len;
}

static void
append_string3(StringInfo str, struct iovec *field, const char *key,
			   const char *s1, const char *s2, const char *s3)
{
	size_t old_len = str->len;

	appendStringInfoString(str, key);
	appendStringInfoString(str, s1);
	appendStringInfoString(str, s2);
	appendStringInfoString(str, s3);

	field->iov_len = str->len - old_len;
}

static void
append_fmt(StringInfo str, struct iovec *field, const char *fmt, ...)
/* This extension allows gcc to check the format string */
__attribute__((format(PG_PRINTF_ATTRIBUTE, 3, 4)));

/* See backend/lib/stringinfo.c function appendStringInfo */
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

		/* Double the buffer size and try again. */
		enlargeStringInfo(str, str->maxlen);
	}

	field->iov_len = str->len - old_len;
}

#define MAX_FIELDS	 23 /* NB! Keep this in sync when adding fields! */

static void
journal_emit_log(ErrorData *edata)
{
	struct iovec fields[MAX_FIELDS];
	MemoryContext oldcontext;
	StringInfoData buf;
	int			ret;
	int			i;
	int			n = 0;
	char	   *ptr;

	if (!edata->output_to_server)
		return;

	/* We should already be in ErrorContext, but just make 100% sure */
	oldcontext = MemoryContextSwitchTo(ErrorContext);

	initStringInfo(&buf);

	/* Assign a MESSAGE_ID to log_statement logging */
	if (edata->hide_stmt && debug_query_string != NULL &&
		memcmp(edata->message, "statement: ", 11) == 0)
	{
		append_string(&buf, &fields[n++], "MESSAGE_ID=",
				"a63699368b304b4cb51bce5644736306");
	}

	if (edata->message)
		append_string3(&buf, &fields[n++], "MESSAGE=",
										   error_severity(edata->elevel),
										   ":  ",
										   edata->message);

	append_fmt(&buf, &fields[n++], "PRIORITY=%d", elevel_to_syslog(edata->elevel));
	append_fmt(&buf, &fields[n++], "PGLEVEL=%d", edata->elevel);

	if (edata->sqlerrcode)
		append_string(&buf, &fields[n++], "SQLSTATE=",
										 unpack_sql_state(edata->sqlerrcode));

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

#if PG_VERSION_NUM >= 90300
	if (edata->schema_name)
		append_string(&buf, &fields[n++], "SCHEMA=", edata->schema_name);
	if (edata->table_name)
		append_string(&buf, &fields[n++], "TABLE=", edata->table_name);
	if (edata->column_name)
		append_string(&buf, &fields[n++], "COLUMN=", edata->column_name);
	if (edata->datatype_name)
		append_string(&buf, &fields[n++], "DATATYPE=", edata->datatype_name);
	if (edata->constraint_name)
		append_string(&buf, &fields[n++], "CONSTRAINT=", edata->constraint_name);
#endif /* PG_VERSION_NUM >= 90300 */

	/*
	 * These fields are normally added by systemd itself, but we override them
	 * to contain the actual PostgreSQL logging call. Not sure how useful they
	 * are in practice.
	 */
#ifdef SD_JOURNAL_SUPPRESS_LOCATION
	if (edata->filename)
		append_string(&buf, &fields[n++], "CODE_FILE=", edata->filename);
	if (edata->lineno > 0)
		append_fmt(&buf, &fields[n++],    "CODE_LINE=%d", edata->lineno);
	if (edata->funcname)
		append_string(&buf, &fields[n++], "CODE_FUNCTION=", edata->funcname);
#endif /* SD_JOURNAL_SUPPRESS_LOCATION */

	/*
	 * Non-ErrorData fields. These field names are named after libpq
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
			append_string3(&buf, &fields[n++], "PGHOST=",
											   MyProcPort->remote_host,
											   ":",
											   MyProcPort->remote_port);
		}
		else if (MyProcPort->remote_host)
			append_string(&buf, &fields[n++], "PGHOST=", MyProcPort->remote_host);
	}

	if (application_name && application_name[0] != '\0')
		append_string(&buf, &fields[n++], "PGAPPNAME=", application_name);

	append_string(&buf, &fields[n++], "SYSLOG_IDENTIFIER=", syslog_ident);

	if (n > MAX_FIELDS)
	{
		/*
		 * Oops, someone forgot to update MAX_FIELDS definition!
		 * Report error and die.
		 */
		ereport(FATAL,
				(errmsg("pg_journal: too many log fields (%d >= %d)",
						n, MAX_FIELDS)));
	}

	/*
	 * Done writing fields. Need to extract pointers to individual items, by
	 * following field lengths. We couldn't do that before, since the string's
	 * base address can move due to reallocations.
	 */
	ptr = buf.data;
	for(i = 0; i < n; i++)
	{
		fields[i].iov_base = ptr;
		ptr += fields[i].iov_len;
	}

	ret = sd_journal_sendv(fields, n);

	if (ret >= 0)
	{
		/* Successfully logged */
		if (! passthrough_server_log)
			edata->output_to_server = false;
	}
	else
	{
		if (! reported_failure)
		{
			ereport(WARNING,
					(errmsg("pg_journal: could not log message with %d fields: %s",
							n, strerror(-ret))));
			/* Prevent spamming the log on subsequent failures */
			reported_failure = true;
		}
	}

	MemoryContextSwitchTo(oldcontext);
}

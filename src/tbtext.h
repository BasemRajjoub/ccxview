/* tbtext.h -- the title block's text as a template: one line per row, "Label: value"
   (the part before the first ": " is the label column; a line without one spans
   the block), with placeholders such as {file} or {date} filled in from a table
   every time it is drawn, so live values stay live after the text is edited by
   hand. `{{` writes a brace; an unknown {name} stays as typed. A line whose
   placeholders all come out empty is left out, as is a blank line. The dates
   take strftime formats, with %-d / %-m / %-H for no leading zero. Also the
   escaping that keeps the multi-line text on one ini line. Headless. */
#ifndef CV_TBTEXT_H
#define CV_TBTEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef struct { const char* key; const char* val; } cv_tb_kv;

typedef struct {
    const cv_tb_kv*  kv; int n;   /* the placeholders and their values ("" for none) */
    const char*      date_fmt;    /* {date} and {date_file}; NULL or "": %Y-%m-%d */
    const struct tm* now;         /* {date}, {time_now}; NULL: empty */
    const struct tm* file;        /* {date_file}, when the result file was written; NULL: empty */
} cv_tb_ctx;

/* the date formats offered in a list, and what "with the time" adds to one */
enum { CV_TB_DATE_N = 6 };
extern const char* const cv_tb_date_fmt[CV_TB_DATE_N];
#define CV_TB_TIME_FMT " %H:%M"

/* one template line (len chars, no newline) filled in: the text into out, *split
   the offset in out where the label's ": " starts (-1: no label, the line spans).
   false when the line is left out. */
bool cv_tb_line(const char* line, size_t len, const cv_tb_ctx* c, char* out, size_t n, int* split);

/* strftime with a portable set of conversions (the C89 ones, %e %F %T %R %D, and
   %-x without the leading zero); an unknown one is copied as typed. The length. */
size_t cv_tb_strftime(char* out, size_t n, const char* fmt, const struct tm* t);

/* text that shows as typed in a template: every '{' doubled */
void cv_tb_literal(const char* in, char* out, size_t n);

/* the template on one ini line and back: \n, \t, \\ and a space at either end as \s */
void cv_tb_escape(const char* in, char* out, size_t n);
void cv_tb_unescape(const char* in, char* out, size_t n);
/* where an escaped text longer than max may be cut into ini values: neither piece
   starts or ends with a space (the ini trims them) nor splits an escape */
size_t cv_tb_cut(const char* s, size_t max);

#endif

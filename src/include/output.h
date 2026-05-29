/*
 * loc_output.h — pluggable output formatters for mini-loc
 *
 * Supports four output formats:
 *   LOC_FMT_TERMINAL  — coloured ANSI table (original behaviour, default)
 *   LOC_FMT_JSON      — machine-readable JSON
 *   LOC_FMT_HTML      — self-contained HTML page with an inline style sheet
 *   LOC_FMT_SQL       — INSERT statements compatible with SQLite / PostgreSQL
 *
 * ── SQL SCHEMA ───────────────────────────────────────────────────────────────
 *
 *  CREATE TABLE IF NOT EXISTS loc_languages (
 *      run_id   TEXT    NOT NULL,
 *      language TEXT    NOT NULL,
 *      files    INTEGER NOT NULL,
 *      code     INTEGER NOT NULL,
 *      comment  INTEGER NOT NULL,
 *      blank    INTEGER NOT NULL,
 *      total    INTEGER NOT NULL,
 *      pct      REAL    NOT NULL,
 *      PRIMARY KEY (run_id, language)
 *  );
 *
 *  CREATE TABLE IF NOT EXISTS loc_files (
 *      run_id   TEXT    NOT NULL,
 *      path     TEXT    NOT NULL,
 *      ext      TEXT,
 *      language TEXT,
 *      code     INTEGER NOT NULL,
 *      comment  INTEGER NOT NULL,
 *      blank    INTEGER NOT NULL,
 *      total    INTEGER NOT NULL,
 *      PRIMARY KEY (run_id, path)
 *  );
 *
 *  The run_id is an ISO-8601 UTC timestamp generated once per invocation.
 *
 ******************************************************************************/

#ifndef LOC_OUTPUT_H
#define LOC_OUTPUT_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "types.h"

/* Master dispatcher — call this instead of (or from within) print_report().
 *
 * Parameters
 *   fmt        — output format selected by the user
 *   files      — g_files array (may be NULL if n_files == 0)
 *   n_files    — number of valid entries in files[]
 *   langs      — g_langs array
 *   n_langs    — number of valid entries in langs[]
 *   params     — output parameters (show_files, verbose, no_bytes, total_bytes,
 * sort_order)
 */
static __attribute__((cold)) void loc_print_report(LocOutputFormat fmt,
 FileResult* files, int n_files, const Language* langs, int n_langs,
 LocOutputParams params);

/* Individual formatters — all static inline so they are inlined into the
 * single compilation unit that #includes this header, producing zero link-time
 * symbol conflicts between single and multi. */
static __attribute__((cold)) void loc_print_json(const FileResult* files,
 int n_files, const Language* langs, int n_langs, LocOutputParams params);
static __attribute__((cold)) void loc_print_html(const FileResult* files,
 int n_files, const Language* langs, int n_langs, LocOutputParams params);
static __attribute__((cold)) void loc_print_sql(const FileResult* files,
 int n_files, const Language* langs, int n_langs, LocOutputParams params);
static __attribute__((cold)) void loc_print_terminal(FileResult* files,
 int n_files, const Language* langs, int n_langs, LocOutputParams params);

/* Internal helpers */

/* Build the per-language summary table from a flat FileResult array.
 * Returns the number of entries written into out_sums (≤ max_sums).
 * Also writes grand totals into *t_files, *t_code, *t_comm, *t_blank. */

static __attribute__((cold)) int loc__build_sums(const FileResult* files_v,
 LocSumParams params, LocLangSum* out_sums, uint64_t* t_files, uint64_t* t_code,
 uint64_t* t_comm, uint64_t* t_blank, uint64_t* t_complexity);

static LocSortOrder g_sort_order = LOC_SORT_TOTAL;

/* qsort comparator — sort by total descending */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static inline __attribute__((cold)) int
loc__sum_cmp(const void* lhs, const void* rhs)
{
    const LocLangSum* la = (const LocLangSum*)lhs;
    const LocLangSum* lb = (const LocLangSum*)rhs;
    uint64_t val_a = 0, val_b = 0;

    switch (g_sort_order) {
    case LOC_SORT_CODE:
        val_a = la->counts.code;
        val_b = lb->counts.code;
        break;
    case LOC_SORT_COMMENT:
        val_a = la->counts.comment;
        val_b = lb->counts.comment;
        break;
    case LOC_SORT_BLANK:
        val_a = la->counts.blank;
        val_b = lb->counts.blank;
        break;
    case LOC_SORT_FILES:
        val_a = (uint64_t)la->files;
        val_b = (uint64_t)lb->files;
        break;
    case LOC_SORT_TOTAL:
    default:
        val_a =
         (uint64_t)la->counts.code + la->counts.comment + la->counts.blank;
        val_b =
         (uint64_t)lb->counts.code + lb->counts.comment + lb->counts.blank;
        break;
    }
    return (val_b > val_a) ? 1 : (val_b < val_a) ? -1 : 0;
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static inline __attribute__((cold)) int
loc__file_cmp(const void* lhs, const void* rhs)
{
    const FileResult* fa = (const FileResult*)lhs;
    const FileResult* fb = (const FileResult*)rhs;
    uint64_t val_a = 0, val_b = 0;

    switch (g_sort_order) {
    case LOC_SORT_CODE:
        val_a = fa->counts.code;
        val_b = fb->counts.code;
        break;
    case LOC_SORT_COMMENT:
        val_a = fa->counts.comment;
        val_b = fb->counts.comment;
        break;
    case LOC_SORT_BLANK:
        val_a = fa->counts.blank;
        val_b = fb->counts.blank;
        break;
    case LOC_SORT_TOTAL:
        val_a =
         (uint64_t)fa->counts.code + fa->counts.comment + fa->counts.blank;
        val_b =
         (uint64_t)fb->counts.code + fb->counts.comment + fb->counts.blank;
        break;
    case LOC_SORT_FILES:
        if (fa->path && fb->path) {
            return strcmp(fa->path, fb->path);
        }
        return 0;
    }
    return (val_b > val_a) ? 1 : (val_b < val_a) ? -1 : 0;
}

/* Escape a string for JSON: replace " -> \" and \ -> \\ in-place into buf. */
static __attribute__((cold)) void
loc__json_escape(const char* src, char* buf, size_t len)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 2 < len; i++) {
        if (src[i] == '"' || src[i] == '\\') {
            buf[j++] = '\\';
        }
        buf[j++] = src[i];
    }
    buf[j] = '\0';
}

/* Escape a string for HTML: replace &, <, >, " with entities. */
static __attribute__((cold)) void
loc__html_escape(const char* src, char* buf, size_t len)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 8 < len; i++) {
        switch (src[i]) {
        case '&':
            memcpy(buf + j, "&amp;", 5);
            j += 5;
            break;
        case '<':
            memcpy(buf + j, "&lt;", 4);
            j += 4;
            break;
        case '>':
            memcpy(buf + j, "&gt;", 4);
            j += 4;
            break;
        case '"':
            memcpy(buf + j, "&quot;", 6);
            j += 6;
            break;
        case '\'':
            memcpy(buf + j, "&#39;", 5);
            j += 5;
            break;
        default:
            buf[j++] = src[i];
            break;
        }
    }
    buf[j] = '\0';
}

/* SQL single-quote escaping: replace ' -> '' (ANSI SQL standard). */
static __attribute__((cold)) void
loc__sql_escape(const char* src, char* buf, size_t len)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 2 < len; i++) {
        if (src[i] == '\'') {
            buf[j++] = '\'';
        }
        buf[j++] = src[i];
    }
    buf[j] = '\0';
}

/* Generate an ISO-8601 UTC timestamp: "2025-05-12T14:30:00Z" */
static inline __attribute__((cold)) void loc__iso8601_now(char* buf, size_t len)
{
    time_t t = time(NULL);
    struct tm* gm = gmtime(&t);
    if (gm) {
        strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", gm);
    } else {
        strncpy(buf, "1970-01-01T00:00:00Z", len);
        buf[len - 1] = '\0';
    }
}

/* HTML helpers */
#define LOC_HTML_HEADER \
    "<!DOCTYPE html>\n" \
    "<html lang=\"en\">\n" \
    "<head>\n" \
    "<meta charset=\"utf-8\">\n" \
    "<title>mini-loc</title>\n" \
    "<style>\n" \
    "body{font-family:monospace;margin:20px;}\n" \
    "table{border-collapse:collapse;}\n" \
    "th,td{border:1px solid #ccc;padding:4px 8px;text-align:right;}\n" \
    "th:first-child,td:first-child{text-align:left;}\n" \
    "</style>\n" \
    "</head>\n" \
    "<body>\n"

#define LOC_HTML_FOOTER \
    "</body>\n" \
    "</html>\n"

/* Terminal helpers */
#define LOC_TERM_RESET "\033[0m"
#define LOC_TERM_CYAN "\033[36m"
#define LOC_TERM_GREEN "\033[32m"
#define LOC_TERM_YELLOW "\033[33m"
#define LOC_TERM_GRAY "\033[90m"
#define LOC_TERM_MAGENTA "\033[35m"

/* Build summary table */
static __attribute__((cold)) int loc__build_sums(const FileResult* files_v,
 LocSumParams params, LocLangSum* out_sums, uint64_t* t_files, uint64_t* t_code,
 uint64_t* t_comm, uint64_t* t_blank, uint64_t* t_complexity)
{
    /* lang_to_sum_idx maps (lang_idx+1) → position in out_sums.
     * Using a stack array since MAX_LANGS is small. */
    int map_size = params.num_langs + 2; /* +1 for the sentinel, +1 for
                                            unknown(-1) */
    int lang_to_sum[MAX_LANGS + 2];
    if (map_size > (int)(sizeof(lang_to_sum) / sizeof(int))) {
        return 0;
    }
    for (int i = 0; i < map_size; i++) {
        lang_to_sum[i] = -1;
    }

    int n_sums = 0;
    *t_files = *t_code = *t_comm = *t_blank = *t_complexity = 0;

    for (int i = 0; i < params.num_files; i++) {
        int li = (files_v + i)->lang_idx;
        int map_idx = li + 1; /* -1 → 0, 0 → 1, … */
        if (map_idx < 0 || map_idx >= map_size) {
            continue;
        }

        int found = lang_to_sum[map_idx];
        if (found == -1) {
            if (n_sums >= params.max_sums) {
                continue;
            }
            found = n_sums++;
            out_sums[found].lang_idx = li;
            out_sums[found].files = 0;
            out_sums[found].counts.code = 0;
            out_sums[found].counts.comment = 0;
            out_sums[found].counts.blank = 0;
            out_sums[found].counts.complexity = 0;
            lang_to_sum[map_idx] = found;
        }
        out_sums[found].files++;
        out_sums[found].counts.code += (files_v + i)->counts.code;
        out_sums[found].counts.comment += (files_v + i)->counts.comment;
        out_sums[found].counts.blank += (files_v + i)->counts.blank;
        out_sums[found].counts.complexity += (files_v + i)->counts.complexity;
    }

    g_sort_order = params.sort_order;
    qsort(out_sums, (size_t)n_sums, sizeof(LocLangSum), loc__sum_cmp);

    for (int i = 0; i < n_sums; i++) {
        *t_files += (uint64_t)out_sums[i].files;
        *t_code += out_sums[i].counts.code;
        *t_comm += out_sums[i].counts.comment;
        *t_blank += out_sums[i].counts.blank;
        *t_complexity += out_sums[i].counts.complexity;
    }
    return n_sums;
}

/*
 * JSON formatter
 */

static void loc_print_json(const FileResult* files_v, int n_files,
 const Language* langs_v, int n_langs, LocOutputParams params)
{
#define MAX_SUMS_JSON 1024
    LocLangSum sums[MAX_SUMS_JSON];
    uint64_t t_files = 0, t_code = 0, t_comm = 0, t_blank = 0, t_complexity = 0;

    int n_sums = loc__build_sums(files_v,
     (LocSumParams){n_files, n_langs, MAX_SUMS_JSON, params.sort_order}, sums,
     &t_files, &t_code, &t_comm, &t_blank, &t_complexity);
    uint64_t grand_total = t_code + t_comm + t_blank;

    char esc[1024];

    printf("{\n");

    /* ── languages array ── */
    printf("  \"languages\": [\n");
    for (int i = 0; i < n_sums; i++) {
        const char* name = (sums[i].lang_idx == -1) ?
         "(unknown)" :
         (langs_v + sums[i].lang_idx)->name;
        loc__json_escape(name, esc, sizeof(esc));
        uint64_t total = (uint64_t)sums[i].counts.code +
         sums[i].counts.comment + sums[i].counts.blank;
        double pct =
         (grand_total > 0) ? 100.0 * (double)total / (double)grand_total : 0.0;
        printf(
         "    {\n"
         "      \"language\": \"%s\",\n"
         "      \"files\": %d,\n"
         "      \"code\": %" PRIu32 ",\n"
         "      \"comment\": %" PRIu32 ",\n"
         "      \"blank\": %" PRIu32 ",\n",
         esc, sums[i].files, sums[i].counts.code, sums[i].counts.comment,
         sums[i].counts.blank);
        if (params.show_complexity) {
            printf("      \"complexity\": %" PRIu32 ",\n",
             sums[i].counts.complexity);
        }
        printf("      \"total\": %" PRIu64 ",\n"
               "      \"pct\": %.2f\n"
               "    }%s\n",
         total, pct, (i < n_sums - 1) ? "," : "");
    }
    printf("  ],\n");

    /* ── totals ── */
    printf(
     "  \"totals\": {\n"
     "    \"files\": %" PRIu64 ",\n"
     "    \"code\": %" PRIu64 ",\n"
     "    \"comment\": %" PRIu64 ",\n"
     "    \"blank\": %" PRIu64 ",\n",
     t_files, t_code, t_comm, t_blank);
    if (params.show_complexity) {
        printf("    \"complexity\": %" PRIu64 ",\n", t_complexity);
    }
    printf("    \"total lines\": %" PRIu64 "", grand_total);
    if (!params.no_bytes) {
        printf(",\n    \"bytes\": %zu", params.total_bytes);
    }
    printf("\n  }");

    /* ── per-file results (optional) ── */
    if (params.show_files && n_files > 0) {
        printf(",\n  \"files\": [\n");
        for (int i = 0; i < n_files; i++) {
            const char* path = (files_v + i)->path;
            const char* ext = (files_v + i)->ext;
            int li = (files_v + i)->lang_idx;
            const char* lang_name =
             (li >= 0 && li < n_langs) ? (langs_v + li)->name : "(unknown)";

            char esc_path[4096], esc_lang[128];
            loc__json_escape(path ? path : "", esc_path, sizeof(esc_path));
            loc__json_escape(lang_name, esc_lang, sizeof(esc_lang));

            uint32_t code = (files_v + i)->counts.code;
            uint32_t comment = (files_v + i)->counts.comment;
            uint32_t blank = (files_v + i)->counts.blank;
            uint32_t complexity = (files_v + i)->counts.complexity;
            uint32_t total = code + comment + blank;

            printf(
             "    {\n"
             "      \"path\": \"%s\",\n"
             "      \"ext\": \"%s\",\n"
             "      \"language\": \"%s\",\n"
             "      \"code\": %" PRIu32 ",\n"
             "      \"comment\": %" PRIu32 ",\n"
             "      \"blank\": %" PRIu32 ",\n",
             esc_path, ext ? ext : "", esc_lang, code, comment, blank);
            if (params.show_complexity) {
                printf("      \"complexity\": %" PRIu32 ",\n", complexity);
            }
            printf("      \"total\": %" PRIu32 "\n"
                   "    }%s\n",
             total, (i < n_files - 1) ? "," : "");
        }
        printf("  ]");
    }

    printf("\n}\n");
#undef MAX_SUMS_JSON
}

/*
 * HTML formatter
 */

static void loc_print_html(const FileResult* files_v, int n_files,
 const Language* langs_v, int n_langs, LocOutputParams params)
{
#define MAX_SUMS_HTML 1024

    LocLangSum* sums = (LocLangSum*)calloc(MAX_SUMS_HTML, sizeof(LocLangSum));

    if (!sums) {
        return;
    }

    uint64_t t_files = 0;
    uint64_t t_code = 0;
    uint64_t t_comment = 0;
    uint64_t t_blank = 0;
    uint64_t t_complexity = 0;

    int n_sums = loc__build_sums(files_v,
     (LocSumParams){n_files, n_langs, MAX_SUMS_HTML, params.sort_order}, sums,
     &t_files, &t_code, &t_comment, &t_blank, &t_complexity);

    uint64_t grand_total = t_code + t_comment + t_blank;

    char esc[4096];

    printf("%s", LOC_HTML_HEADER);

    printf(
     "<table>\n"
     "<thead>\n"
     "<tr>\n"
     "<th>Language</th>\n"
     "<th>Files</th>\n"
     "<th>Code</th>\n"
     "<th>Comment</th>\n"
     "<th>Blank</th>\n");
    if (params.show_complexity) {
        printf("<th>Complexity</th>\n");
    }
    printf(
     "<th>Total</th>\n"
     "<th>%%</th>\n"
     "</tr>\n"
     "</thead>\n"
     "<tbody>\n");
    for (int i = 0; i < n_sums; i++) {
        const char* name = (sums[i].lang_idx == -1) ?
         "(unknown)" :
         (langs_v + sums[i].lang_idx)->name;

        loc__html_escape(name, esc, sizeof(esc));

        uint64_t total = (uint64_t)sums[i].counts.code +
         sums[i].counts.comment + sums[i].counts.blank;

        double pct = (grand_total > 0) ?
         (100.0 * (double)total / (double)grand_total) :
         0.0;

        printf(
         "<tr>"
         "<td>%s</td>"
         "<td>%d</td>"
         "<td>%" PRIu32 "</td>"
         "<td>%" PRIu32 "</td>"
         "<td>%" PRIu32 "</td>",
         esc, sums[i].files, sums[i].counts.code, sums[i].counts.comment,
         sums[i].counts.blank);
        if (params.show_complexity) {
            printf("<td>%" PRIu32 "</td>", sums[i].counts.complexity);
        }
        printf("<td>%" PRIu64 "</td>"
               "<td>%.1f</td>"
               "</tr>\n",
         total, pct);
    }

    printf(
     "<tr>"
     "<td><b>TOTAL</b></td>"
     "<td><b>%" PRIu64 "</b></td>"
     "<td><b>%" PRIu64 "</b></td>"
     "<td><b>%" PRIu64 "</b></td>"
     "<td><b>%" PRIu64 "</b></td>",
     t_files, t_code, t_comment, t_blank);
    if (params.show_complexity) {
        printf("<td><b>%" PRIu64 "</b></td>", t_complexity);
    }
    printf("<td><b>%" PRIu64 "</b></td>"
           "<td><b>100.0</b></td>"
           "</tr>\n",
     grand_total);

    if (!params.no_bytes) {
        printf(
         "<tr>"
         "<td><b>TOTAL BYTES</b></td>"
         "<td colspan=\"6\"><b>%zu</b></td>"
         "</tr>\n",
         params.total_bytes);
    }

    printf("</tbody>\n</table>\n");

    if (params.show_files && n_files > 0) {
        printf(
         "<br>\n"
         "<table>\n"
         "<thead>\n"
         "<tr>\n"
         "<th>Path</th>\n"
         "<th>Code</th>\n"
         "<th>Comment</th>\n"
         "<th>Blank</th>\n");
        if (params.show_complexity) {
            printf("<th>Complexity</th>\n");
        }
        printf(
         "<th>Total</th>\n"
         "</tr>\n"
         "</thead>\n"
         "<tbody>\n");

        for (int i = 0; i < n_files; i++) {
            const char* path = (files_v + i)->path;

            uint32_t code = (files_v + i)->counts.code;
            uint32_t comment = (files_v + i)->counts.comment;
            uint32_t blank = (files_v + i)->counts.blank;
            uint32_t complexity = (files_v + i)->counts.complexity;

            uint32_t total = code + comment + blank;

            loc__html_escape(path ? path : "", esc, sizeof(esc));

            printf(
             "<tr>"
             "<td>%s</td>"
             "<td>%" PRIu32 "</td>"
             "<td>%" PRIu32 "</td>"
             "<td>%" PRIu32 "</td>",
             esc, code, comment, blank);
            if (params.show_complexity) {
                printf("<td>%" PRIu32 "</td>", complexity);
            }
            printf("<td>%" PRIu32 "</td>"
                   "</tr>\n",
             total);
        }

        printf("</tbody>\n</table>\n");
    }

    printf("%s", LOC_HTML_FOOTER);

    free(sums);

#undef MAX_SUMS_HTML
}

/*
 * SQL formatter
 */

static void loc_print_sql(const FileResult* files_v, int n_files,
 const Language* langs_v, int n_langs, LocOutputParams params)
{
#define MAX_SUMS_SQL 1024
    LocLangSum sums[MAX_SUMS_SQL];
    uint64_t t_files = 0, t_code = 0, t_comm = 0, t_blank = 0, t_complexity = 0;

    int n_sums = loc__build_sums(files_v,
     (LocSumParams){n_files, n_langs, MAX_SUMS_SQL, params.sort_order}, sums,
     &t_files, &t_code, &t_comm, &t_blank, &t_complexity);

    uint64_t grand_total = t_code + t_comm + t_blank;

    char ts[32];
    loc__iso8601_now(ts, sizeof(ts));

    char esc[4096];

    /* ── DDL ── */
    printf("-- mini-loc SQL export — run at %s\n\n", ts);

    printf(
     "CREATE TABLE IF NOT EXISTS loc_languages (\n"
     "    run_id   TEXT    NOT NULL,\n"
     "    language TEXT    NOT NULL,\n"
     "    files    INTEGER NOT NULL,\n"
     "    code     INTEGER NOT NULL,\n"
     "    comment  INTEGER NOT NULL,\n"
     "    blank    INTEGER NOT NULL,\n");
    if (params.show_complexity) {
        printf("    complexity INTEGER NOT NULL,\n");
    }
    printf(
     "    total    INTEGER NOT NULL,\n"
     "    pct      REAL    NOT NULL,\n"
     "    PRIMARY KEY (run_id, language)\n"
     ");\n\n");

    if (params.show_files) {
        printf(
         "CREATE TABLE IF NOT EXISTS loc_files (\n"
         "    run_id   TEXT    NOT NULL,\n"
         "    path     TEXT    NOT NULL,\n"
         "    ext      TEXT,\n"
         "    language TEXT,\n"
         "    code     INTEGER NOT NULL,\n"
         "    comment  INTEGER NOT NULL,\n"
         "    blank    INTEGER NOT NULL,\n");
        if (params.show_complexity) {
            printf("    complexity INTEGER NOT NULL,\n");
        }
        printf("    total    INTEGER NOT NULL,\n"
               "    PRIMARY KEY (run_id, path)\n"
               ");\n\n");
    }

    /* ── Language rows ── */
    printf("-- Language summary\n");
    for (int i = 0; i < n_sums; i++) {
        const char* name = (sums[i].lang_idx == -1) ?
         "(unknown)" :
         (langs_v + sums[i].lang_idx)->name;
        loc__sql_escape(name, esc, sizeof(esc));
        uint64_t total = (uint64_t)sums[i].counts.code +
         sums[i].counts.comment + sums[i].counts.blank;
        double pct =
         (grand_total > 0) ? 100.0 * (double)total / (double)grand_total : 0.0;

        if (params.show_complexity) {
            printf(
             "INSERT INTO loc_languages"
             " (run_id, language, files, code, comment, blank, complexity, "
             "total, pct)"
             " VALUES ('%s', '%s', %d, %" PRIu32 ", %" PRIu32 ", %" PRIu32
             ", %" PRIu32 ", %" PRIu64 ", "
             "%.4f);"
             "\n",
             ts, esc, sums[i].files, sums[i].counts.code,
             sums[i].counts.comment, sums[i].counts.blank,
             sums[i].counts.complexity, total, pct);
        } else {
            printf(
             "INSERT INTO loc_languages"
             " (run_id, language, files, code, comment, blank, total, pct)"
             " VALUES ('%s', '%s', %d, %" PRIu32 ", %" PRIu32 ", %" PRIu32
             ", %" PRIu64 ", "
             "%.4f);"
             "\n",
             ts, esc, sums[i].files, sums[i].counts.code,
             sums[i].counts.comment, sums[i].counts.blank, total, pct);
        }
    }
    printf("\n");

    /* ── Per-file rows (optional) ── */
    if (params.show_files && n_files > 0) {
        printf("-- Per-file results\n");
        for (int i = 0; i < n_files; i++) {
            const char* path = (files_v + i)->path;
            const char* ext = (files_v + i)->ext;
            int li = (files_v + i)->lang_idx;
            const char* lang_name =
             (li >= 0 && li < n_langs) ? (langs_v + li)->name : "(unknown)";

            char esc_path[4096], esc_ext[64], esc_lang[128];
            loc__sql_escape(path ? path : "", esc_path, sizeof(esc_path));
            loc__sql_escape(ext ? ext : "", esc_ext, sizeof(esc_ext));
            loc__sql_escape(lang_name, esc_lang, sizeof(esc_lang));

            uint32_t code = (files_v + i)->counts.code;
            uint32_t comment = (files_v + i)->counts.comment;
            uint32_t blank = (files_v + i)->counts.blank;
            uint32_t complexity = (files_v + i)->counts.complexity;
            uint32_t total = code + comment + blank;

            if (params.show_complexity) {
                printf(
                 "INSERT INTO loc_files"
                 " (run_id, path, ext, language, code, comment, blank, "
                 "complexity, total)"
                 " VALUES ('%s', '%s', '%s', '%s', %" PRIu32 ", %" PRIu32
                 ", %" PRIu32 ", %" PRIu32 ", %" PRIu32 ");\n",
                 ts, esc_path, esc_ext, esc_lang, code, comment, blank,
                 complexity, total);
            } else {
                printf(
                 "INSERT INTO loc_files"
                 " (run_id, path, ext, language, code, comment, blank, total)"
                 " VALUES ('%s', '%s', '%s', '%s', %" PRIu32 ", %" PRIu32
                 ", %" PRIu32 ", %" PRIu32 ");\n",
                 ts, esc_path, esc_ext, esc_lang, code, comment, blank, total);
            }
        }
        printf("\n");
    }
#undef MAX_SUMS_SQL
}

/* ═══════════════════════════════════════════════════════════════════════════
 * TTY formatter
 * ═══════════════════════════════════════════════════════════════════════════
 */

static void loc_print_terminal(FileResult* files_v, int n_files,
 const Language* langs_v, int n_langs, LocOutputParams params)
{
#define MAX_SUMS_TERM 1024

    if (n_files == 0) {
        printf("mini-loc: no files processed.\n");
        return;
    }

    LocLangSum* sums = (LocLangSum*)calloc(MAX_SUMS_TERM, sizeof(LocLangSum));

    if (!sums) {
        return;
    }

    uint64_t t_files = 0;
    uint64_t t_code = 0;
    uint64_t t_comment = 0;
    uint64_t t_blank = 0;
    uint64_t t_complexity = 0;

    int n_sums = loc__build_sums(files_v,
     (LocSumParams){n_files, n_langs, MAX_SUMS_TERM, params.sort_order}, sums,
     &t_files, &t_code, &t_comment, &t_blank, &t_complexity);

    uint64_t grand_total = t_code + t_comment + t_blank;

    if (params.show_files) {
        g_sort_order = params.sort_order;
        qsort(files_v, (size_t)n_files, sizeof(FileResult), loc__file_cmp);

        printf("\n%sPer-File Results%s\n\n", LOC_TERM_CYAN, LOC_TERM_RESET);

        if (params.verbose) {
            if (params.show_complexity) {
                printf("%-45s %-10s %9s %9s %9s %12s %9s\n", "File", "Ext",
                 "Code", "Comment", "Blank", "Complexity", "Total");
            } else {
                printf("%-45s %-10s %9s %9s %9s %9s\n", "File", "Ext", "Code",
                 "Comment", "Blank", "Total");
            }
        } else {
            if (params.show_complexity) {
                printf("%-55s %9s %9s %9s %12s %9s\n", "File", "Code",
                 "Comment", "Blank", "Complexity", "Total");
            } else {
                printf("%-55s %9s %9s %9s %9s\n", "File", "Code", "Comment",
                 "Blank", "Total");
            }
        }

        for (int i = 0; i < n_files; i++) {
            uint64_t code = (uint64_t)(files_v + i)->counts.code;
            uint64_t comment = (uint64_t)(files_v + i)->counts.comment;
            uint64_t blank = (uint64_t)(files_v + i)->counts.blank;
            uint64_t complexity = (uint64_t)(files_v + i)->counts.complexity;

            uint64_t total = code + comment + blank;

            if (params.verbose) {
                if (params.show_complexity) {
                    printf("%-45s %-10s %9" PRIu64 " %9" PRIu64 " %9" PRIu64
                           " %12" PRIu64 " %9" PRIu64 "\n",
                     (files_v + i)->path,
                     (files_v + i)->ext ? (files_v + i)->ext : "", code,
                     comment, blank, complexity, total);
                } else {
                    printf("%-45s %-10s %9" PRIu64 " %9" PRIu64 " %9" PRIu64
                           " %9" PRIu64 "\n",
                     (files_v + i)->path,
                     (files_v + i)->ext ? (files_v + i)->ext : "", code,
                     comment, blank, total);
                }
            } else {
                if (params.show_complexity) {
                    printf("%-55s %9" PRIu64 " %9" PRIu64 " %9" PRIu64
                           " %12" PRIu64 " %9" PRIu64 "\n",
                     (files_v + i)->path, code, comment, blank, complexity,
                     total);
                } else {
                    printf("%-55s %9" PRIu64 " %9" PRIu64 " %9" PRIu64
                           " %9" PRIu64 "\n",
                     (files_v + i)->path, code, comment, blank, total);
                }
            }
        }

        printf("\n");
    }

    printf("\n%sLanguage Summary%s\n\n", LOC_TERM_CYAN, LOC_TERM_RESET);

    int max_lang_width = 8;
    for (int i = 0; i < n_sums; i++) {
        const char* name =
         (sums[i].lang_idx >= 0 && sums[i].lang_idx < n_langs) ?
         (langs_v + sums[i].lang_idx)->name :
         "(unknown)";
        int len = (int)strlen(name);
        if (len > max_lang_width) {
            max_lang_width = len;
        }
    }

    if (params.show_complexity) {
        printf("%-*s %7s %10s %7s %10s %10s %12s %10s\n", max_lang_width,
         "Language", "Files", "Code", "Pct", "Comment", "Blank", "Complexity",
         "Total");
    } else {
        printf("%-*s %7s %10s %7s %10s %10s %10s\n", max_lang_width, "Language",
         "Files", "Code", "Pct", "Comment", "Blank", "Total");
    }

    char separator[4096];
    int sep_len = max_lang_width + (params.show_complexity ? 78 : 65);
    if (sep_len > 4095) {
        sep_len = 4095;
    }
    memset(separator, '-', (size_t)sep_len);
    /* NOLINTNEXTLINE(clang-analyzer-security.ArrayBound) */
    separator[sep_len] = '\0';
    printf("%s", separator);
    printf("\n");

    for (int i = 0; i < n_sums; i++) {
        uint64_t total = (uint64_t)sums[i].counts.code +
         sums[i].counts.comment + sums[i].counts.blank;

        double pct = (grand_total > 0) ?
         (100.0 * (double)total / (double)grand_total) :
         0.0;

        const char* name =
         (sums[i].lang_idx >= 0 && sums[i].lang_idx < n_langs) ?
         (langs_v + sums[i].lang_idx)->name :
         "(unknown)";

        if (params.show_complexity) {
            printf(
             "%-*s %7d "
             "%s%10" PRIu32 "%s "
             "%6.1f%% "
             "%s%10" PRIu32 " "
             "%s%10" PRIu32 " "
             "%s%12" PRIu32 "%s "
             "%10" PRIu64 "\n",
             max_lang_width, name, sums[i].files, LOC_TERM_GREEN,
             sums[i].counts.code, LOC_TERM_RESET, pct, LOC_TERM_YELLOW,
             sums[i].counts.comment, LOC_TERM_GRAY, sums[i].counts.blank,
             LOC_TERM_MAGENTA, sums[i].counts.complexity, LOC_TERM_RESET,
             total);
        } else {
            printf(
             "%-*s %7d "
             "%s%10" PRIu32 "%s "
             "%6.1f%% "
             "%s%10" PRIu32 " "
             "%s%10" PRIu32 "%s "
             "%10" PRIu64 "\n",
             max_lang_width, name, sums[i].files, LOC_TERM_GREEN,
             sums[i].counts.code, LOC_TERM_RESET, pct, LOC_TERM_YELLOW,
             sums[i].counts.comment, LOC_TERM_GRAY, sums[i].counts.blank,
             LOC_TERM_RESET, total);
        }
    }
    printf("%s\n", separator);
    if (params.show_complexity) {
        printf("%-*s %7" PRIu64 " %10" PRIu64 " %6.1f%% %10" PRIu64
               " %10" PRIu64 " %12" PRIu64 " %10" PRIu64 "\n\n",
         max_lang_width, "TOTAL", t_files, t_code, 100.0, t_comment, t_blank,
         t_complexity, grand_total);
    } else {
        printf("%-*s %7" PRIu64 " %10" PRIu64 " %6.1f%% %10" PRIu64
               " %10" PRIu64 " %10" PRIu64 "\n\n",
         max_lang_width, "TOTAL", t_files, t_code, 100.0, t_comment, t_blank,
         grand_total);
    }

    printf("Breakdown: %s Code %3.1f%% %s|%s Comment %3.1f%% %s|%s Blank "
           "%3.1f%%%s",
     LOC_TERM_GREEN, (double)(100.0 * (double)t_code / (double)grand_total),
     LOC_TERM_RESET, LOC_TERM_YELLOW,
     (double)(100.0 * (double)t_comment / (double)grand_total), LOC_TERM_RESET,
     LOC_TERM_GRAY, (double)(100.0 * (double)t_blank / (double)grand_total),
     LOC_TERM_RESET);

    if (!params.no_bytes) {
        printf(" | %s%zu B%s", LOC_TERM_CYAN, params.total_bytes,
         LOC_TERM_RESET);
    }
    printf("\n");

    free(sums);

#undef MAX_SUMS_TERM
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Master dispatcher
 * ═══════════════════════════════════════════════════════════════════════════
 */

static void loc_print_report(LocOutputFormat fmt, FileResult* files,
 int n_files, const Language* langs, int n_langs, LocOutputParams params)
{
    switch (fmt) {
    case LOC_FMT_JSON:
        loc_print_json(files, n_files, langs, n_langs, params);
        break;
    case LOC_FMT_HTML:
        loc_print_html(files, n_files, langs, n_langs, params);
        break;
    case LOC_FMT_SQL:
        loc_print_sql(files, n_files, langs, n_langs, params);
        break;
    case LOC_FMT_TERMINAL:
        loc_print_terminal(files, n_files, langs, n_langs, params);
        break;

    default:
        break;
    }
}

/* ── Cleanup macros ── */
#undef LOC_TERM_RESET
#undef LOC_TERM_CYAN
#undef LOC_TERM_GREEN
#undef LOC_TERM_YELLOW
#undef LOC_TERM_GRAY
#undef LOC_TERM_MAGENTA
#undef LOC_HTML_HEADER
#undef LOC_HTML_FOOTER

#endif /* LOC_OUTPUT_H */

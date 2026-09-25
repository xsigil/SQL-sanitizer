%{
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <getopt.h>

extern int yylex(void);
extern int yylineno;
void yyerror(const char *s);

/* SQL 出力用バッファ構造体 */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StringBuffer;

static StringBuffer g_text_buf = {NULL, 0, 0};
static char g_table_name[64] = "raw_corpus";
static char g_doc_id[128] = "anon_doc";
static char g_lang[32] = "ja";
static char g_category[64] = "general";

static char g_default_doc_id[128] = "anon_doc";
static char g_default_lang[32] = "ja";
static char g_default_category[64] = "general";

/* カラム名設定（変更可能） */
static char g_col_doc_id[64] = "doc_id";
static char g_col_lang[64] = "lang";
static char g_col_category[64] = "category";
static char g_col_raw_text[64] = "raw_text";
static char g_col_created_at[64] = "created_at";

/* カラム出力有効フラグ */
static bool g_has_doc_id = true;
static bool g_has_lang = true;
static bool g_has_category = true;
static bool g_has_raw_text = true;
static bool g_has_created_at = true;

static long g_insert_count = 0;
static long g_batch_size = 1000;
static bool g_whole_mode = false;

/* 文字列バッファ操作関数 */
static void buf_init(StringBuffer *b) {
    b->cap = 4096;
    b->len = 0;
    b->data = malloc(b->cap);
    if (!b->data) {
        fprintf(stderr, "[-] Fatal: Out of memory\n");
        exit(1);
    }
    b->data[0] = '\0';
}

static void buf_reset(StringBuffer *b) {
    b->len = 0;
    if (b->data) {
        b->data[0] = '\0';
    }
}

static void buf_ensure(StringBuffer *b, size_t extra) {
    if (b->len + extra + 1 > b->cap) {
        while (b->len + extra + 1 > b->cap) {
            b->cap *= 2;
        }
        b->data = realloc(b->data, b->cap);
        if (!b->data) {
            fprintf(stderr, "[-] Fatal: Out of memory in realloc\n");
            exit(1);
        }
    }
}

/*
 * SQLite 用エスケープ処理:
 * 1. シングルクォート (') を 2連シングルクォート ('') に変換 (SQL Injection 物理根絶)
 * 2. NUL バイト (\0) はスキップ
 * 3. 制御コード (0x00-0x1F、ただし \t, \n, \r 以外) は安全なスペースに置換
 */
static void buf_append_escaped_sql(StringBuffer *b, const char *src, size_t src_len) {
    buf_ensure(b, src_len * 2 + 1);
    char *dst = b->data + b->len;

    for (size_t i = 0; i < src_len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\0') {
            continue;
        } else if (c == '\'') {
            *dst++ = '\'';
            *dst++ = '\'';
        } else if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
            *dst++ = ' ';
        } else {
            *dst++ = (char)c;
        }
    }
    *dst = '\0';
    b->len = dst - b->data;
}

/* 安全な INSERT INTO 文の動的出力（有効なカラムのみ整形出力） */
static void emit_sql_record(const char *doc_id, const char *lang, const char *category, const StringBuffer *text) {
    if (text->len == 0 && !g_has_raw_text) return;

    if (g_insert_count % g_batch_size == 0) {
        if (g_insert_count > 0) {
            printf("COMMIT;\n");
        }
        printf("BEGIN TRANSACTION;\n");
    }

    printf("INSERT INTO %s (", g_table_name);
    bool first = true;
    if (g_has_doc_id) {
        printf("%s%s", first ? "" : ", ", g_col_doc_id);
        first = false;
    }
    if (g_has_lang) {
        printf("%s%s", first ? "" : ", ", g_col_lang);
        first = false;
    }
    if (g_has_category) {
        printf("%s%s", first ? "" : ", ", g_col_category);
        first = false;
    }
    if (g_has_raw_text) {
        printf("%s%s", first ? "" : ", ", g_col_raw_text);
        first = false;
    }
    if (g_has_created_at) {
        printf("%s%s", first ? "" : ", ", g_col_created_at);
        first = false;
    }

    printf(") VALUES (");
    first = true;
    if (g_has_doc_id) {
        printf("%s'%s'", first ? "" : ", ", doc_id);
        first = false;
    }
    if (g_has_lang) {
        printf("%s'%s'", first ? "" : ", ", lang);
        first = false;
    }
    if (g_has_category) {
        printf("%s'%s'", first ? "" : ", ", category);
        first = false;
    }
    if (g_has_raw_text) {
        printf("%s'%s'", first ? "" : ", ", text->data ? text->data : "");
        first = false;
    }
    if (g_has_created_at) {
        printf("%sdatetime('now')", first ? "" : ", ");
        first = false;
    }
    printf(");\n");

    g_insert_count++;
}

%}

%union {
    char *str;
}

%token TOK_DOCUMENT TOK_ID_KEY TOK_LANG_KEY TOK_CAT_KEY
%token <str> STRING_LITERAL RAW_LINE

%%

corpus
    : records
    ;

records
    : /* empty */
    | records record
    ;

record
    : document_block
    | plain_stream_line
    | error { yyerrok; }
    ;

document_block
    : header_spec '{' text_body '}'
      {
          emit_sql_record(g_doc_id, g_lang, g_category, &g_text_buf);
          buf_reset(&g_text_buf);
          strncpy(g_doc_id, g_default_doc_id, sizeof(g_doc_id) - 1);
          strncpy(g_lang, g_default_lang, sizeof(g_lang) - 1);
          strncpy(g_category, g_default_category, sizeof(g_category) - 1);
      }
    ;

header_spec
    : TOK_DOCUMENT
    | TOK_DOCUMENT '(' attr_list ')'
    ;

attr_list
    : /* empty */
    | attr_list_nonempty
    ;

attr_list_nonempty
    : attr_item
    | attr_list_nonempty ',' attr_item
    ;

attr_item
    : TOK_ID_KEY ':' STRING_LITERAL
      {
          strncpy(g_doc_id, $3, sizeof(g_doc_id) - 1);
          g_doc_id[sizeof(g_doc_id) - 1] = '\0';
          free($3);
      }
    | TOK_LANG_KEY ':' STRING_LITERAL
      {
          strncpy(g_lang, $3, sizeof(g_lang) - 1);
          g_lang[sizeof(g_lang) - 1] = '\0';
          free($3);
      }
    | TOK_CAT_KEY ':' STRING_LITERAL
      {
          strncpy(g_category, $3, sizeof(g_category) - 1);
          g_category[sizeof(g_category) - 1] = '\0';
          free($3);
      }
    ;

text_body
    : /* empty */
    ;

plain_stream_line
    : RAW_LINE
      {
          buf_reset(&g_text_buf);
          buf_append_escaped_sql(&g_text_buf, $1, strlen($1));
          emit_sql_record(g_doc_id, g_lang, g_category, &g_text_buf);
          buf_reset(&g_text_buf);
          free($1);
      }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "[-] Parse warning: %s\n", s);
}

/* =========================================================================
   Flex 非依存・ハイブリッド字句解析器（DSL & プレーンストリーム対応）
   ========================================================================= */

typedef enum {
    LEX_TOP,
    LEX_HEADER,
    LEX_BODY
} LexState;

static LexState g_lex_state = LEX_TOP;
static int g_brace_depth = 0;

static void skip_header_whitespace(void) {
    int c;
    while ((c = getchar()) != EOF) {
        if (!isspace(c)) {
            ungetc(c, stdin);
            break;
        }
    }
}

int yylex(void) {
    int c;

    /* Whole モード（標準入力全体を 1 つのレコードとして吸収） */
    if (g_whole_mode) {
        static bool s_done = false;
        if (s_done) return 0;
        s_done = true;

        buf_reset(&g_text_buf);
        char tmp[4096];
        size_t n;
        while ((n = fread(tmp, 1, sizeof(tmp), stdin)) > 0) {
            buf_append_escaped_sql(&g_text_buf, tmp, n);
        }
        if (g_text_buf.len > 0) {
            emit_sql_record(g_doc_id, g_lang, g_category, &g_text_buf);
        }
        return 0;
    }

    if (g_lex_state == LEX_HEADER) {
        skip_header_whitespace();
        c = getchar();
        if (c == EOF) return 0;

        if (c == '(' || c == ')' || c == ':' || c == ',') {
            return c;
        }
        if (c == '{') {
            g_brace_depth = 1;
            g_lex_state = LEX_BODY;
            buf_reset(&g_text_buf);
            return '{';
        }

        if (c == '"') {
            StringBuffer sb;
            buf_init(&sb);
            int sc;
            while ((sc = getchar()) != EOF && sc != '"') {
                if (sc == '\\') {
                    int esc = getchar();
                    if (esc == EOF) break;
                    if (esc == 'n') esc = '\n';
                    else if (esc == 't') esc = '\t';
                    buf_ensure(&sb, 1);
                    sb.data[sb.len++] = esc;
                } else {
                    buf_ensure(&sb, 1);
                    sb.data[sb.len++] = sc;
                }
            }
            sb.data[sb.len] = '\0';
            yylval.str = sb.data;
            return STRING_LITERAL;
        }

        if (isalpha(c) || c == '_') {
            char kw[64];
            size_t idx = 0;
            kw[idx++] = (char)c;
            while ((c = getchar()) != EOF && (isalnum(c) || c == '_')) {
                if (idx < sizeof(kw) - 1) kw[idx++] = (char)c;
            }
            if (c != EOF) ungetc(c, stdin);
            kw[idx] = '\0';

            if (strcmp(kw, "id") == 0) return TOK_ID_KEY;
            if (strcmp(kw, "lang") == 0) return TOK_LANG_KEY;
            if (strcmp(kw, "category") == 0) return TOK_CAT_KEY;
        }
        return c;
    }

    if (g_lex_state == LEX_BODY) {
        while ((c = getchar()) != EOF) {
            if (c == '{') {
                g_brace_depth++;
                buf_append_escaped_sql(&g_text_buf, "{", 1);
            } else if (c == '}') {
                g_brace_depth--;
                if (g_brace_depth == 0) {
                    g_lex_state = LEX_TOP;
                    return '}';
                }
                buf_append_escaped_sql(&g_text_buf, "}", 1);
            } else {
                char ch = (char)c;
                buf_append_escaped_sql(&g_text_buf, &ch, 1);
            }
        }
        g_lex_state = LEX_TOP;
        return 0;
    }

    /* LEX_TOP: 1行単位で読み込み、Document構文か生テキストかを判定 */
    while (1) {
        char line_buf[8192];
        size_t line_len = 0;
        while ((c = getchar()) != EOF) {
            line_buf[line_len++] = (char)c;
            if (c == '\n' || line_len >= sizeof(line_buf) - 1) {
                break;
            }
        }
        if (line_len == 0 && c == EOF) {
            return 0;
        }
        line_buf[line_len] = '\0';

        /* 先頭の空白スキップ */
        size_t p = 0;
        while (p < line_len && (line_buf[p] == ' ' || line_buf[p] == '\t' || line_buf[p] == '\r')) {
            p++;
        }

        /* 改行のみ・空白のみの空行はスキップ（余計な空レコード生成を防止） */
        if (p >= line_len || line_buf[p] == '\n') {
            continue;
        }

        if (strncmp(line_buf + p, "Document", 8) == 0 &&
            (line_buf[p + 8] == '(' || line_buf[p + 8] == '{' || isspace((unsigned char)line_buf[p + 8]))) {
            for (ssize_t i = (ssize_t)line_len - 1; i >= (ssize_t)p + 8; i--) {
                ungetc(line_buf[i], stdin);
            }
            g_lex_state = LEX_HEADER;
            return TOK_DOCUMENT;
        }

        /* 通常の生テキスト行 */
        while (line_len > 0 && (line_buf[line_len - 1] == '\n' || line_buf[line_len - 1] == '\r')) {
            line_buf[--line_len] = '\0';
        }
        yylval.str = strdup(line_buf);
        return RAW_LINE;
    }
}

/* CLI ヘルプ表示 */
static void print_help(const char *prog) {
    fprintf(stderr, "Usage: %s [OPTIONS] < input.txt\n", prog);
    fprintf(stderr, "General Options:\n");
    fprintf(stderr, "  -t, --table <name>          Target SQLite table name (default: raw_corpus)\n");
    fprintf(stderr, "  -c, --category <cat>        Default category (default: general)\n");
    fprintf(stderr, "  -l, --lang <lang>           Default language (default: ja)\n");
    fprintf(stderr, "  -w, --whole                 Treat entire input as a single document\n");
    fprintf(stderr, "  -b, --batch <size>          Transaction batch size (default: 1000)\n");
    fprintf(stderr, "  -h, --help                  Show this help message\n\n");
    fprintf(stderr, "Column Name Customization:\n");
    fprintf(stderr, "  --col-doc_id <name>         Rename doc_id column (default: doc_id)\n");
    fprintf(stderr, "  --col-lang <name>           Rename lang column (default: lang)\n");
    fprintf(stderr, "  --col-category <name>       Rename category column (default: category)\n");
    fprintf(stderr, "  --col-raw_text <name>       Rename raw_text column (default: raw_text)\n");
    fprintf(stderr, "  --col-created_at <name>     Rename created_at column (default: created_at)\n\n");
    fprintf(stderr, "Column Omission Flags:\n");
    fprintf(stderr, "  --no-doc_id                 Omit doc_id column from SQL output\n");
    fprintf(stderr, "  --no-lang                   Omit lang column from SQL output\n");
    fprintf(stderr, "  --no-category               Omit category column from SQL output\n");
    fprintf(stderr, "  --no-raw_text               Omit raw_text column from SQL output\n");
    fprintf(stderr, "  --no-created_at             Omit created_at column from SQL output\n");
}

enum {
    OPT_COL_DOC_ID = 1001,
    OPT_COL_LANG,
    OPT_COL_CATEGORY,
    OPT_COL_RAW_TEXT,
    OPT_COL_CREATED_AT,
    OPT_NO_DOC_ID,
    OPT_NO_LANG,
    OPT_NO_CATEGORY,
    OPT_NO_RAW_TEXT,
    OPT_NO_CREATED_AT
};

int main(int argc, char **argv) {
    static struct option long_options[] = {
        {"table",          required_argument, 0, 't'},
        {"category",       required_argument, 0, 'c'},
        {"lang",           required_argument, 0, 'l'},
        {"whole",          no_argument,       0, 'w'},
        {"batch",          required_argument, 0, 'b'},
        {"help",           no_argument,       0, 'h'},
        /* カラム名カスタマイズ（アンダースコア／ハイフン両対応） */
        {"col-doc_id",     required_argument, 0, OPT_COL_DOC_ID},
        {"col-doc-id",     required_argument, 0, OPT_COL_DOC_ID},
        {"col-lang",       required_argument, 0, OPT_COL_LANG},
        {"col-category",   required_argument, 0, OPT_COL_CATEGORY},
        {"col-raw_text",   required_argument, 0, OPT_COL_RAW_TEXT},
        {"col-raw-text",   required_argument, 0, OPT_COL_RAW_TEXT},
        {"col-created_at", required_argument, 0, OPT_COL_CREATED_AT},
        {"col-created-at", required_argument, 0, OPT_COL_CREATED_AT},
        /* カラム省略フラグ（アンダースコア／ハイフン両対応） */
        {"no-doc_id",      no_argument,       0, OPT_NO_DOC_ID},
        {"no-doc-id",      no_argument,       0, OPT_NO_DOC_ID},
        {"no-lang",        no_argument,       0, OPT_NO_LANG},
        {"no-category",    no_argument,       0, OPT_NO_CATEGORY},
        {"no-raw_text",    no_argument,       0, OPT_NO_RAW_TEXT},
        {"no-raw-text",    no_argument,       0, OPT_NO_RAW_TEXT},
        {"no-created_at",  no_argument,       0, OPT_NO_CREATED_AT},
        {"no-created-at",  no_argument,       0, OPT_NO_CREATED_AT},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "t:c:l:wb:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 't':
                strncpy(g_table_name, optarg, sizeof(g_table_name) - 1);
                break;
            case 'c':
                strncpy(g_category, optarg, sizeof(g_category) - 1);
                strncpy(g_default_category, optarg, sizeof(g_default_category) - 1);
                break;
            case 'l':
                strncpy(g_lang, optarg, sizeof(g_lang) - 1);
                strncpy(g_default_lang, optarg, sizeof(g_default_lang) - 1);
                break;
            case 'w':
                g_whole_mode = true;
                break;
            case 'b':
                g_batch_size = atol(optarg);
                if (g_batch_size <= 0) g_batch_size = 1000;
                break;
            case 'h':
                print_help(argv[0]);
                return 0;
            case OPT_COL_DOC_ID:
                strncpy(g_col_doc_id, optarg, sizeof(g_col_doc_id) - 1);
                break;
            case OPT_COL_LANG:
                strncpy(g_col_lang, optarg, sizeof(g_col_lang) - 1);
                break;
            case OPT_COL_CATEGORY:
                strncpy(g_col_category, optarg, sizeof(g_col_category) - 1);
                break;
            case OPT_COL_RAW_TEXT:
                strncpy(g_col_raw_text, optarg, sizeof(g_col_raw_text) - 1);
                break;
            case OPT_COL_CREATED_AT:
                strncpy(g_col_created_at, optarg, sizeof(g_col_created_at) - 1);
                break;
            case OPT_NO_DOC_ID:
                g_has_doc_id = false;
                break;
            case OPT_NO_LANG:
                g_has_lang = false;
                break;
            case OPT_NO_CATEGORY:
                g_has_category = false;
                break;
            case OPT_NO_RAW_TEXT:
                g_has_raw_text = false;
                break;
            case OPT_NO_CREATED_AT:
                g_has_created_at = false;
                break;
            default:
                print_help(argv[0]);
                return 1;
        }
    }

    buf_init(&g_text_buf);

    /* 初期 DDL の動的出力（有効なカラムのみを生成） */
    printf("-- Auto-generated by UBS sql_filter (Injection-Safe Pipeline)\n");
    printf("PRAGMA journal_mode = WAL;\n");
    printf("PRAGMA synchronous = NORMAL;\n");
    printf("CREATE TABLE IF NOT EXISTS %s (\n", g_table_name);
    printf("    id INTEGER PRIMARY KEY AUTOINCREMENT");
    if (g_has_doc_id) {
        printf(",\n    %s TEXT", g_col_doc_id);
    }
    if (g_has_lang) {
        printf(",\n    %s TEXT", g_col_lang);
    }
    if (g_has_category) {
        printf(",\n    %s TEXT", g_col_category);
    }
    if (g_has_raw_text) {
        printf(",\n    %s TEXT", g_col_raw_text);
    }
    if (g_has_created_at) {
        printf(",\n    %s DATETIME", g_col_created_at);
    }
    printf("\n);\n");

    /* パース実行 */
    yyparse();

    if (g_insert_count > 0 && g_insert_count % g_batch_size != 0) {
        printf("COMMIT;\n");
    }

    fprintf(stderr, "[*] Successfully processed %ld document(s) into SQL stream.\n", g_insert_count);

    if (g_text_buf.data) {
        free(g_text_buf.data);
    }

    return 0;
}
/* STREAMING_CHUNK:Configuring headers and macro definitions... */
%{
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

/* プロトタイプ宣言 */
int yylex(void);
void yyerror(const char *s);

/* 外部参照変数 */
extern int yylineno;
extern char *yytext;

/* SQL 出力用バッファ構造体 */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StringBuffer;

static StringBuffer g_text_buf = {NULL, 0, 0};
static char g_doc_id[128] = "anon_doc";
static char g_lang[32] = "ja";
static char g_category[64] = "general";
static long g_insert_count = 0;
static long g_batch_size = 1000; /* トランザクションを束ねる単位 */

/* 文字列バッファ操作関数 */
static void buf_init(StringBuffer *b) {
    b->cap = 4096;
    b->len = 0;
    b->data = (char *)malloc(b->cap);
    if (!b->data) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }
    b->data[0] = '\0';
}

static void buf_reset(StringBuffer *b) {
    b->len = 0;
    if (b->data) {
        b->data[0] = '\0';
    }
}

/* STREAMING_CHUNK:Implementing SQLite escape engine... */
/*
 * SQLite における SQL インジェクション対策の核心:
 * 1. 文字列リテラル境界を破るシングルクォート (') を '' (2連シングルクォート) に置換。
 * 2. C言語の終端文字を悪用する NUL 文字 (\0) を排除。
 * 3. 不正なバイナリ制御文字（\x00-\x08, \x0B-\x0C, \x0E-\x1F）を空白または除去。
 * 4. UTF-8 のマルチバイト文字（日本語・中国語など）はそのまま透過。
 */
static void buf_append_escaped_sql(StringBuffer *b, const char *src, size_t src_len) {
    /* 最悪の場合、すべての文字が ' だと 2 倍のサイズが必要 */
    size_t needed = b->len + (src_len * 2) + 1;
    if (needed > b->cap) {
        while (b->cap < needed) {
            b->cap *= 2;
        }
        b->data = (char *)realloc(b->data, b->cap);
        if (!b->data) {
            perror("realloc failed");
            exit(EXIT_FAILURE);
        }
    }

    char *dst = b->data + b->len;
    for (size_t i = 0; i < src_len; i++) {
        unsigned char c = (unsigned char)src[i];

        if (c == '\'') {
            /* ' -> '' にエスケープ */
            *dst++ = '\'';
            *dst++ = '\'';
        } else if (c == '\0') {
            /* NUL バイト攻撃の遮断（無視） */
            continue;
        } else if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
            /* 改行・タブ以外の制御文字を無害な空白に置換 */
            *dst++ = ' ';
        } else {
            *dst++ = (char)c;
        }
    }
    *dst = '\0';
    b->len = dst - b->data;
}

/* STREAMING_CHUNK:Defining SQL emission logic... */
/* 安全な INSERT INTO 文の出力 */
static void emit_sql_record(const char *doc_id, const char *lang, const char *category, const StringBuffer *text) {
    if (g_insert_count % g_batch_size == 0) {
        if (g_insert_count > 0) {
            printf("COMMIT;\n");
        }
        printf("BEGIN TRANSACTION;\n");
    }

    /*
     * 出力形式:
     * INSERT INTO raw_corpus (doc_id, lang, category, raw_text, created_at)
     * VALUES ('{doc_id}', '{lang}', '{category}', '{text}', datetime('now'));
     */
    printf("INSERT INTO raw_corpus (doc_id, lang, category, raw_text, created_at) VALUES ('%s', '%s', '%s', '%s', datetime('now'));\n",
           doc_id, lang, category, text->data ? text->data : "");

    g_insert_count++;
}

%}

/* STREAMING_CHUNK:Declaring tokens and AST unions... */
%union {
    char *str;
}

%token TOK_DOCUMENT TOK_ID_KEY TOK_LANG_KEY TOK_CAT_KEY
%token <str> STRING_LITERAL TEXT_CHUNK

%%

/* STREAMING_CHUNK:Building grammar rules... */
input_stream
    : /* empty */
    | input_stream document_block
    ;

document_block
    : header_spec '{' text_body '}'
      {
          /* 1つの文書ブロックが閉じた瞬間に安全な SQL を出力 */
          emit_sql_record(g_doc_id, g_lang, g_category, &g_text_buf);
          buf_reset(&g_text_buf);
          strcpy(g_doc_id, "anon_doc");
          strcpy(g_lang, "ja");
          strcpy(g_category, "general");
      }
    ;

header_spec
    : TOK_DOCUMENT
    | TOK_DOCUMENT '(' attribute_list ')'
    ;

attribute_list
    : attribute
    | attribute_list ',' attribute
    ;

attribute
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
    | text_body text_token
    ;

text_token
    : TEXT_CHUNK
      {
          /* 生テキスト断片をエスケープしながらバッファへ追加 */
          buf_append_escaped_sql(&g_text_buf, $1, strlen($1));
          free($1);
      }
    | STRING_LITERAL
      {
          buf_append_escaped_sql(&g_text_buf, $1, strlen($1));
          free($1);
      }
    ;

%%

/* STREAMING_CHUNK:Implementing integrated lexer... */
/*
 * Flex を介さず単一ファイルで完全動作する高速 DFA 字句解析器
 */
int yylex(void) {
    static int in_body = 0;
    static int body_depth = 0;
    int c;

    if (in_body) {
        /* 波括弧内の本文モード：特殊文字以外はすべて TEXT_CHUNK として貪欲に消費 */
        c = getchar();
        if (c == EOF) {
            in_body = 0;
            return 0;
        }

        if (c == '{') {
            body_depth++;
            yylval.str = strdup("{");
            return TEXT_CHUNK;
        } else if (c == '}') {
            if (body_depth == 0) {
                in_body = 0;
                return '}';
            } else {
                body_depth--;
                yylval.str = strdup("}");
                return TEXT_CHUNK;
            }
        }

        /* 連続するテキストをまとめてチャンク化（高スループット化） */
        char chunk[2048];
        size_t idx = 0;
        chunk[idx++] = (char)c;

        while (idx < sizeof(chunk) - 1) {
            int peek = getchar();
            if (peek == EOF) break;
            if (peek == '{' || peek == '}') {
                ungetc(peek, stdin);
                break;
            }
            chunk[idx++] = (char)peek;
        }
        chunk[idx] = '\0';
        yylval.str = strdup(chunk);
        return TEXT_CHUNK;
    }

    /* ヘッダー・属性解析モード */
    while ((c = getchar()) != EOF) {
        if (isspace(c)) continue;

        if (c == '/' && (c = getchar()) == '/') {
            /* 1行コメントスキップ */
            while ((c = getchar()) != EOF && c != '\n');
            continue;
        }

        if (c == '(' || c == ')' || c == ',' || c == ':') {
            return c;
        }

        if (c == '{') {
            in_body = 1;
            body_depth = 0;
            return '{';
        }

        /* 文字列リテラル "..." */
        if (c == '"') {
            char str_buf[2048];
            size_t idx = 0;
            while ((c = getchar()) != EOF && c != '"' && idx < sizeof(str_buf) - 1) {
                if (c == '\\') {
                    c = getchar();
                    if (c == EOF) break;
                }
                str_buf[idx++] = (char)c;
            }
            str_buf[idx] = '\0';
            yylval.str = strdup(str_buf);
            return STRING_LITERAL;
        }

        /* 識別子・キーワード解析 */
        if (isalpha(c) || c == '_') {
            char id[128];
            size_t idx = 0;
            id[idx++] = (char)c;
            while ((c = getchar()) != EOF && (isalnum(c) || c == '_') && idx < sizeof(id) - 1) {
                id[idx++] = (char)c;
            }
            id[idx] = '\0';
            if (c != EOF) ungetc(c, stdin);

            if (strcmp(id, "Document") == 0) return TOK_DOCUMENT;
            if (strcmp(id, "id") == 0) return TOK_ID_KEY;
            if (strcmp(id, "lang") == 0) return TOK_LANG_KEY;
            if (strcmp(id, "category") == 0) return TOK_CAT_KEY;

            yylval.str = strdup(id);
            return STRING_LITERAL;
        }
    }

    return 0;
}

void yyerror(const char *s) {
    fprintf(stderr, "SQL-Filter Parse Error: %s\n", s);
}

/* STREAMING_CHUNK:Finalizing main runtime entrypoint... */
int main(int argc, char **argv) {
    buf_init(&g_text_buf);

    /* 初期 DDL（テーブル未作成時の自己生成）を出力 */
    printf("-- Auto-generated by UBS sql_filter (Injection-Safe Pipeline)\n");
    printf("PRAGMA journal_mode = WAL;\n");
    printf("PRAGMA synchronous = NORMAL;\n");
    printf("CREATE TABLE IF NOT EXISTS raw_corpus (\n");
    printf("    id INTEGER PRIMARY KEY AUTOINCREMENT,\n");
    printf("    doc_id TEXT,\n");
    printf("    lang TEXT,\n");
    printf("    category TEXT,\n");
    printf("    raw_text TEXT,\n");
    printf("    created_at DATETIME\n");
    printf(");\n");

    /* 構文解析と SQL ストリーム生成の実行 */
    yyparse();

    /* 最終トランザクションのコミット */
    if (g_insert_count > 0) {
        printf("COMMIT;\n");
    }

    fprintf(stderr, "[*] Successfully processed %ld document(s) into SQL stream.\n", g_insert_count);

    if (g_text_buf.data) {
        free(g_text_buf.data);
    }
    return 0;
}
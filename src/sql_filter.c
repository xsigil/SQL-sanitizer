/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "src/sql_filter.y"

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


#line 241 "src/sql_filter.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "sql_filter.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_TOK_DOCUMENT = 3,               /* TOK_DOCUMENT  */
  YYSYMBOL_TOK_ID_KEY = 4,                 /* TOK_ID_KEY  */
  YYSYMBOL_TOK_LANG_KEY = 5,               /* TOK_LANG_KEY  */
  YYSYMBOL_TOK_CAT_KEY = 6,                /* TOK_CAT_KEY  */
  YYSYMBOL_STRING_LITERAL = 7,             /* STRING_LITERAL  */
  YYSYMBOL_RAW_LINE = 8,                   /* RAW_LINE  */
  YYSYMBOL_9_ = 9,                         /* '{'  */
  YYSYMBOL_10_ = 10,                       /* '}'  */
  YYSYMBOL_11_ = 11,                       /* '('  */
  YYSYMBOL_12_ = 12,                       /* ')'  */
  YYSYMBOL_13_ = 13,                       /* ','  */
  YYSYMBOL_14_ = 14,                       /* ':'  */
  YYSYMBOL_YYACCEPT = 15,                  /* $accept  */
  YYSYMBOL_corpus = 16,                    /* corpus  */
  YYSYMBOL_records = 17,                   /* records  */
  YYSYMBOL_record = 18,                    /* record  */
  YYSYMBOL_document_block = 19,            /* document_block  */
  YYSYMBOL_header_spec = 20,               /* header_spec  */
  YYSYMBOL_attr_list = 21,                 /* attr_list  */
  YYSYMBOL_attr_list_nonempty = 22,        /* attr_list_nonempty  */
  YYSYMBOL_attr_item = 23,                 /* attr_item  */
  YYSYMBOL_text_body = 24,                 /* text_body  */
  YYSYMBOL_plain_stream_line = 25          /* plain_stream_line  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  3
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   19

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  15
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  11
/* YYNRULES -- Number of rules.  */
#define YYNRULES  19
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  30

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   263


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      11,    12,     2,     2,    13,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    14,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     9,     2,    10,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint8 yyrline[] =
{
       0,   181,   181,   185,   186,   190,   191,   192,   196,   207,
     208,   212,   213,   217,   218,   222,   228,   234,   243,   247
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "TOK_DOCUMENT",
  "TOK_ID_KEY", "TOK_LANG_KEY", "TOK_CAT_KEY", "STRING_LITERAL",
  "RAW_LINE", "'{'", "'}'", "'('", "')'", "','", "':'", "$accept",
  "corpus", "records", "record", "document_block", "header_spec",
  "attr_list", "attr_list_nonempty", "attr_item", "text_body",
  "plain_stream_line", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-11)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-3)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int8 yypact[] =
{
     -11,     2,     0,   -11,   -11,    -7,   -11,   -11,   -11,     3,
     -11,     1,   -11,    -5,    -4,    -3,     4,     5,   -11,     7,
       6,     8,    12,   -11,     1,   -11,   -11,   -11,   -11,   -11
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       3,     0,     0,     1,     7,     9,    19,     4,     5,     0,
       6,    11,    18,     0,     0,     0,     0,    12,    13,     0,
       0,     0,     0,    10,     0,     8,    15,    16,    17,    14
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -11,   -11,   -11,   -11,   -11,   -11,   -11,   -11,   -10,   -11,
     -11
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
       0,     1,     2,     7,     8,     9,    16,    17,    18,    19,
      10
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int8 yytable[] =
{
      -2,     4,     3,     5,    11,    13,    14,    15,     6,    20,
      21,    22,    12,    26,    29,    27,    23,    25,    24,    28
};

static const yytype_int8 yycheck[] =
{
       0,     1,     0,     3,    11,     4,     5,     6,     8,    14,
      14,    14,     9,     7,    24,     7,    12,    10,    13,     7
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    16,    17,     0,     1,     3,     8,    18,    19,    20,
      25,    11,     9,     4,     5,     6,    21,    22,    23,    24,
      14,    14,    14,    12,    13,    10,     7,     7,     7,    23
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    15,    16,    17,    17,    18,    18,    18,    19,    20,
      20,    21,    21,    22,    22,    23,    23,    23,    24,    25
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     1,     1,     1,     4,     1,
       4,     0,     1,     1,     3,     3,     3,     3,     0,     1
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 7: /* record: error  */
#line 192 "src/sql_filter.y"
            { yyerrok; }
#line 1257 "src/sql_filter.c"
    break;

  case 8: /* document_block: header_spec '{' text_body '}'  */
#line 197 "src/sql_filter.y"
      {
          emit_sql_record(g_doc_id, g_lang, g_category, &g_text_buf);
          buf_reset(&g_text_buf);
          strncpy(g_doc_id, g_default_doc_id, sizeof(g_doc_id) - 1);
          strncpy(g_lang, g_default_lang, sizeof(g_lang) - 1);
          strncpy(g_category, g_default_category, sizeof(g_category) - 1);
      }
#line 1269 "src/sql_filter.c"
    break;

  case 15: /* attr_item: TOK_ID_KEY ':' STRING_LITERAL  */
#line 223 "src/sql_filter.y"
      {
          strncpy(g_doc_id, (yyvsp[0].str), sizeof(g_doc_id) - 1);
          g_doc_id[sizeof(g_doc_id) - 1] = '\0';
          free((yyvsp[0].str));
      }
#line 1279 "src/sql_filter.c"
    break;

  case 16: /* attr_item: TOK_LANG_KEY ':' STRING_LITERAL  */
#line 229 "src/sql_filter.y"
      {
          strncpy(g_lang, (yyvsp[0].str), sizeof(g_lang) - 1);
          g_lang[sizeof(g_lang) - 1] = '\0';
          free((yyvsp[0].str));
      }
#line 1289 "src/sql_filter.c"
    break;

  case 17: /* attr_item: TOK_CAT_KEY ':' STRING_LITERAL  */
#line 235 "src/sql_filter.y"
      {
          strncpy(g_category, (yyvsp[0].str), sizeof(g_category) - 1);
          g_category[sizeof(g_category) - 1] = '\0';
          free((yyvsp[0].str));
      }
#line 1299 "src/sql_filter.c"
    break;

  case 19: /* plain_stream_line: RAW_LINE  */
#line 248 "src/sql_filter.y"
      {
          buf_reset(&g_text_buf);
          buf_append_escaped_sql(&g_text_buf, (yyvsp[0].str), strlen((yyvsp[0].str)));
          emit_sql_record(g_doc_id, g_lang, g_category, &g_text_buf);
          buf_reset(&g_text_buf);
          free((yyvsp[0].str));
      }
#line 1311 "src/sql_filter.c"
    break;


#line 1315 "src/sql_filter.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 257 "src/sql_filter.y"


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

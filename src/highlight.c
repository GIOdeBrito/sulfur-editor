#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "config.h"
#include "editor.h"
#include "highlight.h"
#include "line.h"
#include "util.h"

#define HL_PAIR_KEYWORD 1
#define HL_PAIR_STRING 2
#define HL_PAIR_COMMENT 3
#define HL_PAIR_NUMBER 4
#define HL_PAIR_PREPROC 5
#define HL_PAIR_NORMAL 6
#define HL_PAIR_PAL_BOX 7
#define HL_PAIR_PAL_SEL 8
#define HL_PAIR_PAL_BADGE 9

static const char *s_keywords[] =
{
	"auto", "break", "case", "char", "const", "continue", "default", "do",
	"double", "else", "enum", "extern", "float", "for", "goto", "if",
	"inline", "int", "long", "register", "restrict", "return", "short",
	"signed", "sizeof", "static", "struct", "switch", "typedef", "union",
	"unsigned", "void", "volatile", "while",
	"_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
	"_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local",
	"NULL", "true", "false"
};

static const char *s_keywords_js[] =
{
	"async", "await", "break", "case", "catch", "class", "const", "continue",
	"debugger", "default", "delete", "do", "else", "export", "extends",
	"finally", "for", "from", "function", "if", "import", "in", "instanceof",
	"let", "new", "of", "return", "static", "super", "switch", "this",
	"throw", "try", "typeof", "var", "void", "while", "with", "yield",
	"enum", "implements", "interface", "package", "private", "protected",
	"public", "as", "null", "true", "false", "undefined", "NaN", "Infinity"
};

static const char *s_keywords_php[] =
{
	"array", "as", "bool", "break", "callable", "case", "catch", "class",
	"clone", "const", "continue", "declare", "default", "die", "do",
	"echo", "else", "elseif", "empty", "enddeclare", "endfor", "endforeach",
	"endif", "endswitch", "endwhile", "eval", "exit", "extends", "false",
	"final", "finally", "fn", "for", "foreach", "function", "global",
	"goto", "if", "implements", "include", "include_once", "instanceof",
	"insteadof", "interface", "isset", "iterable", "list", "match",
	"mixed", "namespace", "new", "null", "object", "print", "private",
	"protected", "public", "readonly", "require", "require_once", "return",
	"static", "string", "switch", "throw", "trait", "true", "try",
	"use", "var", "while", "xor", "yield", "int", "float", "void",
	"php", "and", "or"
};

static const char *s_keywords_cs[] =
{
	"abstract", "as", "async", "await", "base", "bool", "break", "byte",
	"case", "catch", "char", "checked", "class", "const", "continue",
	"decimal", "default", "delegate", "do", "double", "dynamic", "else",
	"enum", "event", "explicit", "extern", "false", "finally", "fixed",
	"float", "for", "foreach", "from", "get", "global", "goto", "group",
	"if", "implicit", "in", "int", "interface", "internal", "into", "is",
	"join", "let", "lock", "long", "namespace", "new", "null", "object",
	"operator", "orderby", "out", "override", "params", "private",
	"protected", "public", "readonly", "ref", "return", "sbyte", "sealed",
	"select", "set", "short", "sizeof", "stackalloc", "static", "string",
	"struct", "switch", "this", "throw", "true", "try", "typeof", "uint",
	"ulong", "unchecked", "unsafe", "ushort", "using", "value", "var",
	"virtual", "void", "volatile", "where", "while"
};

static const char *s_keywords_sql[] =
{
	"ADD", "ALL", "ALTER", "AND", "AS", "ASC", "BEGIN", "BETWEEN", "BIGINT",
	"BLOB", "BOOLEAN", "BY", "CASCADE", "CASE", "CAST", "CHAR", "CHECK",
	"COMMIT", "CONSTRAINT", "CREATE", "CROSS", "CURRENT", "DATE", "DECIMAL",
	"DECLARE", "DEFAULT", "DELETE", "DESC", "DISTINCT", "DOUBLE", "DROP",
	"ELSE", "END", "EXCEPT", "EXISTS", "EXTRACT", "FETCH", "FLOAT",
	"FOLLOWING", "FOR", "FOREIGN", "FROM", "FULL", "FUNCTION", "GRANT",
	"GROUP", "HAVING", "IF", "IN", "INDEX", "INNER", "INSERT", "INT",
	"INTEGER", "INTERSECT", "INTO", "IS", "JOIN", "KEY", "LEFT", "LIKE",
	"ILIKE", "LIMIT", "LOOP", "NOT", "NULL", "NUMERIC", "OFFSET", "ON",
	"ORDER", "OUTER", "OVER", "PARTITION", "PRECEDING", "PRIMARY",
	"PROCEDURE", "RANGE", "REAL", "RECURSIVE", "REFERENCES", "RETURN",
	"RETURNS", "REVOKE", "RIGHT", "ROLLBACK", "ROW", "ROWS", "SELECT",
	"SET", "SMALLINT", "TABLE", "TEXT", "THEN", "TIME", "TIMESTAMP",
	"TOP", "TRANSACTION", "TRIGGER", "TRUE", "FALSE", "UNBOUNDED",
	"UNION", "UNIQUE", "UNKNOWN", "UPDATE", "USING", "VALUES", "VARCHAR",
	"VIEW", "WHEN", "WHERE", "WHILE", "WITH", "PRECISION", "RESTRICT",
	"ACTION", "NO", "INTERVAL"
};

static const char *s_keywords_yaml[] =
{
	"true", "True", "TRUE", "false", "False", "FALSE", "null", "Null",
	"NULL", "yes", "Yes", "YES", "no", "No", "NO", "on", "On", "ON",
	"off", "Off", "OFF"
};

static unsigned char *s_scratch;
static size_t s_scratch_cap;
static int s_color;

static int is_alpha_underscore(int c)
{
	if(c >= 'a' && c <= 'z')
	{
		return 1;
	}

	if(c >= 'A' && c <= 'Z')
	{
		return 1;
	}

	return c == '_';
}

static int is_word_continue(int c)
{
	if(is_alpha_underscore(c))
	{
		return 1;
	}

	return c >= '0' && c <= '9';
}

static int is_keyword_in(const char *word, size_t len, const char **table, size_t n)
{
	size_t i;

	if(word == NULL || table == NULL)
	{
		return 0;
	}

	for(i = 0; i < n; i++)
	{
		if(strlen(table[i]) == len && memcmp(table[i], word, len) == 0)
		{
			return 1;
		}
	}

	return 0;
}

static int hl_lower(int c)
{
	if(c >= 'A' && c <= 'Z')
	{
		return c + ('a' - 'A');
	}

	return c;
}

static int is_keyword_ci_in(const char *word, size_t len, const char **table, size_t n)
{
	size_t i;

	if(word == NULL || table == NULL)
	{
		return 0;
	}

	for(i = 0; i < n; i++)
	{
		size_t k;

		if(strlen(table[i]) != len)
		{
			continue;
		}

		for(k = 0; k < len; k++)
		{
			if(hl_lower((unsigned char)word[k]) != hl_lower((unsigned char)table[i][k]))
			{
				break;
			}
		}

		if(k == len)
		{
			return 1;
		}
	}

	return 0;
}

/* Option bits for scan_c_like_line(). */
#define SCAN_HASH_COMMENT 1
#define SCAN_DOLLAR_VAR 2
#define SCAN_BACKTICK_STRING 4
#define SCAN_PHP_TAGS 8
#define SCAN_CS_VERBATIM 16

static void grow_state(Buffer *b, size_t needed)
{
	unsigned char *grown;

	if(b == NULL)
	{
		return;
	}

	if(needed <= b->hl.state_cap)
	{
		return;
	}

	grown = realloc(b->hl.state, needed);

	if(grown == NULL)
	{
		die("realloc");
	}

	b->hl.state = grown;
	b->hl.state_cap = needed;
}

static void grow_scratch(size_t needed)
{
	unsigned char *grown;

	if(needed <= s_scratch_cap)
	{
		return;
	}

	grown = realloc(s_scratch, needed);

	if(grown == NULL)
	{
		die("realloc");
	}

	s_scratch = grown;
	s_scratch_cap = needed;
}

static int scan_c_like_line(const char *data, size_t len, int in_comment, unsigned char *out, const char **keywords, size_t nkeywords, int keyword_ci, int flags)
{
	size_t first;
	size_t i;

	if(data == NULL || out == NULL)
	{
		return in_comment;
	}

	first = 0;

	while(first < len && (data[first] == ' ' || data[first] == '\t'))
	{
		first++;
	}

	i = 0;

	while(i < len)
	{
		size_t j;

		if(in_comment)
		{
			if(data[i] == '*' && i + 1 < len && data[i + 1] == '/')
			{
				out[i] = (unsigned char)HL_COMMENT;
				out[i + 1] = (unsigned char)HL_COMMENT;
				i += 2;
				in_comment = 0;
				continue;
			}

			out[i] = (unsigned char)HL_COMMENT;
			i++;
			continue;
		}

		if(data[i] == '/' && i + 1 < len && data[i + 1] == '/')
		{
			memset(&out[i], HL_COMMENT, len - i);
			break;
		}

		if(data[i] == '/' && i + 1 < len && data[i + 1] == '*')
		{
			out[i] = (unsigned char)HL_COMMENT;
			out[i + 1] = (unsigned char)HL_COMMENT;
			i += 2;
			in_comment = 1;
			continue;
		}

		if(i == first && data[i] == '#' && i + 1 < len && data[i + 1] == '!')
		{
			memset(&out[i], HL_COMMENT, len - i);
			break;
		}

		if((flags & SCAN_HASH_COMMENT) != 0 && data[i] == '#')
		{
			memset(&out[i], HL_COMMENT, len - i);
			break;
		}

		if(data[i] == '"')
		{
			out[i] = (unsigned char)HL_STRING;
			i++;

			while(i < len && data[i] != '"')
			{
				out[i] = (unsigned char)HL_STRING;

				if(data[i] == '\\' && i + 1 < len)
				{
					out[i + 1] = (unsigned char)HL_STRING;
					i += 2;
					continue;
				}

				i++;
			}

			if(i < len)
			{
				out[i] = (unsigned char)HL_STRING;
				i++;
			}

			continue;
		}

		if((flags & SCAN_BACKTICK_STRING) != 0 && data[i] == '`')
		{
			out[i] = (unsigned char)HL_STRING;
			i++;

			while(i < len && data[i] != '`')
			{
				out[i] = (unsigned char)HL_STRING;

				if(data[i] == '\\' && i + 1 < len)
				{
					out[i + 1] = (unsigned char)HL_STRING;
					i += 2;
					continue;
				}

				i++;
			}

			if(i < len)
			{
				out[i] = (unsigned char)HL_STRING;
				i++;
			}

			continue;
		}

		if(data[i] == '\'')
		{
			out[i] = (unsigned char)HL_CHAR;
			i++;

			while(i < len && data[i] != '\'')
			{
				out[i] = (unsigned char)HL_CHAR;

				if(data[i] == '\\' && i + 1 < len)
				{
					out[i + 1] = (unsigned char)HL_CHAR;
					i += 2;
					continue;
				}

				i++;
			}

			if(i < len)
			{
				out[i] = (unsigned char)HL_CHAR;
				i++;
			}

			continue;
		}

		if((flags & SCAN_CS_VERBATIM) != 0)
		{
			if(data[i] == '$' && i + 2 < len && data[i + 1] == '@' && data[i + 2] == '"')
			{
				out[i] = (unsigned char)HL_STRING;
				out[i + 1] = (unsigned char)HL_STRING;
				out[i + 2] = (unsigned char)HL_STRING;
				i += 3;

				while(i < len)
				{
					if(data[i] == '"' && i + 1 < len && data[i + 1] == '"')
					{
						out[i] = (unsigned char)HL_STRING;
						out[i + 1] = (unsigned char)HL_STRING;
						i += 2;
						continue;
					}

					if(data[i] == '"')
					{
						out[i] = (unsigned char)HL_STRING;
						i++;
						break;
					}

					out[i] = (unsigned char)HL_STRING;
					i++;
				}

				continue;
			}

			if(data[i] == '@' && i + 2 < len && data[i + 1] == '$' && data[i + 2] == '"')
			{
				out[i] = (unsigned char)HL_STRING;
				out[i + 1] = (unsigned char)HL_STRING;
				out[i + 2] = (unsigned char)HL_STRING;
				i += 3;

				while(i < len)
				{
					if(data[i] == '"' && i + 1 < len && data[i + 1] == '"')
					{
						out[i] = (unsigned char)HL_STRING;
						out[i + 1] = (unsigned char)HL_STRING;
						i += 2;
						continue;
					}

					if(data[i] == '"')
					{
						out[i] = (unsigned char)HL_STRING;
						i++;
						break;
					}

					out[i] = (unsigned char)HL_STRING;
					i++;
				}

				continue;
			}

			if(data[i] == '@' && i + 1 < len && data[i + 1] == '"')
			{
				out[i] = (unsigned char)HL_STRING;
				out[i + 1] = (unsigned char)HL_STRING;
				i += 2;

				while(i < len)
				{
					if(data[i] == '"' && i + 1 < len && data[i + 1] == '"')
					{
						out[i] = (unsigned char)HL_STRING;
						out[i + 1] = (unsigned char)HL_STRING;
						i += 2;
						continue;
					}

					if(data[i] == '"')
					{
						out[i] = (unsigned char)HL_STRING;
						i++;
						break;
					}

					out[i] = (unsigned char)HL_STRING;
					i++;
				}

				continue;
			}

			if(data[i] == '$' && i + 1 < len && data[i + 1] == '"')
			{
				out[i] = (unsigned char)HL_STRING;
				i++;
				continue;
			}
		}

		if((flags & SCAN_PHP_TAGS) != 0)
		{
			if(data[i] == '<' && i + 1 < len && data[i + 1] == '?')
			{
				out[i] = (unsigned char)HL_PREPROC;
				out[i + 1] = (unsigned char)HL_PREPROC;
				i += 2;

				if(i < len && data[i] == '=')
				{
					out[i] = (unsigned char)HL_PREPROC;
					i++;
				}

				continue;
			}

			if(data[i] == '?' && i + 1 < len && data[i + 1] == '>')
			{
				out[i] = (unsigned char)HL_PREPROC;
				out[i + 1] = (unsigned char)HL_PREPROC;
				i += 2;
				continue;
			}
		}

		if(i == first && data[i] == '#')
		{
			out[i] = (unsigned char)HL_PREPROC;
			i++;

			while(i < len && is_alpha_underscore((unsigned char)data[i]))
			{
				out[i] = (unsigned char)HL_PREPROC;
				i++;
			}

			continue;
		}

		if(data[i] >= '0' && data[i] <= '9')
		{
			while(i < len && (is_word_continue((unsigned char)data[i]) || data[i] == '.'))
			{
				out[i] = (unsigned char)HL_NUMBER;
				i++;
			}

			continue;
		}

		if(is_alpha_underscore((unsigned char)data[i]))
		{
			HlClass cls;

			j = i;

			while(j < len && is_word_continue((unsigned char)data[j]))
			{
				j++;
			}

			cls = HL_NORMAL;

			if(keyword_ci != 0)
			{
				if(is_keyword_ci_in(&data[i], j - i, keywords, nkeywords) != 0)
				{
					cls = HL_KEYWORD;
				}
			}
			else if(is_keyword_in(&data[i], j - i, keywords, nkeywords) != 0)
			{
				cls = HL_KEYWORD;
			}

			memset(&out[i], (int)cls, j - i);
			i = j;
			continue;
		}

		if((flags & SCAN_DOLLAR_VAR) != 0 && data[i] == '$' && i + 1 < len && is_alpha_underscore((unsigned char)data[i + 1]) != 0)
		{
			out[i] = (unsigned char)HL_NUMBER;
			i++;

			while(i < len && is_word_continue((unsigned char)data[i]) != 0)
			{
				out[i] = (unsigned char)HL_NUMBER;
				i++;
			}

			continue;
		}

		out[i] = (unsigned char)HL_NORMAL;
		i++;
	}

	return in_comment;
}

static int scan_c_line(const char *data, size_t len, int in_comment, unsigned char *out)
{
	return scan_c_like_line(data, len, in_comment, out, s_keywords, sizeof(s_keywords) / sizeof(s_keywords[0]), 0, 0);
}

static int scan_js_line(const char *data, size_t len, int in_comment, unsigned char *out)
{
	return scan_c_like_line(data, len, in_comment, out, s_keywords_js, sizeof(s_keywords_js) / sizeof(s_keywords_js[0]), 0, SCAN_BACKTICK_STRING);
}

static int scan_php_line(const char *data, size_t len, int in_comment, unsigned char *out)
{
	return scan_c_like_line(data, len, in_comment, out, s_keywords_php, sizeof(s_keywords_php) / sizeof(s_keywords_php[0]), 1, SCAN_HASH_COMMENT | SCAN_DOLLAR_VAR | SCAN_BACKTICK_STRING | SCAN_PHP_TAGS);
}

static int scan_cs_line(const char *data, size_t len, int in_comment, unsigned char *out)
{
	return scan_c_like_line(data, len, in_comment, out, s_keywords_cs, sizeof(s_keywords_cs) / sizeof(s_keywords_cs[0]), 0, SCAN_CS_VERBATIM);
}

static int scan_sql_line(const char *data, size_t len, int in_comment, unsigned char *out)
{
	size_t i;

	if(data == NULL || out == NULL)
	{
		return in_comment;
	}

	i = 0;

	while(i < len)
	{
		size_t j;

		if(in_comment != 0)
		{
			if(data[i] == '*' && i + 1 < len && data[i + 1] == '/')
			{
				out[i] = (unsigned char)HL_COMMENT;
				out[i + 1] = (unsigned char)HL_COMMENT;
				i += 2;
				in_comment = 0;
				continue;
			}

			out[i] = (unsigned char)HL_COMMENT;
			i++;
			continue;
		}

		if(data[i] == '-' && i + 1 < len && data[i + 1] == '-')
		{
			memset(&out[i], HL_COMMENT, len - i);
			break;
		}

		if(data[i] == '/' && i + 1 < len && data[i + 1] == '*')
		{
			out[i] = (unsigned char)HL_COMMENT;
			out[i + 1] = (unsigned char)HL_COMMENT;
			i += 2;
			in_comment = 1;
			continue;
		}

		if(data[i] == '\'')
		{
			out[i] = (unsigned char)HL_STRING;
			i++;

			while(i < len && data[i] != '\'')
			{
				out[i] = (unsigned char)HL_STRING;

				if(data[i] == '\'' && i + 1 < len && data[i + 1] == '\'')
				{
					out[i + 1] = (unsigned char)HL_STRING;
					i += 2;
					continue;
				}

				if(data[i] == '\\' && i + 1 < len)
				{
					out[i + 1] = (unsigned char)HL_STRING;
					i += 2;
					continue;
				}

				i++;
			}

			if(i < len)
			{
				out[i] = (unsigned char)HL_STRING;
				i++;
			}

			continue;
		}

		if(data[i] == '"')
		{
			out[i] = (unsigned char)HL_STRING;
			i++;

			while(i < len && data[i] != '"')
			{
				out[i] = (unsigned char)HL_STRING;

				if(data[i] == '"' && i + 1 < len && data[i + 1] == '"')
				{
					out[i + 1] = (unsigned char)HL_STRING;
					i += 2;
					continue;
				}

				if(data[i] == '\\' && i + 1 < len)
				{
					out[i + 1] = (unsigned char)HL_STRING;
					i += 2;
					continue;
				}

				i++;
			}

			if(i < len)
			{
				out[i] = (unsigned char)HL_STRING;
				i++;
			}

			continue;
		}

		if(data[i] >= '0' && data[i] <= '9')
		{
			while(i < len && (is_word_continue((unsigned char)data[i]) || data[i] == '.'))
			{
				out[i] = (unsigned char)HL_NUMBER;
				i++;
			}

			continue;
		}

		if(is_alpha_underscore((unsigned char)data[i]))
		{
			HlClass cls;

			j = i;

			while(j < len && is_word_continue((unsigned char)data[j]))
			{
				j++;
			}

			cls = HL_NORMAL;

			if(is_keyword_ci_in(&data[i], j - i, s_keywords_sql, sizeof(s_keywords_sql) / sizeof(s_keywords_sql[0])) != 0)
			{
				cls = HL_KEYWORD;
			}

			memset(&out[i], (int)cls, j - i);
			i = j;
			continue;
		}

		out[i] = (unsigned char)HL_NORMAL;
		i++;
	}

	return in_comment;
}

static int scan_yaml_line(const char *data, size_t len, int in_span, unsigned char *out)
{
	size_t lead;
	size_t i;

	(void)in_span;

	if(data == NULL || out == NULL)
	{
		return 0;
	}

	if(len == 0)
	{
		return 0;
	}

	lead = 0;

	while(lead < len && (data[lead] == ' ' || data[lead] == '\t'))
	{
		lead++;
	}

	for(i = 0; i < lead; i++)
	{
		out[i] = (unsigned char)HL_NORMAL;
	}

	if(lead < len && data[lead] == '#')
	{
		memset(&out[lead], HL_COMMENT, len - lead);
		return 0;
	}

	if(lead + 3 <= len && (memcmp(&data[lead], "---", 3) == 0 || memcmp(&data[lead], "...", 3) == 0))
	{
		size_t k;

		k = lead + 3;

		if(k >= len || data[k] == ' ' || data[k] == '\t')
		{
			memset(&out[lead], HL_PREPROC, len - lead);
			return 0;
		}
	}

	i = lead;

	if(i < len && (data[i] == '-' || data[i] == '?') && i + 1 < len && (data[i + 1] == ' ' || data[i + 1] == '\t'))
	{
		out[i] = (unsigned char)HL_PREPROC;
		i++;

		while(i < len && (data[i] == ' ' || data[i] == '\t'))
		{
			out[i] = (unsigned char)HL_NORMAL;
			i++;
		}
	}

	while(i < len)
	{
		size_t j;

		if(data[i] == '#' && (i == 0 || data[i - 1] == ' ' || data[i - 1] == '\t'))
		{
			memset(&out[i], HL_COMMENT, len - i);
			break;
		}

		if(data[i] == '"')
		{
			out[i] = (unsigned char)HL_STRING;
			i++;

			while(i < len && data[i] != '"')
			{
				out[i] = (unsigned char)HL_STRING;

				if(data[i] == '\\' && i + 1 < len)
				{
					out[i + 1] = (unsigned char)HL_STRING;
					i += 2;
					continue;
				}

				i++;
			}

			if(i < len)
			{
				out[i] = (unsigned char)HL_STRING;
				i++;
			}

			continue;
		}

		if(data[i] == '\'')
		{
			out[i] = (unsigned char)HL_STRING;
			i++;

			while(i < len && data[i] != '\'')
			{
				out[i] = (unsigned char)HL_STRING;

				if(data[i] == '\'' && i + 1 < len && data[i + 1] == '\'')
				{
					out[i + 1] = (unsigned char)HL_STRING;
					i += 2;
					continue;
				}

				i++;
			}

			if(i < len)
			{
				out[i] = (unsigned char)HL_STRING;
				i++;
			}

			continue;
		}

		if(data[i] == '&' || data[i] == '*' || data[i] == '!')
		{
			out[i] = (unsigned char)HL_PREPROC;
			i++;

			while(i < len && (is_word_continue((unsigned char)data[i]) != 0 || data[i] == '-' || data[i] == '.' || data[i] == '/'))
			{
				out[i] = (unsigned char)HL_PREPROC;
				i++;
			}

			continue;
		}

		if((data[i] == '|' || data[i] == '>') && (i == 0 || data[i - 1] == ' ' || data[i - 1] == '\t' || data[i - 1] == ':'))
		{
			size_t k;

			k = i + 1;

			if(k < len && (data[k] == '+' || data[k] == '-'))
			{
				k++;
			}

			while(k < len && data[k] >= '0' && data[k] <= '9')
			{
				k++;
			}

			if(k >= len || data[k] == ' ' || data[k] == '\t' || data[k] == '#')
			{
				while(i < k)
				{
					out[i] = (unsigned char)HL_PREPROC;
					i++;
				}

				continue;
			}
		}

		if(data[i] == '~' && (i + 1 >= len || data[i + 1] == ' ' || data[i + 1] == '\t' || data[i + 1] == '#'))
		{
			out[i] = (unsigned char)HL_KEYWORD;
			i++;
			continue;
		}

		if(data[i] >= '0' && data[i] <= '9')
		{
			while(i < len && (is_word_continue((unsigned char)data[i]) != 0 || data[i] == '.' || data[i] == '-' || data[i] == ':' || data[i] == '+'))
			{
				out[i] = (unsigned char)HL_NUMBER;
				i++;
			}

			continue;
		}

		if(is_alpha_underscore((unsigned char)data[i]) != 0)
		{
			HlClass cls;

			j = i;

			while(j < len && is_word_continue((unsigned char)data[j]) != 0)
			{
				j++;
			}

			cls = HL_NORMAL;

			if(is_keyword_in(&data[i], j - i, s_keywords_yaml, sizeof(s_keywords_yaml) / sizeof(s_keywords_yaml[0])) != 0)
			{
				cls = HL_KEYWORD;
			}
			else
			{
				size_t k;

				k = j;

				while(k < len && (data[k] == ' ' || data[k] == '\t'))
				{
					k++;
				}

				if(k < len && data[k] == ':')
				{
					cls = HL_KEYWORD;
				}
			}

			memset(&out[i], (int)cls, j - i);
			i = j;
			continue;
		}

		out[i] = (unsigned char)HL_NORMAL;
		i++;
	}

	return 0;
}

static int is_md_fence(const char *data, size_t len)
{
	size_t i;
	size_t n;
	char f;

	if(data == NULL)
	{
		return 0;
	}

	i = 0;

	while(i < len && (data[i] == ' ' || data[i] == '\t'))
	{
		i++;
	}

	if(i >= len)
	{
		return 0;
	}

	f = data[i];

	if(f != '`' && f != '~')
	{
		return 0;
	}

	n = 0;

	while(i + n < len && data[i + n] == f)
	{
		n++;
	}

	return n >= 3;
}

static size_t md_find_char(const char *data, size_t len, size_t from, char want)
{
	size_t j;

	if(data == NULL)
	{
		return len;
	}

	for(j = from; j < len; j++)
	{
		if(data[j] == want)
		{
			return j;
		}
	}

	return len;
}

static size_t md_find_seq(const char *data, size_t len, size_t from, const char *seq, size_t seqlen)
{
	size_t j;

	if(data == NULL || seq == NULL || seqlen == 0)
	{
		return len;
	}

	for(j = from; j + seqlen <= len; j++)
	{
		if(memcmp(&data[j], seq, seqlen) == 0)
		{
			return j;
		}
	}

	return len;
}

static int scan_md_line(const char *data, size_t len, int in_fence, unsigned char *out)
{
	size_t lead;
	size_t i;

	if(data == NULL || out == NULL)
	{
		return in_fence;
	}

	if(len == 0)
	{
		return in_fence;
	}

	if(is_md_fence(data, len))
	{
		memset(out, HL_STRING, len);
		return in_fence == 0;
	}

	if(in_fence)
	{
		memset(out, HL_STRING, len);
		return 1;
	}

	lead = 0;

	while(lead < len && (data[lead] == ' ' || data[lead] == '\t'))
	{
		lead++;
	}

	if(lead < len && data[lead] == '#')
	{
		size_t h;

		h = lead;

		while(h < len && data[h] == '#')
		{
			h++;
		}

		if(h - lead <= 6 && (h >= len || data[h] == ' ' || data[h] == '\t'))
		{
			memset(out, HL_KEYWORD, len);
			return 0;
		}
	}

	{
		size_t k;
		char mark;
		size_t count;

		k = lead;
		mark = 0;
		count = 0;

		while(k < len)
		{
			if(data[k] == ' ' || data[k] == '\t')
			{
				k++;
				continue;
			}

			if(data[k] != '-' && data[k] != '*' && data[k] != '_')
			{
				break;
			}

			if(mark == 0)
			{
				mark = data[k];
			}

			if(data[k] != mark)
			{
				break;
			}

			count++;
			k++;
		}

		if(mark != 0 && count >= 3 && k >= len)
		{
			memset(out, HL_PREPROC, len);
			return 0;
		}
	}

	i = 0;

	while(i < lead)
	{
		out[i] = (unsigned char)HL_NORMAL;
		i++;
	}

	if(i < len && data[i] == '>')
	{
		while(i < len && data[i] == '>')
		{
			out[i] = (unsigned char)HL_COMMENT;
			i++;
		}

		if(i < len && (data[i] == ' ' || data[i] == '\t'))
		{
			out[i] = (unsigned char)HL_COMMENT;
			i++;
		}
	}

	if(i < len)
	{
		if((data[i] == '-' || data[i] == '+' || data[i] == '*') && i + 1 < len && (data[i + 1] == ' ' || data[i + 1] == '\t'))
		{
			out[i] = (unsigned char)HL_PREPROC;
			i++;
			out[i] = (unsigned char)HL_NORMAL;
			i++;
		}
		else if(data[i] >= '0' && data[i] <= '9')
		{
			size_t d;

			d = i;

			while(d < len && data[d] >= '0' && data[d] <= '9')
			{
				d++;
			}

			if(d < len && data[d] == '.' && d + 1 < len && (data[d + 1] == ' ' || data[d + 1] == '\t'))
			{
				while(i <= d)
				{
					out[i] = (unsigned char)HL_PREPROC;
					i++;
				}

				out[i] = (unsigned char)HL_NORMAL;
				i++;
			}
		}
	}

	while(i < len)
	{
		if(data[i] == '`')
		{
			size_t close;

			close = md_find_char(data, len, i + 1, '`');

			if(close >= len)
			{
				memset(&out[i], HL_STRING, len - i);
				return 0;
			}

			memset(&out[i], HL_STRING, close - i + 1);
			i = close + 1;
			continue;
		}

		if(data[i] == '[')
		{
			size_t rb;
			size_t paren;
			size_t rp;

			rb = md_find_char(data, len, i + 1, ']');

			if(rb + 1 >= len || data[rb + 1] != '(')
			{
				out[i] = (unsigned char)HL_NORMAL;
				i++;
				continue;
			}

			paren = rb + 1;
			rp = md_find_char(data, len, paren + 1, ')');

			if(rp >= len)
			{
				out[i] = (unsigned char)HL_NORMAL;
				i++;
				continue;
			}

			memset(&out[i], HL_KEYWORD, rb - i + 1);
			memset(&out[paren], HL_STRING, rp - paren + 1);
			i = rp + 1;
			continue;
		}

		if((data[i] == '*' && i + 1 < len && data[i + 1] == '*') || (data[i] == '_' && i + 1 < len && data[i + 1] == '_' && (i == 0 || is_word_continue((unsigned char)data[i - 1]) == 0)))
		{
			char seq[2];
			size_t found;

			seq[0] = data[i];
			seq[1] = data[i + 1];
			found = md_find_seq(data, len, i + 2, seq, 2);

			if(found >= len)
			{
				memset(&out[i], HL_NUMBER, len - i);
				return 0;
			}

			memset(&out[i], HL_NUMBER, found - i + 2);
			i = found + 2;
			continue;
		}

		if(data[i] == '*' || (data[i] == '_' && (i == 0 || is_word_continue((unsigned char)data[i - 1]) == 0)))
		{
			size_t found;

			found = i + 1;

			while(found < len)
			{
				found = md_find_char(data, len, found, data[i]);

				if(found >= len)
				{
					break;
				}

				if(data[i] == '*' || found + 1 >= len || is_word_continue((unsigned char)data[found + 1]) == 0)
				{
					break;
				}

				found++;
			}

			if(found >= len)
			{
				memset(&out[i], HL_PREPROC, len - i);
				return 0;
			}

			memset(&out[i], HL_PREPROC, found - i + 1);
			i = found + 1;
			continue;
		}

		out[i] = (unsigned char)HL_NORMAL;
		i++;
	}

	return 0;
}

void highlight_set_filetype(Buffer *b)
{
	size_t len;

	if(b == NULL)
	{
		return;
	}

	b->filetype = HL_PLAIN;
	len = strlen(b->filename);

	if(len >= 3 && strcmp(&b->filename[len - 3], ".md") == 0)
	{
		b->filetype = HL_MD;
		return;
	}

	if(len >= 4 && strcmp(&b->filename[len - 4], ".cjs") == 0)
	{
		b->filetype = HL_JS;
		return;
	}

	if(len >= 4 && strcmp(&b->filename[len - 4], ".sql") == 0)
	{
		b->filetype = HL_SQL;
		return;
	}

	if(len >= 4 && strcmp(&b->filename[len - 4], ".php") == 0)
	{
		b->filetype = HL_PHP;
		return;
	}

	if(len >= 4 && strcmp(&b->filename[len - 4], ".yml") == 0)
	{
		b->filetype = HL_YAML;
		return;
	}

	if(len >= 5 && strcmp(&b->filename[len - 5], ".yaml") == 0)
	{
		b->filetype = HL_YAML;
		return;
	}

	if(len >= 3 && strcmp(&b->filename[len - 3], ".js") == 0)
	{
		b->filetype = HL_JS;
		return;
	}

	if(len >= 3 && strcmp(&b->filename[len - 3], ".cs") == 0)
	{
		b->filetype = HL_CS;
		return;
	}

	if(len < 2)
	{
		return;
	}

	if(b->filename[len - 2] != '.')
	{
		return;
	}

	if(b->filename[len - 1] == 'c' || b->filename[len - 1] == 'h')
	{
		b->filetype = HL_C;
	}
}

void highlight_mark_dirty(Buffer *b)
{
	if(b == NULL)
	{
		return;
	}

	b->hl.dirty = 1;
}

void highlight_ensure(Buffer *b)
{
	size_t i;
	int in_span;

	if(b == NULL)
	{
		return;
	}

	if(b->hl.dirty == 0)
	{
		return;
	}

	b->hl.dirty = 0;

	if(b->filetype == HL_PLAIN)
	{
		return;
	}

	grow_state(b, b->num_lines);
	in_span = 0;

	for(i = 0; i < b->num_lines; i++)
	{
		Line *line;

		b->hl.state[i] = (unsigned char)in_span;
		line = &b->lines[i];

		if(line->data == NULL || line->len == 0)
		{
			continue;
		}

		grow_scratch(line->len);

		if(b->filetype == HL_MD)
		{
			in_span = scan_md_line(line->data, line->len, in_span, s_scratch);
		}
		else if(b->filetype == HL_JS)
		{
			in_span = scan_js_line(line->data, line->len, in_span, s_scratch);
		}
		else if(b->filetype == HL_PHP)
		{
			in_span = scan_php_line(line->data, line->len, in_span, s_scratch);
		}
		else if(b->filetype == HL_CS)
		{
			in_span = scan_cs_line(line->data, line->len, in_span, s_scratch);
		}
		else if(b->filetype == HL_SQL)
		{
			in_span = scan_sql_line(line->data, line->len, in_span, s_scratch);
		}
		else if(b->filetype == HL_YAML)
		{
			in_span = scan_yaml_line(line->data, line->len, in_span, s_scratch);
		}
		else
		{
			in_span = scan_c_line(line->data, line->len, in_span, s_scratch);
		}
	}
}

void highlight_free(Buffer *b)
{
	if(b == NULL)
	{
		return;
	}

	free(b->hl.state);
	b->hl.state = NULL;
	b->hl.state_cap = 0;
	b->hl.dirty = 0;
}

/* Fit a configured fg (0..255, -1 default) to this terminal.
Values the terminal cannot show fall back to a calm 8-color choice
so the acid theme degrades gracefully on 8-color terminals. */
static int hl_fit_fg(int fg, int fallback)
{
	if(fg < 0 || fg >= COLORS)
	{
		return fallback;
	}

	return fg;
}

/* Base pair for plain text: soft off-white on the painted dark
background. Only used when both fit this terminal (acid on 256
colors); otherwise -1/-1 keeps the terminal's own colors. */
static int hl_base_fitted(int *fg_out, int *bg_out)
{
	int fg;
	int bg;

	if(fg_out == NULL || bg_out == NULL)
	{
		return 0;
	}

	fg = config_highlight_fg(CONFIG_HL_TEXT);
	bg = config_highlight_fg(CONFIG_HL_BG);

	if(fg < 0 || fg >= COLORS || bg < 0 || bg >= COLORS)
	{
		*fg_out = -1;
		*bg_out = -1;
		return 0;
	}

	*fg_out = fg;
	*bg_out = bg;
	return 1;
}

void highlight_init_color(void)
{
	static const int def_fb[5] = { COLOR_YELLOW, COLOR_GREEN, COLOR_CYAN, COLOR_MAGENTA, COLOR_RED };
	static const int acid_fb[5] = { COLOR_GREEN, COLOR_GREEN, -1, COLOR_CYAN, COLOR_YELLOW };
	const int *fb;
	int acid;
	int fg;
	int bg;
	int accent;
	int accent2;
	int palfg;
	int base_fg;
	int base_bg;

	s_color = 0;

	if(has_colors() == FALSE)
	{
		return;
	}

	start_color();

	if(use_default_colors() == ERR)
	{
		return;
	}

	if(config_highlight_mono() != 0)
	{
		return;
	}

	acid = config_highlight_acid();
	fb = acid != 0 ? acid_fb : def_fb;

	if(hl_base_fitted(&base_fg, &base_bg) != 0)
	{
		bg = base_bg;
	}
	else
	{
		base_fg = -1;
		base_bg = -1;
		bg = -1;
	}

	fg = config_highlight_fg(CONFIG_HL_KEYWORD);
	init_pair(HL_PAIR_KEYWORD, hl_fit_fg(fg, fb[0]), bg);
	fg = config_highlight_fg(CONFIG_HL_STRING);
	init_pair(HL_PAIR_STRING, hl_fit_fg(fg, fb[1]), bg);
	fg = config_highlight_fg(CONFIG_HL_COMMENT);
	init_pair(HL_PAIR_COMMENT, hl_fit_fg(fg, fb[2]), bg);
	fg = config_highlight_fg(CONFIG_HL_NUMBER);
	init_pair(HL_PAIR_NUMBER, hl_fit_fg(fg, fb[3]), bg);
	fg = config_highlight_fg(CONFIG_HL_PREPROC);
	init_pair(HL_PAIR_PREPROC, hl_fit_fg(fg, fb[4]), bg);
	init_pair(HL_PAIR_NORMAL, base_fg, base_bg);

	/* Palette accents follow the active theme: the keyword slot is the
	primary accent (input border, selected-row background), the string
	slot is the secondary accent (keybinding badge). Text on accent
	backgrounds uses the theme background, black when unset. Mono and
	colorless terminals keep the A_REVERSE/A_BOLD fallbacks below. */
	accent = hl_fit_fg(config_highlight_fg(CONFIG_HL_KEYWORD), COLOR_YELLOW);
	accent2 = hl_fit_fg(config_highlight_fg(CONFIG_HL_STRING), COLOR_GREEN);

	if(base_bg >= 0)
	{
		palfg = base_bg;
	}
	else
	{
		palfg = COLOR_BLACK;
	}

	init_pair(HL_PAIR_PAL_BOX, accent, bg);
	init_pair(HL_PAIR_PAL_SEL, palfg, accent);
	init_pair(HL_PAIR_PAL_BADGE, palfg, accent2);
	s_color = 1;

	if(base_bg >= 0)
	{
		bkgd((chtype)COLOR_PAIR(HL_PAIR_NORMAL));
	}
}

int highlight_use_color(void)
{
	return s_color;
}

int highlight_attr(HlClass cls)
{
	if(cls == HL_KEYWORD)
	{
		if(config_highlight_acid() != 0)
		{
			return COLOR_PAIR(HL_PAIR_KEYWORD);
		}

		return COLOR_PAIR(HL_PAIR_KEYWORD) | A_BOLD;
	}

	if(cls == HL_STRING || cls == HL_CHAR)
	{
		return COLOR_PAIR(HL_PAIR_STRING);
	}

	if(cls == HL_COMMENT)
	{
		return COLOR_PAIR(HL_PAIR_COMMENT);
	}

	if(cls == HL_NUMBER)
	{
		return COLOR_PAIR(HL_PAIR_NUMBER);
	}

	if(cls == HL_PREPROC)
	{
		return COLOR_PAIR(HL_PAIR_PREPROC);
	}

	{
		int base_fg;
		int base_bg;

		if(s_color != 0 && hl_base_fitted(&base_fg, &base_bg) != 0)
		{
			return COLOR_PAIR(HL_PAIR_NORMAL);
		}
	}

	return A_NORMAL;
}

int highlight_git_attr(GitMarker marker)
{
	if(s_color == 0)
	{
		return A_NORMAL;
	}

	if(marker == GIT_ADDED)
	{
		return COLOR_PAIR(HL_PAIR_STRING);
	}

	if(marker == GIT_MODIFIED)
	{
		return COLOR_PAIR(HL_PAIR_KEYWORD);
	}

	if(marker == GIT_DELETED)
	{
		return COLOR_PAIR(HL_PAIR_PREPROC);
	}

	return A_NORMAL;
}

int highlight_palette_attr(HlPaletteAttr id)
{
	if(s_color == 0)
	{
		if(id == HL_PAL_SEL)
		{
			return A_REVERSE;
		}

		if(id == HL_PAL_BADGE)
		{
			return A_BOLD;
		}

		return A_NORMAL;
	}

	if(id == HL_PAL_BOX)
	{
		return COLOR_PAIR(HL_PAIR_PAL_BOX);
	}

	if(id == HL_PAL_SEL)
	{
		return COLOR_PAIR(HL_PAIR_PAL_SEL);
	}

	if(id == HL_PAL_BADGE)
	{
		return COLOR_PAIR(HL_PAIR_PAL_BADGE);
	}

	return A_NORMAL;
}

const unsigned char *highlight_classes(Buffer *b, size_t row)
{
	Line *line;
	int start_in;

	if(b == NULL)
	{
		return NULL;
	}

	if(b->filetype == HL_PLAIN)
	{
		return NULL;
	}

	if(row >= b->num_lines)
	{
		return NULL;
	}

	line = &b->lines[row];

	if(line->data == NULL || line->len == 0)
	{
		return NULL;
	}

	start_in = 0;

	if(b->hl.state != NULL && row < b->hl.state_cap)
	{
		start_in = b->hl.state[row];
	}

	grow_scratch(line->len);

	if(b->filetype == HL_MD)
	{
		scan_md_line(line->data, line->len, start_in, s_scratch);
	}
	else if(b->filetype == HL_JS)
	{
		scan_js_line(line->data, line->len, start_in, s_scratch);
	}
	else if(b->filetype == HL_PHP)
	{
		scan_php_line(line->data, line->len, start_in, s_scratch);
	}
	else if(b->filetype == HL_CS)
	{
		scan_cs_line(line->data, line->len, start_in, s_scratch);
	}
	else if(b->filetype == HL_SQL)
	{
		scan_sql_line(line->data, line->len, start_in, s_scratch);
	}
	else if(b->filetype == HL_YAML)
	{
		scan_yaml_line(line->data, line->len, start_in, s_scratch);
	}
	else
	{
		scan_c_line(line->data, line->len, start_in, s_scratch);
	}

	return s_scratch;
}

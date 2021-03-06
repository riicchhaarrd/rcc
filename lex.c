#include <stdio.h>
#include <stdlib.h>
#include "parse.h"
#include "token.h"
#include "rhd/heap_string.h"
#include "rhd/linked_list.h"

struct lexer
{
    const char *buf;
    int bufsz;
    int pos;
    struct token tk;
    int lineno;
    struct linked_list *tokens;
    int savepos;
    int flags;
};

static int next(struct lexer *lex)
{
    if(lex->pos + 1 > lex->bufsz)
		return -1;
    return lex->buf[lex->pos++];
}

static void save(struct lexer *lex)
{
    lex->savepos = lex->pos;
}

static void restore(struct lexer *lex)
{
    lex->pos = lex->savepos;
}

static int next_check(struct lexer *lex, int check)
{
    int pos = lex->pos;
    int n = next(lex);
    if(n != check)
    {
        lex->pos = pos;
        return 1;
    }
    return 0;
}

static heap_string next_number(struct lexer *lex, int *is_int)
{
    *is_int = 1;
    //undo the fetch from before
    --lex->pos;
    
    heap_string s = NULL;
    while(1)
    {
        int ch = next(lex);
        if(ch == -1)
		{
            heap_string_free(&s);
			return NULL;
		}
		int valid = ( ch >= '0' && ch <= '9' ) || ch == '.' || ch == 'f';
        if(!valid)
		{
            --lex->pos;
            break;
		}
        if(ch == 'f')
		{
            *is_int = 0;
            break;
		}
        if(ch == '.')
		{
            if(!*is_int) //can't have more than one .
			{
                heap_string_free(&s);
                return NULL;
			} else
                *is_int = 0;
		}
		heap_string_push( &s, ch );
	}
    return s;
}

static heap_string next_match_string(struct lexer *lex)
{
    //undo the fetch from before
    --lex->pos;
    
    heap_string s = NULL;
    int bs = 0;
    while(1)
    {
        int ch = next(lex);
        if(ch == -1 || ch == '"')
        {
            --lex->pos;
            return s;
        }
        if(bs)
        {
            switch(ch)
            {
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            case '\\':
                ch = '\\';
                break;
            }
            bs = 0;
        }
        
        if(ch == '\\')
            bs = 1;
        else
        	heap_string_push(&s, ch);
    }
    return s;
}

static heap_string next_match(struct lexer *lex, int (*cmp)(int))
{
    //undo the fetch from before
    --lex->pos;
    
    heap_string s = NULL;
    while(1)
    {
        int ch = next(lex);
        if(ch == -1 || !cmp(ch))
        {
            --lex->pos;
            return s;
        }
        heap_string_push(&s, ch);
    }
    return s;
}

static int match_test_ident(int ch)
{
    //Keep in mind this only works with numbers being non-first because there's a if before that checks for integers and this is called
    //in a if else, otherwise check if it's only on the first character.
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '$' || ch == '_' || (ch >= '0' && ch <= '9');
}

static int match_test_string(int ch)
{
    return ch != '"';
}

static int byte_value(int ch)
{
    if(ch >= '0' && ch <= '9')
        return ch - '0';
    if(ch >= 'a' && ch <= 'z')
        return ch - 'a' + 10;
    if(ch >= 'A' && ch <= 'Z')
        return ch - 'A' + 10;
    return -1;
}

static int token(struct lexer *lex, struct token *tk)
{
    int single_line_comment = 0;
    int multiple_line_comment = 0;
    int ch;
retry:
    ch = next(lex);
    tk->lineno = lex->lineno + 1;
    if(ch == -1)
		return 1;
    if(ch == 0)
    {
		tk->type = TK_EOF;
        return 0;
    }
    if(multiple_line_comment && ch == '*' && !next_check(lex, '/'))
	{
		multiple_line_comment = 0;
        goto retry;
	}
	else if(ch == '\n')
        single_line_comment = 0;
    if(single_line_comment || multiple_line_comment)
        goto retry;

	tk->type = ch;
    switch(ch)
	{
	case '\n':
		++lex->lineno;
		if ( lex->flags & LEX_FL_NEWLINE_TOKEN )
		{
            tk->type = '\n';
			return 0;
		}
	case '\t':
		goto retry;
        
	case '\r':
	case ' ':
        ++tk->character_start;
        goto retry;

	case '<':
        if(!next_check(lex, '<'))
        {
            tk->type = TK_LSHIFT;
            return 0;
        } else if(!next_check(lex, '='))
        {
            tk->type = TK_LEQUAL;
            return 0;
        }
        break;
	case '>':
        if(!next_check(lex, '>'))
        {
            tk->type = TK_RSHIFT;
            return 0;
        } else if(!next_check(lex, '='))
        {
            tk->type = TK_GEQUAL;
            return 0;
        }
        break;
	case '"':
    {
        tk->type = TK_STRING;
        tk->string[0] = 0;
        if(!next_check(lex, '"'))
        {
            return 0;
        }
        ++lex->pos;
        heap_string s = next_match_string(lex);
        snprintf(tk->string, sizeof(tk->string), "%s", s);
        heap_string_free(&s);
        if(next_check(lex, '"'))
        {
            //expected closing "
            return 1;
        }
    } break;
	case '/':
        if(!next_check(lex, '/'))
		{
			single_line_comment = 1;
			goto retry;
		} else if(!next_check(lex, '*'))
        {
            multiple_line_comment = 1;
            goto retry;
		} else if(!next_check(lex, '='))
        {
            tk->type = TK_DIVIDE_ASSIGN;
            return 0;
        }
        break;
	case '*':
        if(!next_check(lex, '='))
        {
            tk->type = TK_MULTIPLY_ASSIGN;
            return 0;
        }
        break;
	case '^':
        if(!next_check(lex, '='))
        {
            tk->type = TK_XOR_ASSIGN;
            return 0;
        }
        break;
    case '-':
        if(!next_check(lex, '>'))
		{
            tk->type = TK_ARROW;
            return 0;
		} else if(!next_check(lex, '='))
        {
            tk->type = TK_MINUS_ASSIGN;
            return 0;
        } else if(!next_check(lex, '-'))
		{
            tk->type = TK_MINUS_MINUS;
            return 0;
		}
		break;
	case '+':
        if(!next_check(lex, '='))
        {
            tk->type = TK_PLUS_ASSIGN;
            return 0;
        } else if(!next_check(lex, '+'))
		{
            tk->type = TK_PLUS_PLUS;
            return 0;
		}
		break;
	case '=':
        if(!next_check(lex, '='))
        {
            tk->type = TK_EQUAL;
            return 0;
        }
        break;
	case '|':
        if(!next_check(lex, '='))
        {
            tk->type = TK_OR_ASSIGN;
            return 0;
        }
        break;
	case '%':
        if(!next_check(lex, '='))
        {
            tk->type = TK_MOD_ASSIGN;
            return 0;
        }
        break;
        
	case '\'':
    {
        tk->type = TK_INTEGER;
        if(!next_check(lex, '"'))
        {
            perror("error: empty character constant\n");
            return 1;
        }
        int character_constant = next(lex);
        if(character_constant == -1 || character_constant == 0)
		{
            printf("unexpected end of file\n");
			return 1;
		}
		assert(character_constant > 0 && character_constant <= 0xff);
        tk->integer = character_constant; //TODO: add support for \0 \hex and other stuff
        if(next_check(lex, '\''))
        {
            printf("expecting closing ' for character constant\n");
            //expected closing "
            return 1;
        }
    } break;
    
	case '!':
        if(!next_check(lex, '='))
        {
            tk->type = TK_NOT_EQUAL;
            return 0;
        }
        break;
        
	case '.':
        save(lex);
        if(next(lex) == '.' && next(lex) == '.')
		{
            tk->type = TK_DOT_THREE_TIMES;
            return 0;
		} else
            restore(lex);
        break;

	case '\\':
        if(lex->flags & LEX_FL_BACKSLASH_TOKEN)
		{
            tk->type = '\\';
            return 0;
		}
	case '#':
	case '{':
	case '}':
	case '[':
	case ']':
	case '&':
	case '(':
    case '?':
	case ')':
	case ';':
	case ':':
	case ',':
    case '~':
        return 0;

	default:
        if(ch == '0' && !next_check(lex, 'x'))
		{
            tk->type=TK_INTEGER;
            tk->integer = 0;
            while(1)
			{
                int nch = next(lex);
                if(nch == -1)
					return 1;
				int bv = byte_value( nch );
				if ( bv == -1 )
				{
					--lex->pos;
					break;
				}
                tk->integer = (tk->integer << 4) | (bv & 0xf);
			}
		} else if(ch >= '0' && ch <= '9')
	    {
            int is_int;
			heap_string s = next_number( lex, &is_int );
            if(!s) //error
                return 1;
            if(is_int)
			{
				tk->type = TK_INTEGER;
				tk->integer = atoi( s );
			} else
			{
				tk->type = TK_FLOAT;
				tk->flt = atof( s );
			}
			heap_string_free( &s );
	    } else if(match_test_ident(ch))
	    {
		tk->type = TK_IDENT;
		heap_string s = next_match(lex, match_test_ident);
        if((lex->flags & LEX_FL_FORCE_IDENT) != LEX_FL_FORCE_IDENT)
		{
			// check whether this ident is a special ident
			if ( !strcmp( s, "for" ) )
				tk->type = TK_FOR;
			else if ( !strcmp( s, "while" ) )
				tk->type = TK_WHILE;
			else if ( !strcmp( s, "do" ) )
				tk->type = TK_DO;
			else if ( !strcmp( s, "if" ) )
				tk->type = TK_IF;
			else if ( !strcmp( s, "else" ) )
				tk->type = TK_ELSE;
			else if ( !strcmp( s, "return" ) )
				tk->type = TK_RETURN;
			else if ( !strcmp( s, "break" ) )
				tk->type = TK_BREAK;
			else if ( !strcmp( s, "char" ) )
				tk->type = TK_T_CHAR;
			else if ( !strcmp( s, "short" ) )
				tk->type = TK_T_SHORT;
			else if ( !strcmp( s, "int" ) )
				tk->type = TK_T_INT;
			else if ( !strcmp( s, "long" ) )
				tk->type = TK_T_LONG;
			else if ( !strcmp( s, "float" ) )
				tk->type = TK_T_FLOAT;
			else if ( !strcmp( s, "double" ) )
				tk->type = TK_T_DOUBLE;
			else if ( !strcmp( s, "void" ) )
				tk->type = TK_T_VOID;
			else if ( !strcmp( s, "const" ) )
				tk->type = TK_CONST;
			else if ( !strcmp( s, "unsigned" ) )
				tk->type = TK_T_UNSIGNED;
			else if ( !strcmp( s, "sizeof" ) )
				tk->type = TK_SIZEOF;
			else if ( !strcmp( s, "__emit" ) )
				tk->type = TK_EMIT;
			else if ( !strcmp( s, "struct" ) )
				tk->type = TK_STRUCT;
			else if ( !strcmp( s, "union" ) )
				tk->type = TK_UNION;
			else if ( !strcmp( s, "typedef" ) )
				tk->type = TK_TYPEDEF;
			else if ( !strcmp( s, "enum" ) )
				tk->type = TK_ENUM;
		}
			snprintf(tk->string, sizeof(tk->string), "%s", s);
			heap_string_free(&s);
	    } else
	    {
			tk->type = TK_INVALID;
			printf("got %c, unhandled error\n", ch);
			return 1; //error
	    }
	    break;
	}
	return 0;
}

void parse(const char *data, struct token **tokens_out/*must be free'd*/, int *num_tokens, int flags)
{
    *tokens_out = NULL;
	*num_tokens = 0;
    
    int len = strlen(data);

    struct lexer lex = {
        .buf = data,
        .bufsz = strlen(data) + 1,
        .pos = 0,
        .lineno = 0,
        .tokens = NULL,
        .flags = flags
    };

	lex.tokens = linked_list_create( struct token );

	struct token tk = { 0 };
    struct token *end = NULL;

	for ( int i = 0; i < len; ++i )
	{
        tk.start = lex.pos;
        tk.character_start = lex.pos;
		int ret = token( &lex, &tk );
		if ( ret )
		{
			break;
		}
        tk.end = lex.pos;
		// if(tk.type == TK_IDENT)
		//printf("token = %s (%s)\n", token_type_to_string(tk.type), tk.string);
		end = linked_list_prepend( lex.tokens, tk );
        (*num_tokens)++;
	}

    end->end = lex.pos;

    //allocate num_tokens
    struct token *tokens = malloc(sizeof(struct token) * *num_tokens);
    assert(tokens != NULL);

    int index = 0;
	linked_list_reversed_foreach(lex.tokens, struct token*, it,
    {
	    if(it->type == TK_IDENT)
            ;//printf("]%s\n", it->string);

        memcpy(&tokens[index++], it, sizeof(struct token));
    });

    *tokens_out = tokens;

    linked_list_destroy(&lex.tokens);
}

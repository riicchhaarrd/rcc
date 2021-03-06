#ifndef AST_H
#define AST_H
#include <stdio.h>
#include <stdbool.h>
#include "data_type.h"

#define ENUM_BEGIN(typ) enum typ {
#define ENUM(nam) nam
#define ENUM_VALUE(nam, val) nam = val
#define ENUM_END(typ) };
#include "ast_node_type.h"

#undef ENUM_BEGIN
#undef ENUM
#undef ENUM_VALUE
#undef ENUM_END

#define ENUM_BEGIN(typ) static const char * typ ## _strings[] = {
#define ENUM(nam) #nam
#define ENUM_VALUE(nam, val) #nam
#define ENUM_END( typ )                                                                                                \
	}                                                                                                                  \
	;                                                                                                                  \
	static const char* typ##_to_string( int i )                                                                        \
	{                                                                                                                  \
		if ( i < 0 )                                                                                                   \
			return "invalid";                                                                                          \
		return typ##_strings[i];                                                                                       \
	}
#include "ast_node_type.h"

struct ast_node;

#define IDENT_CHARLEN (64)

enum AST_LITERAL_TYPE
{
	LITERAL_INTEGER,
    LITERAL_FLOAT,
    LITERAL_DOUBLE,
    LITERAL_STRING
};

struct ast_block_stmt
{
    struct linked_list *body;
};

struct ast_literal
{
    enum AST_LITERAL_TYPE type;

    union
    {
        char string[IDENT_CHARLEN]; //C's max identifier length is 31 iirc
        float flt;
        double dbl;
        int integer;
        float vector[4];
    };
};

struct ast_identifier
{
    char name[IDENT_CHARLEN];
};

static void print_literal(struct ast_literal* lit)
{
    //TODO: FIX non-integers
    if(lit->type == LITERAL_INTEGER)
    printf("literal %d\n", lit->integer);
    else if(lit->type == LITERAL_FLOAT)
    printf("literal %f\n", lit->flt);
    else if(lit->type == LITERAL_STRING)
        printf("literal '%s'\n", lit->string);
    else
        printf("literal ??????\n");
}

struct ast_bin_expr
{
    struct ast_node *lhs;
    struct ast_node *rhs;
    int operator;
};

struct ast_function_call_expr
{
    struct ast_node *callee;
    struct ast_node *arguments[32];
    int numargs;
};

struct ast_unary_expr
{
	struct ast_node *argument;
    int operator;
    int prefix;
};

struct ast_assignment_expr
{
    struct ast_node *lhs;
    struct ast_node *rhs;
    int operator;
};

struct ast_expr_stmt
{
	struct ast_node *expr;
};

struct ast_if_stmt
{
    struct ast_node *test;
    struct ast_node *consequent;
    struct ast_node *alternative;
};

struct ast_for_stmt
{
    struct ast_node *init;
    struct ast_node *test;
    struct ast_node *update;
    struct ast_node *body;
};

struct ast_while_stmt
{
    struct ast_node *test;
    struct ast_node *body;
};

struct ast_do_while_stmt
{
    struct ast_node *test;
    struct ast_node *body;
};

struct ast_function_decl
{
    struct ast_node *id;
    struct ast_node *parameters[32];
    int numparms;
    struct ast_node *body; //no body means just forward declaration, just prototype function
    struct ast_node *return_data_type;
    int variadic;
    //TODO: access same named variables in different scopes
    struct ast_node *declarations[64]; //TODO: increase max amount of local variables, for now this'll do
    int numdeclarations;
};

struct ast_program
{
    struct linked_list *body;   
};

struct ast_return_stmt
{
    struct ast_node *argument;
};

struct ast_member_expr
{
    struct ast_node *object;
    struct ast_node *property;
    int computed; //unused atm
    int as_pointer;
};

enum TYPE_QUALIFIER
{
    TQ_NONE = 0,
    TQ_CONST = 1,
    TQ_VOLATILE = 2,
    TQ_UNSIGNED = 4
};

/* int,char,float,double etc...*/
struct ast_primitive
{
    int primitive_type;
	int qualifiers;
};

//TODO: FIXME rename
//maybe name is too generic?
struct ast_data_type
{
    struct ast_node *data_type;
    int qualifiers;
	int array_size;
};

struct ast_struct_decl
{
	char name[IDENT_CHARLEN];
	struct ast_node* fields[32]; // TODO: increase N
	int numfields;
};

struct ast_variable_decl
{
    struct ast_node *id;
    struct ast_node *data_type;
    struct ast_node *initializer_value;
};

struct ast_emit
{
    int opcode;
};

struct ast_sizeof
{
    struct ast_node *subject;
};

struct ast_ternary_expr
{
    struct ast_node *condition;
    struct ast_node *consequent;
    struct ast_node *alternative;
};

struct ast_break_stmt
{
    //maybe add break level, nested loops
    //keep track of which loop node we're in maybe
    int unused;
};

struct ast_seq_expr
{
    struct ast_node *expr[16]; //TODO: increase N
    int numexpr;
};

struct ast_cast
{
    struct ast_node *type;
    struct ast_node *expr;
};


//typedef node
//typedef unsigned char BYTE;

struct ast_typedef
{
	char name[IDENT_CHARLEN];
    struct ast_node *type;
};

// enum node
// enum colors { red, green, blue };

struct ast_enum
{
    char name[IDENT_CHARLEN]; //enum name
	struct ast_node* values[32]; //holds the identifiers (ast_identifier) the enum value is the index
	int numvalues;
};

struct ast_enum_value
{
    char ident[IDENT_CHARLEN];
    int value;
};

struct ast_node
{
    struct ast_node *parent;
	enum AST_NODE_TYPE type;
    int start, end;
    int rvalue;
    union
    {
        struct ast_block_stmt block_stmt_data;
		struct ast_bin_expr bin_expr_data;
        struct ast_literal literal_data;
        struct ast_expr_stmt expr_stmt_data;
        struct ast_unary_expr unary_expr_data;
        struct ast_assignment_expr assignment_expr_data;
        struct ast_identifier identifier_data;
        struct ast_function_call_expr call_expr_data;
        struct ast_if_stmt if_stmt_data;
        struct ast_for_stmt for_stmt_data;
        struct ast_while_stmt while_stmt_data;
        struct ast_do_while_stmt do_while_stmt_data;
        struct ast_function_decl func_decl_data;
        struct ast_program program_data;
        struct ast_return_stmt return_stmt_data;
        struct ast_member_expr member_expr_data;
        struct ast_variable_decl variable_decl_data;
        struct ast_primitive primitive_data;
        struct ast_emit emit_data;
        struct ast_sizeof sizeof_data;
        struct ast_ternary_expr ternary_expr_data;
        struct ast_break_stmt break_stmt_data;
        struct ast_seq_expr seq_expr_data;
        struct ast_cast cast_data;
        struct ast_data_type data_type_data;
        struct ast_struct_decl struct_decl_data;
        struct ast_typedef typedef_data;
        struct ast_enum enum_data;
        struct ast_enum_value enum_value_data;
    };
};

static void ast_print_node_type(const char *key, struct ast_node *n)
{
    printf("node type: %s -> %s\n", key, AST_NODE_TYPE_to_string(n->type));
}

#endif

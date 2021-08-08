ENUM_BEGIN(AST_NODE_TYPE)
    ENUM(AST_NONE),
    ENUM(AST_IDENTIFIER),
    ENUM(AST_LITERAL),
    ENUM(AST_UNARY_EXPR),
    ENUM(AST_BIN_EXPR),
    ENUM(AST_TERNARY_EXPR),
    ENUM(AST_EXPR_STMT),
    ENUM(AST_ASSIGNMENT_EXPR),
    ENUM(AST_FUNCTION_CALL_EXPR),
    ENUM(AST_IF_STMT),
    ENUM(AST_FOR_STMT),
    ENUM(AST_WHILE_STMT),
    ENUM(AST_DO_WHILE_STMT),
    ENUM(AST_BLOCK_STMT),
    ENUM(AST_FUNCTION_DECL),
    ENUM(AST_PROGRAM),
    ENUM(AST_RETURN_STMT),
    ENUM(AST_ADDRESS_OF),
    ENUM(AST_DEREFERENCE),
    ENUM(AST_MEMBER_EXPR),
    ENUM(AST_VARIABLE_DECL),
    ENUM(AST_PRIMITIVE_DATA_TYPE),
    ENUM(AST_ARRAY_DATA_TYPE),
    ENUM(AST_POINTER_DATA_TYPE),
    ENUM(AST_SIZEOF),
    ENUM(AST_EMIT),
    ENUM(AST_BREAK_STMT),
    ENUM(AST_EMPTY),
    ENUM(AST_EXIT),
    ENUM_VALUE(AST_INVALID, -1)
ENUM_END(AST_NODE_TYPE)

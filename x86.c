#include "ast.h"
#include "types.h"
#include "token.h"

#include "std.h"
#include "compile.h"
#include "rhd/linked_list.h"
#include "rhd/hash_map.h"

int instruction_position(struct compile_context *ctx)
{
    return heap_string_size(&ctx->instr);
}

static void dd(struct compile_context *ctx, u32 i)
{
    union
    {
        uint32_t i;
        uint8_t b[4];
    } u = { .i = i };
    
    for(size_t i = 0; i < 4; ++i)
		heap_string_push(&ctx->instr, u.b[i]);
}

static void dw(struct compile_context *ctx, u16 i)
{
    union
    {
        uint16_t s;
        uint8_t b[2];
    } u = { .s = i };

    heap_string_push(&ctx->instr, u.b[0]);
    heap_string_push(&ctx->instr, u.b[1]);
}

static void db(struct compile_context *ctx, u8 op)
{
    heap_string_push(&ctx->instr, op);
}

static void set8(struct compile_context *ctx, int offset, u8 op)
{
    ctx->instr[offset] = op;
}

static void set32(struct compile_context *ctx, int offset, u32 value)
{
    u32 *ptr = (u32*)&ctx->instr[offset];
    *ptr = value;
}

static void buf(struct compile_context *ctx, const char *buf, size_t len)
{
    for(size_t i = 0; i < len; ++i)
    {
		heap_string_push(&ctx->instr, buf[i] & 0xff);
    }
}

int get_function_position(struct compile_context *ctx, const char *name)
{    
    linked_list_reversed_foreach(ctx->functions, struct function*, it,
    {
        if(!strcmp(it->name, name))
            return it->location;
    });
    return -1;
}

static int primitive_data_type_size(int type)
{
    switch(type)
	{
    case DT_CHAR: return IMM8;
    case DT_SHORT: return IMM16;
    case DT_INT: return IMM32;
    case DT_NUMBER: return IMM32;
    case DT_FLOAT: return IMM32;
    case DT_DOUBLE: return IMM32;
    case DT_VOID: return 0;
	}
    debug_printf("unhandled type %d\n", type);
    return 0;
}

static int data_type_size(struct ast_node *n)
{
    switch(n->type)
	{
    case AST_POINTER_DATA_TYPE:
        return IMM32;
    case AST_PRIMITIVE_DATA_TYPE:
        return primitive_data_type_size(n->primitive_data_type_data.primitive_type);
    case AST_ARRAY_DATA_TYPE:
	{
		assert( n->array_data_type_data.array_size > 0 );
        
		if ( n->array_data_type_data.data_type->type == AST_ARRAY_DATA_TYPE )
			return data_type_size( n->array_data_type_data.data_type ) * n->array_data_type_data.array_size;
        else if(n->array_data_type_data.data_type->type == AST_PRIMITIVE_DATA_TYPE)
		{
            //printf("array size = %d, primitive_type_size = %d\n", n->array_data_type_data.array_size, primitive_data_type_size(  n->array_data_type_data.data_type->primitive_data_type_data.primitive_type ));
			return primitive_data_type_size( n->array_data_type_data.data_type->primitive_data_type_data.primitive_type ) *
				   n->array_data_type_data.array_size;
		}
		else
		{
            perror("unhandled else for data_type_size\n");
            return 0;
		}
	} break;
	}
	perror( "unhandled data type node, can't get size\n" );
	return 0;
}

static int data_type_pass_by_reference(struct ast_node *n)
{
    if(n->type == AST_PRIMITIVE_DATA_TYPE || n->type == AST_POINTER_DATA_TYPE)
        return 0;
    //for now pass everything else by reference, arrays etc
    return 1;
}

static void process(struct compile_context *ctx, struct ast_node *n);

static int function_call_ident(struct compile_context *ctx, const char *function_name, struct ast_node **args, int numargs)
{
    //printf("func call %s\n", function_name);
    if(!strcmp(function_name, "exit"))
	{
        assert(numargs > 0);
        //maybe later push eax and pop after to preserve the register
        process(ctx, args[0]);
        //insert linux syscall exit
        db(ctx, 0x88); //mov bl, al
        db(ctx, 0xc3);
        db(ctx, 0x31); //xor eax,eax
        db(ctx, 0xc0);
        db(ctx, 0x40); //inc eax
        db(ctx, 0xcd); //int 0x80
        db(ctx, 0x80);
        return 0;
	} else if(!strcmp(function_name, "write"))
    {
        
        process(ctx, args[0]); //fd
        //mov ebx,eax
        db(ctx, 0x89);
        db(ctx, 0xc3);
        process(ctx, args[1]); //buf
        //mov ecx,eax
        db(ctx, 0x89);
        db(ctx, 0xc1);
        process(ctx, args[2]); //bufsz
        //mov edx,eax
        db(ctx, 0x89);
        db(ctx, 0xc2);

        //mov eax,4
        db(ctx, 0xb8);
        db(ctx, 0x04);
        db(ctx, 0x00);
        db(ctx, 0x00);
        db(ctx, 0x00);
        
        db(ctx, 0xcd); //int 0x80
        db(ctx, 0x80);
        return 0;
    } else if(!strcmp(function_name, "int3"))
	{
        if(opt_flags & OPT_DEBUG)
        db(ctx, 0xcc); //int3
        return 0;
	}

    int pos = get_function_position(ctx, function_name);
    if(pos == -1)
    {
        db(ctx, 0xcc);
        db(ctx, 0xcc);
        db(ctx, 0xcc);
		return 1;
    }

	for(int i = 0; i < numargs; ++i)
    {
		process( ctx, args[i] );
        //push eax
        db(ctx, 0x50);
    }
    
    int t = instruction_position(ctx);
    db(ctx, 0xe8);
    dd(ctx, pos - t - 5);

    if(numargs > 0)
	{
		// add esp, 4
		db( ctx, 0x83 );
		db( ctx, 0xc4 );
		db( ctx, numargs * 4 );
	}
	return 0;
}

static int add_data(struct compile_context *ctx, void *data, u32 data_size)
{
    int curpos = heap_string_size(&ctx->data);
    heap_string_appendn(&ctx->data, data, data_size);
	return curpos;
}

static int accumulate_local_variable_declaration_size(struct compile_context *ctx, struct ast_node *n)
{
    assert(n->type == AST_BLOCK_STMT);
    
    int total = 0;
    //TODO: fix this and make it change depending on variable type declaration instead of assignment
    linked_list_reversed_foreach(n->block_stmt_data.body, struct ast_node**, it,
    {
        if((*it)->type == AST_VARIABLE_DECL)
		{
            int ds = data_type_size((*it)->variable_decl_data.data_type);
            assert(ds > 0);
            total += ds;
		}
#if 0
        if((*it)->type == AST_ASSIGNMENT_EXPR && (*it)->assignment_expr_data.operator == '=')
        {
            total += 4; //FIXME: shouldn't always be 4 bytes
        }
#endif
    });
    return total;
    /*
    //align to 32
    //TODO: fix this make sure the esp value is aligned instead
    int aligned = total & ~31;
    if(aligned == 0)
        return 32;
    return aligned;
    */
}

//TODO: implement all opcodes we'll be using so we can keep track of the registers and their values
//TODO: replace our "real" registers with "virtual" registers

static void push(struct compile_context *ctx, enum REGISTER reg)
{
    db(ctx, 0x50 + reg);
    ctx->registers[ESP] -= 4;
}

static void pop(struct compile_context *ctx, enum REGISTER reg)
{
    db(ctx, 0x58 + reg);
    ctx->registers[ESP] += 4;
}

static void mov_r_imm32(struct compile_context *ctx, enum REGISTER reg, i32 imm)
{
    ctx->registers[reg] = imm;
    db(ctx, 0xb8 + reg);
    dd(ctx, imm);
}

static void add(struct compile_context *ctx, enum REGISTER a, enum REGISTER b)
{
    ctx->registers[a] += ctx->registers[b];
    db(ctx, 0x01);
    db(ctx, 0xc0 + b * 9 + a);
}

static void mov_r_string(struct compile_context *ctx, enum REGISTER reg, const char *str)
{
	db( ctx, 0xb8 + reg);
	int from = instruction_position( ctx );
	dd( ctx, 0xcccccccc ); // placeholder

    //TODO: FIXME reloc the register we're keeping track of aswell to the correct to be determined memory location
    ctx->registers[reg] = (intptr_t)str;
    
	int sz = strlen( str ) + 1;
	int to = add_data( ctx, (void*)str, sz );

	// TODO: FIXME make it cleaner and add just a function call before the placeholder inject/xref something
	// and make it work with any type of data so it can go into the .data segment

	struct relocation reloc = { .from = from, .to = to, .size = sz, .type = RELOC_DATA };
	linked_list_prepend( ctx->relocations, reloc );
}

static void load_variable(struct compile_context *ctx, enum REGISTER reg, int as_pointer, struct ast_node *variable_node, int allocate_space);

static intptr_t register_value(struct compile_context *ctx, enum REGISTER reg)
{
    return ctx->registers[reg];
}

static int rvalue(struct compile_context *ctx, enum REGISTER reg, struct ast_node *n)
{
    ctx->operand_size = IMM32;
    switch(n->type)
	{
	case AST_LITERAL:
		// mov eax,imm32
		switch ( n->literal_data.type )
		{
		case LITERAL_INTEGER:
            mov_r_imm32(ctx, reg, n->literal_data.integer);
			break;
		case LITERAL_STRING:
		{
            mov_r_string(ctx, reg, n->literal_data.string);
		}
		break;
		default:
			perror( "unhandled literal" );
			break;
		}
		break;

    case AST_IDENTIFIER:
	{
		const char* variable_name = n->identifier_data.name;
		struct variable* var = hash_map_find( ctx->function->variables, variable_name );
		assert( var );
        int offset = var->is_param ? 8 + var->offset : 0xfc - var->offset;
		// mov r32,[ebp - offset]
		db( ctx, 0x8b );
		db( ctx, 0x45 + 8 * reg );
		db( ctx, offset );
	} break;

    case AST_BIN_EXPR:
    {
        struct ast_node *lhs = n->bin_expr_data.lhs;
        struct ast_node *rhs = n->bin_expr_data.rhs;

        //eax should still be stored leftover
        if(lhs->type == AST_LITERAL)
        {
            //mov eax,imm32
            db(ctx, 0xb8);
            dd(ctx, lhs->literal_data.integer);
        } else
            process(ctx, lhs);
        
        if(rhs->type == AST_LITERAL)
        {
            //mov ecx,imm32
            db(ctx, 0xb9);
            dd(ctx, rhs->literal_data.integer);
        } else
        {
            //push eax
            db(ctx, 0x50);
            process(ctx, rhs);
            //mov ecx,eax
            db(ctx, 0x89);
            db(ctx, 0xc1);
            //pop eax
            db(ctx, 0x58);
        }

        //xor edx,edx
        db(ctx, 0x31);
        db(ctx, 0xd2);

        //xor edx,edx
        //db(ctx, 0x31);
        //db(ctx, 0xd2);

        switch(n->bin_expr_data.operator)
        {
        case '*':
            //imul ecx
            db(ctx, 0xf7);
            db(ctx, 0xe9);
            break;
        case '/':
            //idiv ecx
            db(ctx, 0xf7);
            db(ctx, 0xf9);
            break;

        case '+':
            db(ctx, 0x01);
            db(ctx, 0xc8);
            break;
        case '-':
            db(ctx, 0x29);
            db(ctx, 0xc8);
            break;
        case '&':
            db(ctx, 0x21);
            db(ctx, 0xc8);
            break;
            break;
        case '|':
            db(ctx, 0x09);
            db(ctx, 0xc8);
            break;
        case '^':
            db(ctx, 0x31);
            db(ctx, 0xc8);
            break;
        case TK_LSHIFT:
            db(ctx, 0xd3);
            db(ctx, 0xf0);
            break;
        case TK_RSHIFT:
            db(ctx, 0xd3);
            db(ctx, 0xf8);
            break;
        case '%':
            db(ctx, 0xf7);
            db(ctx, 0xf9);
            db(ctx, 0x89);
            db(ctx, 0xd0);
            break;
        case '>':
            //cmp eax,ecx
            db(ctx, 0x39);
            db(ctx, 0xc8);
            
            //jl <relative offset>
            db(ctx, 0x7c);
            db(ctx, 0x5);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);
            
            //inc eax
            db(ctx, 0x40);
            
            //jmp
            db(ctx, 0xeb);
            db(ctx, 0x02);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);
            
            break;
            
        case '<':
            //cmp eax,ecx
            db(ctx, 0x39);
            db(ctx, 0xc8);
            
            //jg <relative offset>
            db(ctx, 0x7f);
            db(ctx, 0x5);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);
            
            //inc eax
            db(ctx, 0x40);
            
            //jmp
            db(ctx, 0xeb);
            db(ctx, 0x02);
            
            //xor eax,eax
            db(ctx, 0x31);
            db(ctx, 0xc0);
            
            break;

        default:
            printf("unhandled operator %c\n", n->bin_expr_data.operator);
            break;
        }

        //mov eax,edx
        //db(ctx, 0x89);
        //db(ctx, 0xd0);
            
    } break;
    
	default:
        debug_printf("unhandled rvalue '%s'\n", AST_NODE_TYPE_to_string(n->type));
        return 1;
	}
    return 0;
}

//locator value, can be local variable, global variable, array offset or any other valid lvalue
static void lvalue(struct compile_context *ctx, enum REGISTER reg, struct ast_node *n)
{
    switch(n->type)
	{
    case AST_IDENTIFIER:
	{
		const char* variable_name = n->identifier_data.name;
		struct variable* var = hash_map_find( ctx->function->variables, variable_name );
		assert( var );
		int as_pointer = data_type_pass_by_reference( var->data_type_node );
        struct ast_node *variable_type = var->data_type_node;
        int offset = var->is_param ? 8 + var->offset : 0xfc - var->offset;
        if(as_pointer || !var->is_param)
		{
			// lea r32,[ebp - offset]
			db( ctx, 0x8d );
			db( ctx, 0x45 + 8 * reg );
            db( ctx, offset );
		} else
		{
			// mov r32,[ebp - offset]
			db( ctx, 0x8b );
			db( ctx, 0x45 + 8 * reg );
			db( ctx, offset );
		}

        switch(variable_type->type)
		{
        case AST_PRIMITIVE_DATA_TYPE:
            ctx->register_size = primitive_data_type_size(variable_type->primitive_data_type_data.primitive_type);
            break;
        case AST_POINTER_DATA_TYPE:
            ctx->register_size = primitive_data_type_size(variable_type->pointer_data_type_data.data_type->primitive_data_type_data.primitive_type);
            break;
        case AST_ARRAY_DATA_TYPE:
            //TODO: fix multidimensional arrays
            assert(variable_type->array_data_type_data.data_type->type == AST_PRIMITIVE_DATA_TYPE);
            ctx->register_size = primitive_data_type_size(variable_type->array_data_type_data.data_type->primitive_data_type_data.primitive_type);
            break;
        default:
            debug_printf("unhandled data type '%s'\n", AST_NODE_TYPE_to_string(variable_type->type));
            break;
		}
        
        assert(ctx->register_size > 0);
        
		// load_variable( ctx, reg, 1, n, 0 );
	} break;
	case AST_MEMBER_EXPR:
	{
		// TODO: FIXME for multidimensional arrays
		struct ast_node* object = n->member_expr_data.object;
		lvalue( ctx, reg, object );
        int tmp = ctx->register_size;
		rvalue( ctx, EAX, n->member_expr_data.property );
		if ( register_value( ctx, EAX ) > 0 )
			add( ctx, EBX, EAX );
        ctx->register_size = tmp;
	} break;
    default:
        debug_printf("unhandled lvalue '%s'\n", AST_NODE_TYPE_to_string(n->type));
        break;
	}
}

static void load_variable(struct compile_context *ctx, enum REGISTER reg, int as_pointer, struct ast_node *variable_node, int allocate_space)
{
	// allocate_space is deprecated, we're using declaration of variables now e.g
	// char a;
	// then assigning them and loading the variables afterwards..
	// a = 0xff;
	// my_func(a);
	assert(variable_node->type == AST_IDENTIFIER);    
	const char *variable_name = variable_node->identifier_data.name;
    
    struct variable *var = hash_map_find(ctx->function->variables, variable_name);
    if(!var)
    {
        printf("variable '%s' doesn't exist.\n", variable_name);
    }
    assert(var);
    if(data_type_pass_by_reference(var->data_type_node))
        as_pointer = 1;
	//assert(!(allocate_space && var)); //if we already have a parameter, we can't create a local variable then
    
    //assert(var); //assume the variable exists, otherwise return a compiler error... FIXME
    //FIXME: don't assume that it's only integer values.. lookup the variable and check the type and handle it accordingly

    switch(reg)
	{
    case EAX:
        if(!as_pointer)
        {
		// mov eax,[ebp-4]
		db( ctx, 0x8b );
		db( ctx, 0x45 );
        if(var->is_param)
			db( ctx, 8 + var->offset);
        else
			db( ctx, 0xfc - var->offset );
        } else
		{
            // lea eax,[ebp-4]
            db( ctx, 0x8d );
            db( ctx, 0x45 );
            
            if(var->is_param)
                db( ctx, 8 + var->offset);
            else
                db( ctx, 0xfc - var->offset );
		}
		break;
    case EBX:
        if(as_pointer)
        {
            // lea ebx,[ebp-4]
            db( ctx, 0x8d );
            db( ctx, 0x5d );
            if(!allocate_space || var)
            {
                if(var->is_param)
                    db( ctx, 8 + var->offset);
                else
                    db( ctx, 0xfc - var->offset );
            } else
            {
                assert(var);
                //perror("this has been moved to variable declaration..");
                //db(ctx, 0xfc - 4 * ctx->function->localsize++);

                //struct variable tv = { .offset = ctx->function->localsize - 1, .is_param = 0 };
                //hash_map_insert( ctx->function->variables, variable_name, tv );
            }
		} else
		{
            assert(!allocate_space);
			// mov ebx,[ebp-4]
            db( ctx, 0x8b );
            db( ctx, 0x5d );
            if(var->is_param)
                db( ctx, 8 + var->offset);
            else
                db( ctx, 0xfc - var->offset );
		}
		break;
	}
}

static void process(struct compile_context *ctx, struct ast_node *n)
{
    switch(n->type)
    {
    case AST_EMIT:
    {
        db(ctx, n->emit_data.opcode);
    } break;
        
    case AST_IF_STMT:
    {
        struct ast_node *test = n->if_stmt_data.test;
        process(ctx, test);

        //i think we should have our test value in eax now, just test whether it's not zero

        //cmp eax,0
        db(ctx, 0x83);
        db(ctx, 0xf8);
        db(ctx, 0x00);

        //let's assume the jmp distance is small enough so we can relative jump
        
        //je <relative_offset>
		int tmp = instruction_position(ctx);
        db(ctx, 0x74);
        db(ctx, 0x0); //placeholder
        
        //db(ctx, 0xcc); //int3
        
        struct ast_node *consequent = n->if_stmt_data.consequent;
        process(ctx, consequent);
        
        int off = instruction_position(ctx) - tmp;
        assert(off > 0);
        int op = (0xfe + off) % 256;
        set8(ctx, tmp + 1, op & 0xff);
        //TODO: fix if the distance is more, use set32 and different opcode
        
        //db(ctx, 0xcc); //int3
    } break;

    case AST_RETURN_STMT:
    {
        struct ast_node *expr = n->return_stmt_data.argument;
        process(ctx, expr);
        
		//mov esp,ebp
        //pop ebp
        db(ctx, 0x89);
        db(ctx, 0xec);
        db(ctx, 0x5d);
        
        //ret
        db(ctx, 0xc3);   
    } break;
    
    //TODO: implement this properly
    case AST_FUNCTION_DECL:
    {
        if(!strcmp(n->func_decl_data.id->identifier_data.name, "main"))
		{
            //printf("set entry call to 0x%02X (%d)\n", instruction_position( ctx ), instruction_position( ctx ));
			ctx->entry = instruction_position( ctx );
		}
        int loc = instruction_position( ctx );
        struct function func = {
            .location = loc,
            .name = n->func_decl_data.id->identifier_data.name,
            .localvariablesize = 0,
            .variables = hash_map_create(struct variable)
        };
        ctx->function = linked_list_prepend(ctx->functions, func);
        int offset = 0;
        for(int i = 0; i < n->func_decl_data.numparms; ++i)
		{
            struct ast_node *parm = n->func_decl_data.parameters[i];
            assert(parm->type == AST_VARIABLE_DECL);

            struct variable tv = {
                    .offset = offset,
                    .is_param = 1,
                    .data_type_node = parm->variable_decl_data.data_type
            };

            offset += data_type_pass_by_reference(parm->variable_decl_data.data_type) ? /*TODO:FIXME hardcoded pointer size*/IMM32:data_type_size(parm->variable_decl_data.data_type);
            
            assert(parm->variable_decl_data.id->type == AST_IDENTIFIER);
            hash_map_insert(ctx->function->variables, parm->variable_decl_data.id->identifier_data.name, tv);
		}
        assert(n->func_decl_data.body->type == AST_BLOCK_STMT);
        int localsize = accumulate_local_variable_declaration_size(ctx, n->func_decl_data.body);
        //push ebp
        //mov ebp, esp
        db(ctx, 0x55);
        db(ctx, 0x89);
        db(ctx, 0xe5);
        
        //allocate some space

        //sub esp, 4
        //works for < 0xff
        //db(ctx, 0x83);
        //db(ctx, 0xec);
        //db(ctx, 0x04);

        //sub esp, imm32
        db(ctx, 0x81);
        db(ctx, 0xec);
        dd(ctx, localsize);
        
        process(ctx, n->func_decl_data.body);
        
		//mov esp,ebp
        //pop ebp
        db(ctx, 0x89);
        db(ctx, 0xec);
        db(ctx, 0x5d);
        
        //ret
        db(ctx, 0xc3);
    } break;
    
    case AST_BLOCK_STMT:
    {
        //db(ctx, 0xcc); //int3
        linked_list_reversed_foreach(n->block_stmt_data.body, struct ast_node**, it,
        {
            process(ctx, *it);
        });
    } break;

    case AST_PROGRAM:
    {
		linked_list_reversed_foreach( n->program_data.body, struct ast_node**, it, {
                process(ctx, (*it));
        } );
    } break;
    
    case AST_IDENTIFIER:
	{
        load_variable(ctx, EAX, 0, n, 0);
	} break;
    case AST_LITERAL:        
        //mov eax,imm32
        switch(n->literal_data.type)
        {
        case LITERAL_INTEGER:
            db(ctx, 0xb8);
            dd(ctx, n->literal_data.integer);
            break;
        case LITERAL_STRING:
        {
            db(ctx, 0xb8);
            int from = instruction_position(ctx);
            dd(ctx, 0xcccccccc); //placeholder
            const char *str = n->literal_data.string;
            int sz = strlen(str) + 1;
            int to = add_data(ctx, (void*)str, sz);
            
            //TODO: FIXME make it cleaner and add just a function call before the placeholder inject/xref something
            //and make it work with any type of data so it can go into the .data segment

            struct relocation reloc = {
                .from = from,
                .to = to,
                .size = sz,
                .type = RELOC_DATA
            };
            linked_list_prepend(ctx->relocations, reloc);
        } break;
        default:
            perror("unhandled literal");
            break;
        }
        break;
    case AST_UNARY_EXPR:
    {
        struct ast_node *arg = n->unary_expr_data.argument;
        if(arg->type == AST_LITERAL)
        {
            switch(n->unary_expr_data.operator)
            {
            case '-':
                //neg eax
                //db(ctx, 0xf7);
                //db(ctx, 0xd8);
                
                //mov eax,imm32
                db(ctx, 0xb8);
                dd(ctx, -arg->literal_data.integer);
                break;
                
            case '+':
                //mov eax,imm32
                db(ctx, 0xb8);
                dd(ctx, arg->literal_data.integer);
                break;

            case '!':
                //mov eax,imm32
                db(ctx, 0xb8);
                dd(ctx, !arg->literal_data.integer);
                break;
                
            case '~':
                //mov eax,imm32
                db(ctx, 0xb8);
                dd(ctx, ~arg->literal_data.integer);
                break;
                
            default:
                printf("unhandled unary expression %c\n", n->unary_expr_data.operator);
                break;
            }
        } else
        {
            process(ctx, arg);
            switch(n->unary_expr_data.operator)
            {
            case '-':
                //neg eax
                db(ctx, 0xf7);
                db(ctx, 0xd8);
                break;
            case '!':
            case '~':
                //not eax
                db(ctx, 0xf7);
                db(ctx, 0xd0);
                if(n->unary_expr_data.operator=='!')
                {
                    //and eax,1
					db(ctx, 0x83);
					db(ctx, 0xe0);
					db(ctx, 0x01);
                }
                break;
                
            default:
                printf("unhandled unary expression %c\n", n->unary_expr_data.operator);
                break;
            }
        }
    } break;

    case AST_ADDRESS_OF:
    {
        assert(n->address_of_data.value->type == AST_IDENTIFIER);
        load_variable(ctx, EAX, 1, n->address_of_data.value, 0);
    } break;

    case AST_FOR_STMT:
    {
        process(ctx, n->for_stmt_data.init);
        
        int pos = instruction_position(ctx);
        process(ctx, n->for_stmt_data.test);
        //test eax,eax
        db(ctx, 0x85);
        db(ctx, 0xc0);
        
        //jz rel32
        int jz_pos = instruction_position(ctx); //jmp_pos + 2 = new_pos
        db(ctx, 0x0f);
        db(ctx, 0x84);
        dd(ctx, 0x0); //placeholder

        process(ctx, n->for_stmt_data.body);
        process(ctx, n->for_stmt_data.update);
        int tmp = instruction_position(ctx);
        
        //jmp relative
        db(ctx, 0xe9);
        dd(ctx, pos - tmp - 5);
        
        set32(ctx, jz_pos + 2, instruction_position(ctx) - jz_pos - 6);
    } break;

    case AST_ASSIGNMENT_EXPR:
    {
        struct ast_node *lhs = n->assignment_expr_data.lhs;
        struct ast_node *rhs = n->assignment_expr_data.rhs;

        rvalue(ctx, EAX, rhs);
        //we should now have our result in eax
    
        switch(n->assignment_expr_data.operator)
        {
        case TK_PLUS_ASSIGN:
        {
            push(ctx, EAX);
            load_variable(ctx, EBX, 1, lhs, 0);
            pop(ctx, EAX);
            
            //add [ebx],eax
            db(ctx, 0x01);
            db(ctx, 0x03);
        } break;
        case TK_MINUS_ASSIGN:
        {
            push(ctx, EAX);
            load_variable(ctx, EBX, 1, lhs, 0);
            pop(ctx, EAX);
            //sub [ebx],eax
            db(ctx, 0x29);
            db(ctx, 0x03);
        } break;
        //TODO: add div,mul,mod and other operators
        
        case '=':
        {
            push(ctx, EAX);
            lvalue(ctx, EBX, lhs);
            pop(ctx, EAX);

            //TODO: fix hardcoded EBX
            switch(ctx->register_size)
			{
			case IMM32:
				// mov [ebx],eax
				db( ctx, 0x89 );
				db( ctx, 0x03 );
				break;
            case IMM8:
                //mov byte ptr [ebx], al
				db( ctx, 0x88 );
				db( ctx, 0x03 );
                break;
            default:
                debug_printf("unhandled register size %d\n", ctx->register_size);
                exit(1);
                break;
			}
        } break;
        
        default:
            printf("unhandled assignment operator\n");
            break;
        }
    } break;

    case AST_MEMBER_EXPR:
    {
        struct ast_node *object = n->member_expr_data.object;
        struct ast_node *property = n->member_expr_data.property;
        if(object->type == AST_IDENTIFIER)
		{
            process(ctx, property);
            load_variable(ctx, EBX, 1, object, 0);
            //add ebx, eax
            db(ctx, 0x01);
            db(ctx, 0xc3);
		} else if(object->type == AST_MEMBER_EXPR)
		{
            process(ctx, object);
            //should be in ebx now
            push(ctx, EBX);
            
            process(ctx, property);
            //should be in eax now

            pop(ctx, EBX);

            //finally add them together
            //add ebx, eax
            db(ctx, 0x01);
            db(ctx, 0xc3);
		} else
            perror("unhandled member expression");
	} break;

    case AST_VARIABLE_DECL:
    {
        struct ast_node *id = n->variable_decl_data.id;
        struct ast_node *data_type_node = n->variable_decl_data.data_type;
        assert(id->type == AST_IDENTIFIER);
        const char *variable_name = id->identifier_data.name;

        int offset = ctx->function->localvariablesize;
        
        int variable_size = data_type_size(data_type_node);
        assert(variable_size > 0);
        ctx->function->localvariablesize += variable_size;
        
        struct variable tv = { .offset = offset, .is_param = 0, .data_type_node = data_type_node };
        hash_map_insert( ctx->function->variables, variable_name, tv );
    } break;

    case AST_FUNCTION_CALL_EXPR:
    {
        struct ast_node **args = n->call_expr_data.arguments;
        int numargs = n->call_expr_data.numargs;
        struct ast_node *callee = n->call_expr_data.callee;

        if(callee->type == AST_IDENTIFIER)
		{
            int ret = function_call_ident(ctx, callee->identifier_data.name, args, numargs);
            if(ret)
			{
                FIXME("cannot find function '%s'\n", callee->identifier_data.name);
			}
		} else
		{
            FIXME("unhandled function call expression callee type");
		}
	} break;

    case AST_EXIT:
    {
        //TODO: FIXME
    } break;
    
    default:
        if(rvalue(ctx, EAX, n))
		{
			printf( "unhandled ast node type '%s'\n", AST_NODE_TYPE_to_string( n->type ) );
			exit( -1 );
		}
		break;
    }
}

int x86(struct ast_node *head, struct compile_context *ctx)
{
    ctx->entry = 0xffffffff;
    ctx->instr = NULL;
    ctx->function = NULL;
    ctx->relocations = linked_list_create(struct relocation);
    ctx->functions = linked_list_create(struct function);
    ctx->data = NULL;
    ctx->operand_size = ctx->register_size = 0;

    memset(ctx->registers, 0, sizeof(ctx->registers));

    //mov eax,imm32
    db(ctx, 0xb8);
    int from = instruction_position(ctx);
    dd(ctx, 0x0);
    
    //call eax
    db(ctx, 0xff);
    db(ctx, 0xd0);
    
    //insert linux syscall exit
    //xor ebx,ebx
    db(ctx, 0x31);
    db(ctx, 0xdb);
    
    db(ctx, 0x31); //xor eax,eax
    db(ctx, 0xc0);
    db(ctx, 0x40); //inc eax
    db(ctx, 0xcd); //int 0x80
    db(ctx, 0x80);
    
    process(ctx, head);
    
    struct relocation reloc = {
        .from = from,
        .to = ctx->entry,
        .size = 4,
        .type = RELOC_CODE
    };
    linked_list_prepend(ctx->relocations, reloc);
    return 0;
}

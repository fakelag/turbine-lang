#pragma once

typedef double( *JitExecuteFn )( );

struct JitFunction {
	unsigned char* base;
	unsigned char* dst;

	JitExecuteFn execute_fn;

	double constants[ 2 ];
};

struct AstNode;

bool jit_compile( std::vector< AstNode* >& ast, JitFunction* function );

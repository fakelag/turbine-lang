#pragma once

typedef double( *JitExecuteFn )( );

struct JitFunction {
	JitExecuteFn fn;
	std::vector< double > constants;
};

struct AstNode;

bool jit_compile( std::vector< AstNode* >& ast, JitFunction* function );

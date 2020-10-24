#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#include <vector>

#include "x86_64Compiler.h"
#include "../Main.h"

#define REG_RAX 0
#define REG_RCX 1
#define REG_RDX 2
#define REG_RBX 3
#define REG_RSP 4
#define REG_RBP 5
#define REG_RSI 6
#define REG_RDI 7

#define REG_XMM0 0
#define REG_XMM1 1
#define REG_XMM2 2
#define REG_XMM3 3
#define REG_XMM4 4
#define REG_XMM5 5
#define REG_XMM6 6
#define REG_XMM7 7

#define REG_CONST_TABLE REG_RCX

void asm_write_bytes( JitFunction* context, unsigned char n, ... );
void asm_write_byte_array( JitFunction* context, unsigned char n, unsigned char* bytes );
void asm_push_reg( JitFunction* context, unsigned char reg );
void asm_pop_reg( JitFunction* context, unsigned char reg );
void asm_mov_rax_uint64( JitFunction* context, uint64_t uint64 );
void asm_mov_reg_reg( JitFunction* context, unsigned char dst, unsigned char src );
void asm_mov_reg_xmm( JitFunction* context, unsigned char dst, unsigned char xmm_src );
void asm_mov_xmm_xmm( JitFunction* context, unsigned char xmm_dest, unsigned char xmm_src );
void asm_sub_xmm_xmm( JitFunction* context, unsigned char xmm_dest, unsigned char xmm_src );
void asm_mov_xmm_const( JitFunction* context, unsigned char xmm_dest, uint8_t constant_index );
void asm_sub_xmm_const( JitFunction* context, unsigned char xmm_dest, uint8_t constant_index );
void asm_add_xmm_const( JitFunction* context, unsigned char xmm_dest, uint8_t constant_index );
void asm_mul_xmm_const( JitFunction* context, unsigned char xmm_dest, uint8_t constant_index );
void asm_div_xmm_const( JitFunction* context, unsigned char xmm_dest, uint8_t constant_index );
void asm_jmp_rel8( JitFunction* context, unsigned char rel8 );
void asm_je_rel8( JitFunction* context, unsigned char rel8 );
void asm_ret( JitFunction* context );

void jit_build( std::vector< AstNode* >& ast, JitFunction* context ) {
	context->constants[ 0 ] = 1.52;
	context->constants[ 1 ] = 1.0;

	asm_mov_rax_uint64( context, ( uint64_t ) &context->constants[ 0 ] );
	asm_mov_reg_reg( context, REG_CONST_TABLE, REG_RAX );

	asm_mov_xmm_const( context, REG_XMM0, 0 );
	asm_mov_xmm_const( context, REG_XMM1, 1 );

	asm_sub_xmm_xmm( context, REG_XMM0, REG_XMM1 );

	asm_mov_reg_xmm( context, REG_RAX, REG_XMM0 );

	asm_ret( context );
}

bool jit_compile( std::vector< AstNode* >& ast, JitFunction* function ) {
	unsigned char* memory = ( unsigned char* ) VirtualAllocEx( ( HANDLE ) -1, NULL, 4098, MEM_COMMIT, PAGE_EXECUTE_READWRITE );

	function->base = memory;
	function->dst = memory;
	function->execute_fn = ( JitExecuteFn ) memory;

	jit_build( ast, function );

	return true;
}

void asm_write_bytes( JitFunction* context, unsigned char n, ... ) {
	va_list bytes;

	va_start( bytes, n );
	for ( int i = 0; i < n; ++i ) {
		*( context->dst++ ) = ( char ) va_arg( bytes, int );
	}
	va_end( bytes );
}

void asm_write_byte_array( JitFunction* context, unsigned char n, unsigned char* bytes ) {
	for ( int i = 0; i < n; ++i ) {
		*( context->dst++ ) = bytes[ i ];
	}
}

void asm_push_reg( JitFunction* context, unsigned char reg ) {
	// push <reg>
	asm_write_bytes( context, 1, 0x50 | reg );
}

void asm_pop_reg( JitFunction* context, unsigned char reg ) {
	// pop <reg>
	asm_write_bytes( context, 1, 0x58 | reg );
}

void asm_mov_rax_uint64( JitFunction* context, uint64_t uint64 ) {
	encoded_value value;
	value.data.uint64[ 0 ] = uint64;

	// movabs rax, ds:uint64
	asm_write_bytes( context, 10, 0x48, 0xB8,
		value.data.uint8[ 0 ],
		value.data.uint8[ 1 ],
		value.data.uint8[ 2 ],
		value.data.uint8[ 3 ],
		value.data.uint8[ 4 ],
		value.data.uint8[ 5 ],
		value.data.uint8[ 6 ],
		value.data.uint8[ 7 ]
	);
}

void asm_mov_reg_reg( JitFunction* context, unsigned char dst, unsigned char src ) {
	asm_write_bytes( context, 3, 0x48, 0x89, 0xC0 | dst | src << 3 );
}

void asm_mov_reg_xmm( JitFunction* context, unsigned char dst, unsigned char xmm_src ) {
	// movq <reg>, <xmm>
	asm_write_bytes( context, 5, 0x66, 0x48, 0x0F, 0x7E, 0xC0 | ( xmm_src << 3 ) | dst );
}

void asm_mov_xmm_xmm( JitFunction* context, unsigned char xmm_dest, unsigned char xmm_src ) {
	// movsd <xmm>, <xmm>
	asm_write_bytes( context, 4, 0xF2, 0x0F, 0x10, 0xC0 | ( xmm_dest << 3 ) | xmm_src );
}

void asm_sub_xmm_xmm( JitFunction* context, unsigned char xmm_dest, unsigned char xmm_src ) {
	// subsd <xmm>, <xmm>
	asm_write_bytes( context, 4, 0xF2, 0x0F, 0x5C, 0xC0 | ( xmm_dest << 3 ) | xmm_src );
}

void asm_mov_xmm_const( JitFunction* context, unsigned char xmm_dest, uint8_t constant_index ) {
	// movsd  <xmm>, QWORD PTR [REG_CONST_TABLE+constant_index]
	if ( constant_index == 0 ) {
		asm_write_bytes( context, 4, 0xF2, 0x0F, 0x10, 0x00 | ( xmm_dest << 3 ) | REG_CONST_TABLE );
	} else {
		asm_write_bytes( context, 5, 0xF2, 0x0F, 0x10, 0x40 | ( xmm_dest << 3 ) | REG_CONST_TABLE, constant_index * sizeof( double ) );
	}
}

void asm_sub_xmm_const( JitFunction* context, unsigned char xmm_dest, uint8_t constant_index ) {
	// subsd  <xmm>, QWORD PTR [REG_CONST_TABLE+constant_index]
	if ( constant_index == 0 ) {
		asm_write_bytes( context, 4, 0xF2, 0x0F, 0x5C, 0x00 | ( xmm_dest << 3 ) | REG_CONST_TABLE );
	} else {
		asm_write_bytes( context, 5, 0xF2, 0x0F, 0x5C, 0x40 | ( xmm_dest << 3 ) | REG_CONST_TABLE, constant_index * sizeof( double ) );
	}
}

void asm_add_xmm_const( JitFunction* context, unsigned char xmm_dest, uint8_t constant_index ) {
	// addsd  <xmm>, QWORD PTR [REG_CONST_TABLE+constant_index]
	if ( constant_index == 0 ) {
		asm_write_bytes( context, 4, 0xF2, 0x0F, 0x58, 0x00 | ( xmm_dest << 3 ) | REG_CONST_TABLE );
	} else {
		asm_write_bytes( context, 5, 0xF2, 0x0F, 0x58, 0x40 | ( xmm_dest << 3 ) | REG_CONST_TABLE, constant_index * sizeof( double ) );
	}
}

void asm_mul_xmm_const( JitFunction* context, unsigned char xmm_dest, uint8_t constant_index ) {
	// mulsd  <xmm>, QWORD PTR [REG_CONST_TABLE+constant_index]
	if ( constant_index == 0 ) {
		asm_write_bytes( context, 4, 0xF2, 0x0F, 0x59, 0x00 | ( xmm_dest << 3 ) | REG_CONST_TABLE );
	} else {
		asm_write_bytes( context, 5, 0xF2, 0x0F, 0x59, 0x40 | ( xmm_dest << 3 ) | REG_CONST_TABLE, constant_index * sizeof( double ) );
	}
}

void asm_div_xmm_const( JitFunction* context, unsigned char xmm_dest, uint8_t constant_index ) {
	// divsd  <xmm>, QWORD PTR [REG_CONST_TABLE+constant_index]
	if ( constant_index == 0 ) {
		asm_write_bytes( context, 4, 0xF2, 0x0F, 0x5E, 0x00 | ( xmm_dest << 3 ) | REG_CONST_TABLE );
	} else {
		asm_write_bytes( context, 5, 0xF2, 0x0F, 0x5E, 0x40 | ( xmm_dest << 3 ) | REG_CONST_TABLE, constant_index * sizeof( double ) );
	}
}

void asm_jmp_rel8( JitFunction* context, unsigned char rel8 ) {
	// jmp <rel8>
	asm_write_bytes( context, 2, 0xEB, rel8 );
}

void asm_je_rel8( JitFunction* context, unsigned char rel8 ) {
	// je <rel8>
	asm_write_bytes( context, 2, 0x74, rel8 );
}

void asm_ret( JitFunction* context ) {
	// ret
	asm_write_bytes( context, 1, 0xC3 );
}

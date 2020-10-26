#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#include <vector>
#include <string>
#include <assert.h>
#include <algorithm>
#include <iostream>

#include "Decompiler.h"
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

enum LocationType {
	location_stack,
	location_xmm,
};

struct Identifier {
	Identifier( const std::string& name, LocationType loc_type, uint32_t loc, uint32_t hydr_count, bool is_const, uint32_t const_index ) {
		static int s_identifier_uuid = 0;

		names.push_back( name );
		uuid = "id_" + std::to_string( s_identifier_uuid++ );
		location_type = loc_type;
		location = loc;
		hydrate_count = hydr_count;
		is_constant = is_const;
		constant_index = const_index;
	}

	bool has_name( const std::string& name ) const {
		return std::find( names.begin(), names.end(), name ) != names.end();
	}

	std::string uuid;
	std::vector< std::string > names;
	LocationType location_type;
	uint32_t location;
	uint32_t hydrate_count;

	bool is_constant;
	uint32_t constant_index;
};

struct JitContext {
	JitFunction* function;
	unsigned char* dst;
	std::vector< Identifier > identifiers;
	uint32_t spill_count;
	uint32_t hydrate_count;
};

unsigned char* asm_write_bytes( unsigned char* at, uint32_t length, ... );
unsigned char* asm_write_byte_array( unsigned char* at, uint32_t length, unsigned char* bytes );
void asm_push_reg( JitContext* context, unsigned char reg );
void asm_pop_reg( JitContext* context, unsigned char reg );
void asm_mov_rax_uint64( JitContext* context, uint64_t uint64 );
void asm_mov_stack_xmm( JitContext* context, uint32_t rsp_offset, unsigned char xmm_src );
void asm_mov_xmm_stack( JitContext* context, unsigned char xmm_dst, uint32_t rsp_offset );
void asm_mov_reg_reg( JitContext* context, unsigned char dst, unsigned char src );
void asm_sub_reg_const( JitContext* context, unsigned char dst, uint32_t constant );
void asm_mov_reg_xmm( JitContext* context, unsigned char dst, unsigned char xmm_src );
void asm_mov_xmm_xmm( JitContext* context, unsigned char xmm_dest, unsigned char xmm_src );
void asm_add_xmm_xmm( JitContext* context, unsigned char xmm_dest, unsigned char xmm_src );
void asm_sub_xmm_xmm( JitContext* context, unsigned char xmm_dest, unsigned char xmm_src );
void asm_mov_xmm_const( JitContext* context, unsigned char xmm_dest, uint8_t constant_index );
void asm_sub_xmm_const( JitContext* context, unsigned char xmm_dest, uint8_t constant_index );
void asm_add_xmm_const( JitContext* context, unsigned char xmm_dest, uint8_t constant_index );
//void asm_mul_xmm_const( JitContext* context, unsigned char xmm_dest, uint8_t constant_index );
//void asm_div_xmm_const( JitContext* context, unsigned char xmm_dest, uint8_t constant_index );
void asm_xor_xmm_xmm( JitContext* context, unsigned char xmm_dest, unsigned char xmm_src );
void asm_ucomisd_xmm_xmm( JitContext* context, unsigned char xmm_a, unsigned char xmm_b );
void asm_ucomisd_xmm_const( JitContext* context, unsigned char xmm, uint8_t constant_index );
void asm_jmp_rel32( JitContext* context, uint32_t rel32 );
void asm_jz_rel32( JitContext* context, uint32_t rel32 );
void asm_ret( JitContext* context );

struct Label {
	Label() {
		gen_name();
	}

	Label( unsigned char* loc ) {
		gen_name();
		location = loc;
	}

	void gen_name() {
		static int s_label_id = 0;
		name = "label_" + std::to_string( s_label_id++ );
	}

	std::string name;
	unsigned char* location;
};

void create_identifier( JitContext* context, uint32_t xmm_location, const std::string& name ) {
	context->identifiers.emplace_back( name, LocationType::location_xmm, xmm_location, context->hydrate_count, false, 0 );
}

void create_constant_identifier( JitContext* context, uint32_t xmm_location, const std::string& name, uint32_t const_index ) {
	context->identifiers.emplace_back( name, LocationType::location_xmm, xmm_location, context->hydrate_count, true, const_index );
}

Identifier* find_identifier_constant_in_xmm( JitContext* context, uint32_t constant_index ) {
	auto find_result = std::find_if( context->identifiers.begin(), context->identifiers.end(), [ constant_index ]( const Identifier& id ) {
		return id.is_constant && id.location_type == LocationType::location_xmm && id.constant_index == constant_index;
	} );

	if ( find_result == context->identifiers.end() ) {
		return NULL;
	}

	return &*find_result;
}

Identifier* find_identifier_by_name( JitContext* context, const std::string& name ) {
	auto find_result = std::find_if( context->identifiers.begin(), context->identifiers.end(), [ &name ]( const Identifier& id ) {
		return id.has_name( name );
	} );

	assert( find_result != context->identifiers.end() );

	return &*find_result;
}

void remove_identifier_by_name( JitContext* context, const std::string& name ) {
	auto find_result = std::find_if( context->identifiers.begin(), context->identifiers.end(), [ &name ]( const Identifier& id ) {
		return id.has_name( name );
	} );

	assert( find_result != context->identifiers.end() );

	find_result->names.erase( std::find( find_result->names.begin(), find_result->names.end(), name ) );

	if ( find_result->names.size() == 0 ) {
		context->identifiers.erase( find_result );
	}
}

bool jit_find_constant( JitContext* context, double constant, uint32_t* out_index ) {
	auto function = context->function;
	auto find_result = std::find( function->constants.begin(), function->constants.end(), constant );

	if ( find_result == function->constants.end() ) {
		return false;
	}

	*out_index = ( uint32_t ) std::distance( function->constants.begin(), find_result );
	return true;
}

uint32_t jit_add_constant( JitContext* context, double constant ) {
	assert( context->function->constants.size() < 32 );

	auto function = context->function;

	uint32_t constant_index;
	if ( jit_find_constant( context, constant, &constant_index ) ) {
		return constant_index;
	}

	function->constants.push_back( constant );
	return ( uint32_t ) function->constants.size() - 1;
}

int32_t jit_alloc_xmm( JitContext* context ) {
	auto xmm_available = std::vector< uint32_t >{
		REG_XMM0,
		REG_XMM1,
		REG_XMM2,
		REG_XMM3,
		REG_XMM4,
		REG_XMM5,
		REG_XMM6,
		REG_XMM7,
	};

	Identifier* spill_identifier = NULL;

	for ( auto& identifier : context->identifiers ) {
		if ( identifier.location_type == LocationType::location_xmm ) {
			// Remove from available list
			xmm_available.erase( std::find( xmm_available.begin(), xmm_available.end(), identifier.location ) );

			if ( spill_identifier == NULL || identifier.hydrate_count < spill_identifier->hydrate_count ) {
				spill_identifier = &identifier;
			}
		}
	}

	if ( xmm_available.size() > 0 ) {
		// Assign available
		return xmm_available[ 0 ];
	} else {
		// Spill first
		asm_mov_stack_xmm( context, context->spill_count++ * sizeof( double ), spill_identifier->location );

		auto previous_location = spill_identifier->location;
		
		spill_identifier->location = context->spill_count - 1;
		spill_identifier->location_type = LocationType::location_stack;

		return previous_location;
	}
}

void hydrate_identifier( JitContext* context, Identifier* identifier ) {
	if ( identifier->location_type == LocationType::location_stack ) {
		auto xmm = jit_alloc_xmm( context );

		asm_mov_xmm_stack( context, xmm, identifier->location * sizeof( double ) );

		identifier->location = xmm;
		identifier->location_type = LocationType::location_xmm;
	}

	++context->hydrate_count;
	identifier->hydrate_count = context->hydrate_count;
}

void jit_recursive( JitContext* context, AstNode* node ) {
	switch ( node->node_type ) {
	case AstNodeType::node_const: {
		assert( node->var_id_to.length() > 0 );

		auto constant_index = jit_add_constant( context, node->constant );
		auto constant_identifier = find_identifier_constant_in_xmm( context, constant_index );

		if ( constant_identifier ) {
			constant_identifier->names.push_back( node->var_id_to );
		} else {
			auto xmm = jit_alloc_xmm( context );
			asm_mov_xmm_const( context, xmm, constant_index );

			create_constant_identifier( context, xmm, node->var_id_to, constant_index );
		}
		break;
	}
	case AstNodeType::node_identifier: {
		auto referred_identifier = find_identifier_by_name( context, node->var_id_from );
		hydrate_identifier( context, referred_identifier );

		auto xmm = jit_alloc_xmm( context );
		asm_mov_xmm_xmm( context, xmm, referred_identifier->location );

		create_identifier( context, xmm, node->var_id_to );
		break;
	}
	case AstNodeType::node_sub:
	case AstNodeType::node_add: {
		int constant_node = -1;
		uint32_t const_index = -1;
		for ( uint32_t i = 0; i < 2; ++i ) {
			auto child = node->children[ i ];

			if ( child->node_type != AstNodeType::node_const ) {
				continue;
			}

			uint32_t constant_index = jit_add_constant( context, child->constant );

			if ( find_identifier_constant_in_xmm( context, constant_index ) == NULL ) {
				// Constant is loaded in the table but is not present in a register
				// Use a direct constant operation
				constant_node = i;
				const_index = constant_index;
				break;
			}
		}

		if ( constant_node != -1 ) {
			int other_node = 1 - constant_node;

			jit_recursive( context, node->children[ other_node ] );

			auto identifier = find_identifier_by_name( context, node->children[ other_node ]->var_id_to );
			hydrate_identifier( context, identifier );

			assert( identifier->location_type == LocationType::location_xmm );

			auto target_xmm = identifier->location;

			switch ( node->node_type ) {
			case AstNodeType::node_add: asm_add_xmm_const( context, target_xmm, const_index ); break;
			case AstNodeType::node_sub: asm_sub_xmm_const( context, target_xmm, const_index ); break;
			default: break;
			}

			remove_identifier_by_name( context, node->children[ other_node ]->var_id_to );
			create_identifier( context, target_xmm, node->var_id_to );
		} else {
			jit_recursive( context, node->children[ 0 ] );
			jit_recursive( context, node->children[ 1 ] );

			auto left_identifier = find_identifier_by_name( context, node->children[ 0 ]->var_id_to );
			auto right_identifier = find_identifier_by_name( context, node->children[ 1 ]->var_id_to );

			hydrate_identifier( context, left_identifier );
			hydrate_identifier( context, right_identifier );

			assert( left_identifier->location_type == LocationType::location_xmm );
			assert( right_identifier->location_type == LocationType::location_xmm );

			auto target_xmm = left_identifier->location;

			switch ( node->node_type ) {
			case AstNodeType::node_add: asm_add_xmm_xmm( context, target_xmm, right_identifier->location ); break;
			case AstNodeType::node_sub: asm_sub_xmm_xmm( context, target_xmm, right_identifier->location ); break;
			default: break;
			}

			remove_identifier_by_name( context, node->children[ 0 ]->var_id_to );
			remove_identifier_by_name( context, node->children[ 1 ]->var_id_to );

			create_identifier( context, target_xmm, node->var_id_to );
		}
		break;
	}
	case AstNodeType::node_return: {
		jit_recursive( context, node->children[ 0 ] );

		auto return_identifier = find_identifier_by_name( context, node->children[ 0 ]->var_id_to );
		hydrate_identifier( context, return_identifier );

		// Return with xmm0
		asm_mov_xmm_xmm( context, REG_XMM0, return_identifier->location );

		// Fix stack pointers
		asm_mov_reg_reg( context, REG_RSP, REG_RBP );
		asm_pop_reg( context, REG_RBP );

		asm_ret( context );
		break;
	}
	case AstNodeType::node_if: {
		Label jz_label;

		jit_recursive( context, node->children[ 0 ] );

		auto identifier = find_identifier_by_name( context, node->children[ 0 ]->var_id_to );
		hydrate_identifier( context, identifier );

		assert( identifier->location_type == LocationType::location_xmm );

		auto zero_constant = jit_add_constant( context, 0.0 );

		asm_ucomisd_xmm_const( context, identifier->location, zero_constant );
		asm_jz_rel32( context, 0x7FFFFFFF );
		jz_label.location = context->dst - 0x4;

		remove_identifier_by_name( context, node->children[ 0 ]->var_id_to );

		for ( size_t i = 1; i < node->children.size(); ++i ) {
			jit_recursive( context, node->children[ i ] );
		}

		uint32_t relative_addr = ( uint32_t ) ( context->dst - ( jz_label.location + 0x4 ) );

		encoded_value value;
		value.data.uint32[ 0 ] = relative_addr;

		asm_write_bytes( jz_label.location, 0x4,
			value.data.uint8[ 0 ],
			value.data.uint8[ 1 ],
			value.data.uint8[ 2 ],
			value.data.uint8[ 3 ]
		);

		break;
	}
	default:
		throw std::exception( "Invalid node" );
	}
}

void jit_build( std::vector< AstNode* >& ast, JitContext* context ) {
	Label lbl_const_table;
	Label lbl_local_vars;

	// Setup constants table in REG_CONST_TABLE
	asm_mov_rax_uint64( context, ( uint64_t ) 0xFFFFFFFFFFFFFFFF );
	lbl_const_table.location = context->dst - 0x8;
	asm_mov_reg_reg( context, REG_CONST_TABLE, REG_RAX );

	// Setup local vars
	asm_push_reg( context, REG_RBP );
	asm_mov_reg_reg( context, REG_RBP, REG_RSP );
	asm_sub_reg_const( context, REG_RSP, 0x7FFFFFFF );
	lbl_local_vars.location = context->dst - 0x4;

	for ( auto node : ast ) {
		jit_recursive( context, node );
	}

	encoded_value value;
	value.data.uint64[ 0 ] = ( uint64_t ) context->function->constants.data();

	asm_write_bytes( lbl_const_table.location, 0x8,
		value.data.uint8[ 0 ],
		value.data.uint8[ 1 ],
		value.data.uint8[ 2 ],
		value.data.uint8[ 3 ],
		value.data.uint8[ 4 ],
		value.data.uint8[ 5 ],
		value.data.uint8[ 6 ],
		value.data.uint8[ 7 ]
	);

	value.data.uint32[ 0 ] = context->spill_count * sizeof( double );

	asm_write_bytes( lbl_local_vars.location, 0x4, 
		value.data.uint8[ 0 ],
		value.data.uint8[ 1 ],
		value.data.uint8[ 2 ],
		value.data.uint8[ 3 ]
	);
}

bool jit_compile( std::vector< AstNode* >& ast, JitFunction* function ) {
	unsigned char* memory = ( unsigned char* ) VirtualAllocEx( ( HANDLE ) -1, NULL, 4098, MEM_COMMIT, PAGE_EXECUTE_READWRITE );

	JitContext context;
	context.function = function;
	context.spill_count = 0;
	context.hydrate_count = 0;

	context.dst = memory;
	context.function->fn = ( JitExecuteFn ) memory;

	jit_build( ast, &context );

	return true;
}

unsigned char* asm_write_bytes( unsigned char* at, uint32_t length, ... ) {
	va_list bytes;

	va_start( bytes, length );
	for ( uint32_t i = 0; i < length; ++i ) {
		*( at++ ) = ( char ) va_arg( bytes, int );
	}
	va_end( bytes );

	return at;
}

unsigned char* asm_write_byte_array( unsigned char* at, uint32_t length, unsigned char* bytes ) {
	for ( uint32_t i = 0; i < length; ++i ) {
		*( at++ ) = bytes[ i ];
	}

	return at;
}

void asm_push_reg( JitContext* context, unsigned char reg ) {
	// push <reg>
	context->dst = asm_write_bytes( context->dst, 1, 0x50 | reg );
}

void asm_pop_reg( JitContext* context, unsigned char reg ) {
	// pop <reg>
	context->dst = asm_write_bytes( context->dst, 1, 0x58 | reg );
}

void asm_mov_rax_uint64( JitContext* context, uint64_t uint64 ) {
	encoded_value value;
	value.data.uint64[ 0 ] = uint64;

	// movabs rax, ds:uint64
	context->dst = asm_write_bytes( context->dst, 10, 0x48, 0xB8,
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

void asm_mov_reg_reg( JitContext* context, unsigned char dst, unsigned char src ) {
	context->dst = asm_write_bytes( context->dst, 3, 0x48, 0x89, 0xC0 | dst | src << 3 );
}

void asm_sub_reg_const( JitContext* context, unsigned char dst, uint32_t constant ) {
	if ( constant <= 0x7F ) {
		context->dst = asm_write_bytes( context->dst, 4, 0x48, 0x83, 0xE8 | dst, ( uint8_t ) constant );
	} else if ( constant <= 0x7FFFFFFF ) {
		encoded_value value;
		value.data.uint32[ 0 ] = constant;

		if ( dst == REG_RAX ) {
			context->dst = asm_write_bytes( context->dst, 6, 0x48, 0x2D, 
				value.data.uint8[ 0 ],
				value.data.uint8[ 1 ],
				value.data.uint8[ 2 ],
				value.data.uint8[ 3 ]
			);
		} else {
			context->dst = asm_write_bytes( context->dst, 7, 0x48, 0x81, 0xE8 | dst,
				value.data.uint8[ 0 ],
				value.data.uint8[ 1 ],
				value.data.uint8[ 2 ],
				value.data.uint8[ 3 ]
			);
		}
	} else {
		throw std::exception( "x86_64 offset > 0x7FFFFFFF" );
	}
}

void asm_mov_stack_xmm( JitContext* context, uint32_t rsp_offset, unsigned char xmm_src ) {
	if ( rsp_offset == 0 ) {
		// movq QWORD PTR [rsp], <xmm>
		context->dst = asm_write_bytes( context->dst, 5, 0x66, 0x0F, 0xD6, 0x00 | ( xmm_src << 3 ) | REG_RSP, 0x24 );
	} else if ( rsp_offset <= 0x7F ) {
		// movq QWORD PTR [rsp+offset], <xmm>
		context->dst = asm_write_bytes( context->dst, 6, 0x66, 0x0F, 0xD6, 0x40 | ( xmm_src << 3 ) | REG_RSP, 0x24, ( uint8_t ) rsp_offset );
	} else if ( rsp_offset <= 0x7FFFFFFF ) {
		// movq QWORD PTR [rsp+offset], <xmm>
		encoded_value value;
		value.data.uint32[ 0 ] = rsp_offset;

		context->dst = asm_write_bytes( context->dst, 9, 0x66, 0x0F, 0xD6, 0x80 | ( xmm_src << 3 ) | REG_RSP, 0x24,
			value.data.uint8[ 0 ],
			value.data.uint8[ 1 ],
			value.data.uint8[ 2 ],
			value.data.uint8[ 3 ]
		);
	} else {
		throw std::exception( "x86_64 offset > 0x7FFFFFFF" );
	}
}

void asm_mov_xmm_stack( JitContext* context, unsigned char xmm_dst, uint32_t rsp_offset ) {
	if ( rsp_offset == 0 ) {
		// movq <xmm>, QWORD PTR [rsp]
		context->dst = asm_write_bytes( context->dst, 5, 0xF3, 0x0F, 0x7E, 0x00 | ( xmm_dst << 3 ) | REG_RSP, 0x24 );
	} else if ( rsp_offset <= 0x7F ) {
		// movq <xmm>, QWORD PTR [rsp+offset]
		context->dst = asm_write_bytes( context->dst, 6, 0xF3, 0x0F, 0x7E, 0x40 | ( xmm_dst << 3 ) | REG_RSP, 0x24, ( uint8_t ) rsp_offset );
	} else if ( rsp_offset <= 0x7FFFFFFF ) {
		// movq <xmm>, QWORD PTR [rsp+offset]
		encoded_value value;
		value.data.uint32[ 0 ] = rsp_offset;

		context->dst = asm_write_bytes( context->dst, 9, 0xF3, 0x0F, 0x7E, 0x80 | ( xmm_dst << 3 ) | REG_RSP, 0x24,
			value.data.uint8[ 0 ],
			value.data.uint8[ 1 ],
			value.data.uint8[ 2 ],
			value.data.uint8[ 3 ]
		);
	} else {
		throw std::exception( "x86_64 offset > 0x7FFFFFFF" );
	}
}

void asm_mov_reg_xmm( JitContext* context, unsigned char dst, unsigned char xmm_src ) {
	// movq <reg>, <xmm>
	context->dst = asm_write_bytes( context->dst, 5, 0x66, 0x48, 0x0F, 0x7E, 0xC0 | ( xmm_src << 3 ) | dst );
}

void asm_mov_xmm_xmm( JitContext* context, unsigned char xmm_dest, unsigned char xmm_src ) {
	// movsd <xmm>, <xmm>
	context->dst = asm_write_bytes( context->dst, 4, 0xF2, 0x0F, 0x10, 0xC0 | ( xmm_dest << 3 ) | xmm_src );
}

void asm_add_xmm_xmm( JitContext* context, unsigned char xmm_dest, unsigned char xmm_src ) {
	// addsd <xmm>, <xmm>
	context->dst = asm_write_bytes( context->dst, 4, 0xF2, 0x0F, 0x58, 0xC0 | ( xmm_dest << 3 ) | xmm_src );
}

void asm_sub_xmm_xmm( JitContext* context, unsigned char xmm_dest, unsigned char xmm_src ) {
	// subsd <xmm>, <xmm>
	context->dst = asm_write_bytes( context->dst, 4, 0xF2, 0x0F, 0x5C, 0xC0 | ( xmm_dest << 3 ) | xmm_src );
}

void asm_mov_xmm_const( JitContext* context, unsigned char xmm_dest, uint8_t constant_index ) {
	// movsd  <xmm>, QWORD PTR [REG_CONST_TABLE+constant_index]
	if ( constant_index == 0 ) {
		context->dst = asm_write_bytes( context->dst, 4, 0xF2, 0x0F, 0x10, 0x00 | ( xmm_dest << 3 ) | REG_CONST_TABLE );
	} else {
		context->dst = asm_write_bytes( context->dst, 5, 0xF2, 0x0F, 0x10, 0x40 | ( xmm_dest << 3 ) | REG_CONST_TABLE, constant_index * sizeof( double ) );
	}
}

void asm_sub_xmm_const( JitContext* context, unsigned char xmm_dest, uint8_t constant_index ) {
	// subsd  <xmm>, QWORD PTR [REG_CONST_TABLE+constant_index]
	if ( constant_index == 0 ) {
		context->dst = asm_write_bytes( context->dst, 4, 0xF2, 0x0F, 0x5C, 0x00 | ( xmm_dest << 3 ) | REG_CONST_TABLE );
	} else {
		context->dst = asm_write_bytes( context->dst, 5, 0xF2, 0x0F, 0x5C, 0x40 | ( xmm_dest << 3 ) | REG_CONST_TABLE, constant_index * sizeof( double ) );
	}
}

void asm_add_xmm_const( JitContext* context, unsigned char xmm_dest, uint8_t constant_index ) {
	// addsd  <xmm>, QWORD PTR [REG_CONST_TABLE+constant_index]
	if ( constant_index == 0 ) {
		context->dst = asm_write_bytes( context->dst, 4, 0xF2, 0x0F, 0x58, 0x00 | ( xmm_dest << 3 ) | REG_CONST_TABLE );
	} else {
		context->dst = asm_write_bytes( context->dst, 5, 0xF2, 0x0F, 0x58, 0x40 | ( xmm_dest << 3 ) | REG_CONST_TABLE, constant_index * sizeof( double ) );
	}
}

//void asm_mul_xmm_const( JitContext* context, unsigned char xmm_dest, uint8_t constant_index ) {
//	// mulsd  <xmm>, QWORD PTR [REG_CONST_TABLE+constant_index]
//	if ( constant_index == 0 ) {
//		context->dst = asm_write_bytes( context->dst, 4, 0xF2, 0x0F, 0x59, 0x00 | ( xmm_dest << 3 ) | REG_CONST_TABLE );
//	} else {
//		context->dst = asm_write_bytes( context->dst, 5, 0xF2, 0x0F, 0x59, 0x40 | ( xmm_dest << 3 ) | REG_CONST_TABLE, constant_index * sizeof( double ) );
//	}
//}
//
//void asm_div_xmm_const( JitContext* context, unsigned char xmm_dest, uint8_t constant_index ) {
//	// divsd  <xmm>, QWORD PTR [REG_CONST_TABLE+constant_index]
//	if ( constant_index == 0 ) {
//		context->dst = asm_write_bytes( context->dst, 4, 0xF2, 0x0F, 0x5E, 0x00 | ( xmm_dest << 3 ) | REG_CONST_TABLE );
//	} else {
//		context->dst = asm_write_bytes( context->dst, 5, 0xF2, 0x0F, 0x5E, 0x40 | ( xmm_dest << 3 ) | REG_CONST_TABLE, constant_index * sizeof( double ) );
//	}
//}

void asm_xor_xmm_xmm( JitContext* context, unsigned char xmm_dest, unsigned char xmm_src ) {
	context->dst = asm_write_bytes( context->dst, 4, 0x66, 0x0F, 0xEF, 0xC0 | ( xmm_dest << 3 ) | xmm_src );
}

void asm_ucomisd_xmm_xmm( JitContext* context, unsigned char xmm_a, unsigned char xmm_b ) {
	context->dst = asm_write_bytes( context->dst, 4, 0x66, 0x0F, 0x2E, 0xC0 | ( xmm_a << 3 ) | xmm_b );
}

void asm_ucomisd_xmm_const( JitContext* context, unsigned char xmm, uint8_t constant_index ) {
	if ( constant_index == 0 ) {
		context->dst = asm_write_bytes( context->dst, 4, 0x66, 0x0F, 0x2E, 0x00 | ( xmm << 3 ) | REG_CONST_TABLE );
	} else {
		context->dst = asm_write_bytes( context->dst, 5, 0x66, 0x0F, 0x2E, 0x40 | ( xmm << 3 ) | REG_CONST_TABLE, constant_index * sizeof( double ) );
	}
}

void asm_jmp_rel32( JitContext* context, uint32_t rel32 ) {
	encoded_value value;
	value.data.uint32[ 0 ] = rel32;

	// jmp <rel32>
	context->dst = asm_write_bytes( context->dst, 5, 0xE9, 
		value.data.uint8[ 0 ],
		value.data.uint8[ 1 ],
		value.data.uint8[ 2 ],
		value.data.uint8[ 3 ]
	);
}

void asm_jz_rel32( JitContext* context, uint32_t rel32 ) {
	encoded_value value;
	value.data.uint32[ 0 ] = rel32;

	// jz <rel8>
	context->dst = asm_write_bytes( context->dst, 6, 0x0F, 0x84,
		value.data.uint8[ 0 ],
		value.data.uint8[ 1 ],
		value.data.uint8[ 2 ],
		value.data.uint8[ 3 ]
	);
}

void asm_ret( JitContext* context ) {
	// ret
	context->dst = asm_write_bytes( context->dst, 1, 0xC3 );
}

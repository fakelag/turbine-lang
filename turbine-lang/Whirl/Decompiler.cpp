#define WIN32_LEAN_AND_MEAN
#include "windows.h"

#include <iostream>
#include <vector>
#include <string>
#include <regex>
#include <unordered_map>
#include <map>
#include <stack>
#include <iomanip>

#include "Decompiler.h"
#include "../Main.h"

struct StackValue {
	std::string						var_id;
	std::string						node_id;
};

struct Block {
	Block( const std::vector< uint32_t >& full_code, uint32_t from, uint32_t to ) {
		cursor = 0;
		std::copy( full_code.begin() + from, full_code.begin() + to, std::back_inserter( code ) );
	}

	Block( const std::vector< uint32_t >& full_code ) {
		cursor = 0;
		std::copy( full_code.begin(), full_code.end(), std::back_inserter( code ) );
	}

	uint32_t						cursor;
	std::vector< StackValue >		stack;
	std::vector< AstNode* >			nodes;
	std::vector< uint32_t >			code;
};

struct NodeAllocator {
	AstNode* alloc_node() {
		auto node = new AstNode;
		allocated_nodes.push_back( node );
		return node;
	}

	std::vector< AstNode* > flatten( AstNode* node ) {
		std::vector< AstNode* > flat_list;

		for ( auto child : node->children ) {
			auto flat_children = flatten( child );
			std::copy( flat_children.begin(), flat_children.end(), std::back_inserter( flat_list ) );
		}

		flat_list.push_back( node );

		return flat_list;
	}

	void prune( const std::vector< AstNode* >& ast ) {
		std::unordered_map< uintptr_t, bool > ast_map;

		for ( auto node : ast ) {
			auto flat_node = flatten( node );

			std::transform( flat_node.begin(), flat_node.end(), std::inserter( ast_map, ast_map.end() ), []( const AstNode* node ) {
				return std::make_pair( ( uintptr_t ) node, true );
			} );
		}

		for ( auto alloc : allocated_nodes ) {
			if ( ast_map.find( ( uintptr_t ) alloc ) == ast_map.end() ) {
				delete alloc;
			}
		}
	}

	std::vector< AstNode* >			allocated_nodes;
};

std::string gen_node_id() {
	static int s_node_counter = 0;
	return "node_" + std::to_string( s_node_counter++ );
}

std::string gen_var_id() {
	static int s_var_counter = 0;
	return "var_" + std::to_string( s_var_counter++ );
}

std::string gen_var_copy_id( const std::string& original_id ) {
	static int s_var_counter = 0;
	return original_id + "_copy_" + std::to_string( s_var_counter++ );
}

AstNode* alloc_simple_node( NodeAllocator& allocator, const std::string& node_id, AstNodeType node_type, AstNode* child ) {
	auto node = allocator.alloc_node();
	node->node_id = node_id;
	node->node_type = node_type;
	node->node_group = AstNodeGroup::node_simple;
	node->children = std::vector< AstNode* >{ child };
	node->is_statement = false;

	return node;
}

AstNode* alloc_complex_node( NodeAllocator& allocator, const std::string& node_id, AstNodeType node_type,
	const std::string& var_id_to, AstNode* lhs_child, AstNode* rhs_child ) {
	auto node = allocator.alloc_node();
	node->node_id = node_id;
	node->node_type = node_type;
	node->node_group = AstNodeGroup::node_complex;
	node->children = std::vector< AstNode* >{ lhs_child, rhs_child };
	node->var_id_to = var_id_to;
	node->is_statement = false;

	return node;
}

AstNode* alloc_list_node( NodeAllocator& allocator, const std::string& node_id,
	AstNodeType node_type, const std::vector< AstNode* >& children ) {
	auto node = allocator.alloc_node();
	node->node_id = node_id;
	node->node_type = node_type;
	node->node_group = AstNodeGroup::node_list;
	node->children = children;
	node->is_statement = false;

	return node;
}

AstNode* alloc_const_node( NodeAllocator& allocator, const std::string& node_id,
	AstNodeType node_type, const std::string& var_id_to, double constant ) {
	auto node = allocator.alloc_node();
	node->node_id = node_id;
	node->node_type = node_type;
	node->node_group = AstNodeGroup::node_constant;
	node->constant = constant;
	node->var_id_to = var_id_to;
	node->is_statement = false;

	return node;
}

AstNode* alloc_identifier_node( NodeAllocator& allocator, const std::string& node_id,
	const std::string& var_id_from, const std::string& var_id_to ) {
	auto node = allocator.alloc_node();
	node->node_id = node_id;
	node->node_type = AstNodeType::node_identifier;
	node->node_group = AstNodeGroup::node_name;
	node->var_id_from = var_id_from;
	node->var_id_to = var_id_to;
	node->is_statement = false;
	
	return node;
}

AstNode* alloc_assign_node( NodeAllocator& allocator, const std::string& node_id, const std::string& var_id_to, AstNode* expr_node ) {
	auto node = allocator.alloc_node();
	node->node_id = node_id;
	node->node_type = AstNodeType::node_assign;
	node->node_group = AstNodeGroup::node_complex;
	node->var_id_to = var_id_to;
	node->children = std::vector< AstNode* >{ expr_node };
	node->is_statement = true;

	return node;
}

AstNode* find_and_remove_node( std::vector< AstNode* >& mutable_nodes, const std::string& node_id, bool expressions_only ) {
	auto find_result = std::find_if( mutable_nodes.begin(), mutable_nodes.end(), [ &node_id ]( const AstNode* node ) {
		return node->node_id == node_id;
	} );

	if ( find_result == mutable_nodes.end() ) {
		throw new std::exception( ( "Node \"" + node_id + "\" not found" ).c_str() );
	}

	auto found_node = *find_result;

	if ( found_node->is_statement && expressions_only ) {
		return NULL;
	} else {
		mutable_nodes.erase( find_result );
		return found_node;
	}
}

void stack_pop( std::vector< AstNode* >& mutable_nodes, std::vector< StackValue >& mutable_stack, StackValue* out_value = NULL, AstNode** out_node = NULL ) {
	if ( mutable_stack.size() == 0 ) {
		throw new std::exception( "Invalid stack pop" );
	}

	auto stack_value = mutable_stack[ mutable_stack.size() - 1 ];
	mutable_stack.pop_back();

	auto node = find_and_remove_node( mutable_nodes, stack_value.node_id, true );

	if ( out_value ) {
		*out_value = stack_value;
	}

	if ( out_node ) {
		*out_node = node;
	}
}

void parse_block(
	NodeAllocator& allocator,
	const Block& block,
	std::vector< StackValue >* out_stack,
	std::vector< AstNode* >* out_nodes,
	bool* out_backjump = NULL
) {
	auto cursor = block.cursor;
	auto stack = block.stack;
	auto nodes = block.nodes;
	auto len = block.code.size();

	if ( out_backjump ) {
		*out_backjump = false;
	}

	while ( cursor < len ) {
		auto inst = block.code.at( cursor++ );

		switch ( inst ) {
		case OpCode::op_load_number: {
			encoded_value value;
			value.data.uint32[ 0 ] = block.code.at( cursor++ );
			value.data.uint32[ 1 ] = block.code.at( cursor++ );

			auto node_id = gen_node_id();
			auto var_id = gen_var_id();

			nodes.push_back( alloc_const_node( allocator, node_id, AstNodeType::node_const, var_id, value.data.dbl ) );
			stack.push_back( StackValue{ var_id, node_id } );
			break;
		}
		case OpCode::op_load_slot: {
			auto slot = block.code.at( cursor++ );
			auto& current = stack[ slot ];

			auto node_id = gen_node_id();
			auto var_id = gen_var_copy_id( current.var_id );

			nodes.push_back( alloc_identifier_node( allocator, node_id, current.var_id, var_id ) );
			stack.push_back( StackValue{ var_id, node_id } );
			break;
		}
		case OpCode::op_load_zero: {
			auto node_id = gen_node_id();
			auto var_id = gen_var_id();

			nodes.push_back( alloc_const_node( allocator, node_id, AstNodeType::node_const, var_id, 0.0 ) );
			stack.push_back( StackValue{ var_id, node_id } );
			break;
		}
		case OpCode::op_set_slot: {
			auto slot = block.code.at( cursor++ );

			auto& assign_dst = stack[ slot ];

			AstNode* src_node = NULL;
			StackValue assign_src;

			stack_pop( nodes, stack, &assign_src, &src_node );

			auto node_id = gen_node_id();

			auto node = alloc_assign_node(
				allocator,
				node_id,
				assign_dst.var_id,
				src_node
			);

			nodes.push_back( node );
			stack.push_back( StackValue{ gen_var_id(), node_id } );
			break;
		}
		case OpCode::op_ne:
		case OpCode::op_eq:
		case OpCode::op_div:
		case OpCode::op_mul:
		case OpCode::op_sub:
		case OpCode::op_add: {
			AstNode* right_node = NULL;
			AstNode* left_node = NULL;

			stack_pop( nodes, stack, NULL, &right_node );
			stack_pop( nodes, stack, NULL, &left_node );

			auto node_id = gen_node_id();
			auto var_id = gen_var_id();

			AstNodeType node_type;
			switch ( inst ) {
			case OpCode::op_ne: node_type = AstNodeType::node_ne; break;
			case OpCode::op_eq: node_type = AstNodeType::node_eq; break;
			case OpCode::op_div: node_type = AstNodeType::node_div; break;
			case OpCode::op_mul: node_type = AstNodeType::node_mul; break;
			case OpCode::op_sub: node_type = AstNodeType::node_sub; break;
			case OpCode::op_add: node_type = AstNodeType::node_add; break;
			default: break;
			}

			nodes.push_back( alloc_complex_node( allocator, node_id, node_type, var_id, left_node, right_node ) );
			stack.push_back( StackValue{ var_id, node_id } );
			break;
		}
		case OpCode::op_pop: {
			stack_pop( nodes, stack );
			break;
		}
		case OpCode::op_return: {
			AstNode* return_value_node = NULL;
			stack_pop( nodes, stack, NULL, &return_value_node );

			nodes.push_back( alloc_simple_node( allocator, gen_node_id(), AstNodeType::node_return, return_value_node ) );
			break;
		}
		case OpCode::op_jmp: {
			int32_t offset = ( int32_t ) block.code.at( cursor++ );

			if ( offset < 0 ) {
				if ( out_backjump ) {
					*out_backjump = true;
				}

				if ( out_stack ) {
					*out_stack = stack;
				}

				if ( out_nodes ) {
					*out_nodes = nodes;
				}

				return;
			}

			cursor += offset;
			break;
		}
		case OpCode::op_jz: {
			int32_t offset = ( int32_t ) block.code.at( cursor++ );

			if ( offset < 0 ) {
				throw std::exception( "Unhandled backwards jump" );
			}

			// Condition
			auto cond_node = std::find_if( nodes.begin(), nodes.end(), [ &stack ]( const AstNode* node ) {
				return node->node_id == stack[ stack.size() - 1 ].node_id;
			} );

			if ( cond_node == nodes.end() ) {
				throw std::exception( "Cond node not found" );
			}

			std::unordered_map< std::string, bool > current_node_ids;
			std::transform( nodes.begin(), nodes.end(), std::inserter( current_node_ids, current_node_ids.end() ), []( const AstNode* node ) {
				return std::make_pair( node->node_id, true );
			} );

			// Then-block
			Block then_block( block.code, cursor, cursor + offset );
			std::copy( stack.begin(), stack.end(), std::back_inserter( then_block.stack ) );
			std::copy( nodes.begin(), nodes.end(), std::back_inserter( then_block.nodes ) );

			std::vector< AstNode* > then_body_nodes;

			bool backjump = false;
			parse_block( allocator, then_block, NULL, &then_body_nodes, &backjump );

			std::vector< AstNode* > then_new_nodes;
			std::copy_if(
				then_body_nodes.begin(),
				then_body_nodes.end(),
				std::back_inserter( then_new_nodes ),
				[ &current_node_ids ]( const AstNode* node ) {
					return current_node_ids.find( node->node_id ) == current_node_ids.end();
				}
			);

			std::vector< AstNode* > body_nodes;
			body_nodes.push_back( *cond_node );
			std::copy( then_new_nodes.begin(), then_new_nodes.end(), std::back_inserter( body_nodes ) );

			nodes.push_back( alloc_list_node( allocator, gen_node_id(), backjump ? AstNodeType::node_while : AstNodeType::node_if, body_nodes ) );

			cursor += offset;

			// TODO: Else-branch
			if ( block.code[ cursor++ ] != OpCode::op_pop ) {
				throw std::exception( "Pop not found in else block" );
			}

			stack_pop( nodes, stack );
			break;
		}
		default:
			throw std::exception( "Unknown instruction" );
		}

		if ( inst == OpCode::op_return ) {
			break;
		}
	}

	if ( out_stack ) {
		*out_stack = stack;
	}

	if ( out_nodes ) {
		*out_nodes = nodes;
	}
};

void jit_decompile( const Function& function, std::vector< AstNode* >* out_ast ) {
	NodeAllocator allocator;

	Block block( function.code );
	parse_block( allocator, block, NULL, out_ast );

	allocator.prune( *out_ast );
}

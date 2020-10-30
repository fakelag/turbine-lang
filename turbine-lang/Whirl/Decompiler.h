#pragma once

struct Function;

enum AstNodeType {
	node_eq,
	node_ne,
	node_div,
	node_mul,
	node_sub,
	node_add,
	node_const,
	node_identifier,
	node_assign,
	node_return,
	node_if,
	node_while,
};

enum AstNodeGroup {
	node_simple,
	node_complex,
	node_list,
	node_constant,
	node_name,
};

struct AstNode {
	AstNode() { }

	std::string						node_id;
	AstNodeType						node_type;
	AstNodeGroup					node_group;

	std::vector< AstNode* >			children;
	std::string						var_id_from;
	std::string						var_id_to;
	double							constant;
	bool							is_statement;
};

void jit_decompile( const Function& function, std::vector< AstNode* >* out_ast );

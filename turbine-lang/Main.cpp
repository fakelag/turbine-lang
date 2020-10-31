#define WIN32_LEAN_AND_MEAN
#include "windows.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <regex>
#include <unordered_map>
#include <stack>
#include <iomanip>
#include <chrono>

#include "Main.h"
#include "Whirl/Decompiler.h"
#include "Whirl/x86_64Compiler.h"

/*
	Fn FuncName arg0, arg1:
		While cond:
			FuncName()
			FuncName(1);
			FuncName(1, 2, 3);
		End While
	End Fn

	Const x = 1;
	Const y = x+1*2/FuncName(32, 64);
*/

struct Disassembly;
struct Program;

void print_disassembly( const Disassembly& disasm );
bool disassemble( const Program& program, Disassembly* disasm );

struct Disassembly {
	struct OpCode {
		OpCode( uint32_t len, const std::string& instName, const std::string& instArgs ) {
			length = len;
			name = instName;
			args = instArgs;
		}

		uint32_t length;
		uint32_t address;
		std::string name;
		std::string args;
	};

	struct Fn {
		std::string name;
		std::vector< OpCode > opcodes;
	};

	std::vector< Fn > functions;
};

struct Program {
	int										global;
	int										main;
	std::vector< Function >					functions;
};

enum TokenId {
	token_identifier,
	token_number,
	token_function,
	token_while,
	token_end,
	token_plus,
	token_minus,
	token_star,
	token_slash,
	token_equals,
	token_semicolon,
	token_paren_left,
	token_paren_right,
	token_comma,
	token_colon,
	token_const,
	token_any,
	token_return,
	token_if,
	token_else,
	token_then,
	token_lessthan,
	token_morethan,
	token_2equals,
	token_notequals,
	token_eof,
};

struct Keyword {
	enum KeywordType {
		kw_operator,
		kw_word,
	};

	std::string		string;
	KeywordType		type;
	int				lbp;
	TokenId			token_type;
};

struct Token {
	const Keyword*		keyword;
	std::string			token_string;
	bool				is_last;
	int					parse_distance;
	int					lbp;
	TokenId				token_type;
};

enum ParserPrecedence {
	prec_none = 0,
	prec_assignment = 10,
	prec_equality = 20,
	prec_arithmetic_addsub = 30,
	prec_arithmetic_muldiv = 40,
	prec_left_paren = 50,
	prec_variable = 60,
};

const std::unordered_map< std::string, Keyword > keyword_list ={
	{ "Fn",			Keyword { "Fn",		Keyword::kw_word, ParserPrecedence::prec_variable,		TokenId::token_function } },
	{ "Const",		Keyword { "Const",	Keyword::kw_word, ParserPrecedence::prec_variable,		TokenId::token_const } },
	{ "Any",		Keyword { "Any",	Keyword::kw_word, ParserPrecedence::prec_variable,		TokenId::token_any } },
	{ "End",		Keyword { "End",	Keyword::kw_word, ParserPrecedence::prec_variable,		TokenId::token_end } },
	{ "Return",		Keyword { "Return",	Keyword::kw_word, ParserPrecedence::prec_none,			TokenId::token_return } },
	{ "If",			Keyword { "If",		Keyword::kw_word, ParserPrecedence::prec_none,			TokenId::token_if } },
	{ "Else",		Keyword { "Else",	Keyword::kw_word, ParserPrecedence::prec_none,			TokenId::token_else } },
	{ "Then",		Keyword { "Then",	Keyword::kw_word, ParserPrecedence::prec_none,			TokenId::token_then } },
	{ "While",		Keyword { "While",	Keyword::kw_word, ParserPrecedence::prec_none,			TokenId::token_while } },
};

const std::regex rx_double_operator = std::regex( "(==)|(!=)" );
const std::regex rx_operator = std::regex( "([\\(\\)\\+\\-\\*\\/=;:,><])" );
const std::regex rx_identifier_char = std::regex( "[_a-zA-Z0-9]" );
const std::regex rx_identifier = std::regex( "[_a-zA-Z0-9]+" );
const std::regex rx_number_char = std::regex( "[.0-9]" );
const std::regex rx_number = std::regex( "[.0-9]+" );

const std::unordered_map< std::string, Keyword > operator_list = {
	{ "+",		Keyword { "+",		Keyword::kw_operator, ParserPrecedence::prec_arithmetic_addsub,		TokenId::token_plus } },
	{ "-",		Keyword { "-",		Keyword::kw_operator, ParserPrecedence::prec_arithmetic_addsub,		TokenId::token_minus } },
	{ "/",		Keyword { "/",		Keyword::kw_operator, ParserPrecedence::prec_arithmetic_muldiv,		TokenId::token_slash } },
	{ "*",		Keyword { "*",		Keyword::kw_operator, ParserPrecedence::prec_arithmetic_muldiv,		TokenId::token_star } },
	{ ";",		Keyword { ";",		Keyword::kw_operator, ParserPrecedence::prec_none,					TokenId::token_semicolon } },
	{ "=",		Keyword { "=",		Keyword::kw_operator, ParserPrecedence::prec_assignment,			TokenId::token_equals } },
	{ "(",		Keyword { "(",		Keyword::kw_operator, ParserPrecedence::prec_left_paren,			TokenId::token_paren_left } },
	{ ")",		Keyword { ")",		Keyword::kw_operator, ParserPrecedence::prec_none,					TokenId::token_paren_right } },
	{ ":",		Keyword { ":",		Keyword::kw_operator, ParserPrecedence::prec_none,					TokenId::token_colon } },
	{ ",",		Keyword { ",",		Keyword::kw_operator, ParserPrecedence::prec_none,					TokenId::token_comma } },

	{ "<",		Keyword { "<",		Keyword::kw_operator, ParserPrecedence::prec_equality,				TokenId::token_lessthan } },
	{ ">",		Keyword { ">",		Keyword::kw_operator, ParserPrecedence::prec_equality,				TokenId::token_morethan } },
};

const std::unordered_map< std::string, Keyword > double_char_operator_list ={
	{ "==",		Keyword{ "==",		Keyword::kw_operator, ParserPrecedence::prec_equality,				TokenId::token_2equals } },
	{ "!=",		Keyword{ "!=",		Keyword::kw_operator, ParserPrecedence::prec_equality,				TokenId::token_notequals } },
};

bool scan( const std::string& input, const std::regex& mask, std::string* result ) {
	std::smatch match;
	auto res = std::regex_search( input, match, mask );

	*result = match.str();
	return res;
}

Token next_token( const std::string_view& input ) {
	auto make_token = [ &input ]( const std::string& token_string, int parse_dist, int lbp, TokenId type, const Keyword* kw ) -> Token {
		Token token;

		token.keyword = kw;
		token.token_string = token_string;
		token.parse_distance = parse_dist;
		token.is_last = false; // token.parse_distance >= ( int ) input.length();
		token.lbp = lbp;
		token.token_type = type;

		return token;
	};

	for ( size_t i = 0; i < input.length(); ++i ) {
		auto current_char = input.at( i );
		std::string scan_string = std::string( input.substr( i ) );
		std::string token_string;

		bool is_whitespace = ( current_char == '\n' || current_char == ' ' || current_char == '\t' );

		if ( is_whitespace )
			continue;

		if ( std::regex_match( scan_string.substr( 0, 2 ), rx_double_operator ) ) {
			auto keyword_string = scan_string.substr( 0, 2 );
			auto keyword = &double_char_operator_list.at( keyword_string );

			return make_token( keyword_string, i + 2, keyword->lbp, keyword->token_type, keyword );
		}

		if ( std::regex_match( std::string( 1, current_char ), rx_operator ) ) {
			auto keyword_string = std::string( 1, current_char );
			auto keyword = &operator_list.at( keyword_string );

			return make_token( keyword_string, i + 1, keyword->lbp, keyword->token_type, keyword );
		}

		if ( std::regex_match( std::string( 1, current_char ), rx_number_char ) && scan( scan_string, rx_number, &token_string ) ) {
			return make_token( token_string, i + token_string.length(), 0, TokenId::token_number, NULL );
		}

		if ( std::regex_match( std::string( 1, current_char ), rx_identifier_char ) && scan( scan_string, rx_identifier, &token_string ) ) {
			auto found_keyword = keyword_list.find( token_string );

			if ( found_keyword != keyword_list.end() ) {
				const Keyword* kw = &found_keyword->second;
				return make_token( token_string, i + token_string.length(), kw->lbp, kw->token_type, kw );
			} else {
				return make_token( token_string, i + token_string.length(), 0, TokenId::token_identifier, NULL );
			}
		}
	}

	Token eofToken;
	eofToken.keyword = NULL;
	eofToken.token_string = "";
	eofToken.parse_distance = input.length();
	eofToken.is_last = true;
	eofToken.lbp = 0;
	eofToken.token_type = TokenId::token_eof;

	return eofToken;
}

std::vector< Token > tokenize( const std::string& input ) {
	std::vector< Token > tokens;
	Token last_token;
	int token_cursor = 0;
	
	do {
		last_token = next_token( std::string_view( input ).substr( token_cursor ) );
		token_cursor += last_token.parse_distance;

		tokens.push_back( last_token );
	} while ( !last_token.is_last );

	return tokens;
}

// Parser
struct Parser {
	struct Slot {
		int							depth;
		int							slot_index;
		bool						is_defined;
		std::string					name;
		bool						is_const;
	};

	struct Label {
		Label( const std::string& label_name ) {
			name = label_name;
		}

		std::string					name;
		int32_t						patch_location;
		int32_t						target_location;
	};

	std::vector< Token >						tokens;
	std::vector< Token >::const_iterator		token_iterator;

	std::vector< Function >						functions;
	int											current_function;

	std::vector< Slot >							stack;
	int											stack_depth;
};

void parse_precedence( Parser& parser, int rbp = 0 );
void parse_number( Parser& parser );
void statement( Parser& parser );
void expression( Parser& parser );

void emit( Parser& parser, uint32_t byte ) {
	parser.functions[ parser.current_function ].code.push_back( byte );
}

void label_emplace( Parser& parser, Parser::Label& label ) {
	auto& code = parser.functions[ parser.current_function ].code;
	label.patch_location = code.size();
	code.push_back( 0 );
}

void label_bind( Parser& parser, Parser::Label& label ) {
	auto& code = parser.functions[ parser.current_function ].code;
	label.target_location = ( uint32_t ) code.size();
}

void label_patch( Parser& parser, Parser::Label& label ) {
	auto& code = parser.functions[ parser.current_function ].code;

	int32_t offset = label.target_location - ( label.patch_location + 1 );

	encoded_value value;
	value.data.int32[ 0 ] = offset;

	code[ label.patch_location ] = value.data.uint32[ 0 ];
}

const Token& advance_token( Parser& parser ) {
	return *parser.token_iterator++;
}

const Token& get_current_token( Parser& parser ) {
	return *parser.token_iterator;
}

const Token& get_previous_token( Parser& parser, int offset = 0 ) {
	return *( parser.token_iterator - 1 - offset );
}

const Parser::Slot& create_variable( Parser& parser, const std::string& name, bool is_const ) {
	parser.stack.push_back(
		Parser::Slot{
			parser.stack_depth,
			( int ) parser.stack.size(),
			false,
			name,
			is_const,
		}
	);

	return parser.stack[ parser.stack.size() - 1 ];
}

void create_scope( Parser& parser ) {
	++parser.stack_depth;
}

void destroy_scope( Parser& parser ) {
	for ( int i = ( int ) parser.stack.size() - 1; i >= 0; --i ) {
		if ( parser.stack[ i ].depth >= parser.stack_depth ) {
			parser.stack.erase( parser.stack.begin() + i );

			emit( parser, OpCode::op_pop );
		}
	}

	--parser.stack_depth;
}

void create_function( Parser& parser, const std::string& name, FunctionType type = FunctionType::fn_virtual ) {
	parser.functions.push_back( Function{ name, {}, ( int ) parser.functions.size(), type } );
	create_scope( parser );

	parser.current_function = parser.functions.size() - 1;
}

void finish_function( Parser& parser ) {
	emit( parser, OpCode::op_load_zero );
	emit( parser, OpCode::op_return );

	destroy_scope( parser );
	parser.current_function = 0;
}

bool find_variable( Parser& parser, const std::string& name, Parser::Slot* target_slot ) {
	auto find_result = std::find_if( parser.stack.begin(), parser.stack.end(), [ &name ]( const Parser::Slot& slot ) {
		return slot.name == name;
	} );

	if ( find_result == parser.stack.end() ) {
		return false;
	}

	*target_slot = *find_result;
	return true;
}

bool find_function( Parser& parser, const std::string& name, int* function_index ) {
	auto find_result = std::find_if( parser.functions.begin(), parser.functions.end(), [ &name ]( const Function& slot ) {
		return slot.name == name;
	} );

	if ( find_result == parser.functions.end() ) {
		return false;
	}

	*function_index = std::distance( parser.functions.begin(), find_result );
	return true;
}

void define_variable( Parser& parser, int slot_index ) {
	parser.stack[ slot_index ].is_defined = true;
}

void expect( Parser& parser, TokenId token, const std::string& error ) {
	if ( parser.token_iterator->token_type == token ) {
		advance_token( parser );
		return;
	}

	throw std::exception( error.c_str() );
}

bool match( Parser& parser, TokenId token ) {
	if ( parser.token_iterator->token_type == token ) {
		advance_token( parser );
		return true;
	}

	return false;
}

bool is_finished( Parser& parser ) {
	return parser.token_iterator == parser.tokens.end();
}

void emit_load_number( Parser& parser, double number ) {
	encoded_value value;
	value.data.dbl = number;

	emit( parser, OpCode::op_load_number );
	emit( parser, value.data.uint32[ 0 ] );
	emit( parser, value.data.uint32[ 1 ] );
}

void parse_number( Parser& parser ) {
	auto& token = get_previous_token( parser );
	double number = std::stod( token.token_string );

	emit_load_number( parser, number );
}

void parse_binary( Parser& parser ) {
	auto token = get_previous_token( parser );

	parse_precedence( parser, token.lbp );

	switch ( token.token_type ) {
	case TokenId::token_plus: emit( parser, OpCode::op_add ); break;
	case TokenId::token_minus: emit( parser, OpCode::op_sub );  break;
	case TokenId::token_star: emit( parser, OpCode::op_mul ); break;
	case TokenId::token_slash: emit( parser, OpCode::op_div ); break;
	case TokenId::token_lessthan: emit( parser, OpCode::op_lt ); break;
	case TokenId::token_morethan: emit( parser, OpCode::op_gt ); break;
	case TokenId::token_2equals: emit( parser, OpCode::op_eq ); break;
	case TokenId::token_notequals: emit( parser, OpCode::op_ne ); break;
	default: break;
	}
}

void parse_assignment( Parser& parser ) {
	auto identifier_token = get_previous_token( parser, 1 );

	if ( identifier_token.token_type != TokenId::token_identifier ) {
		throw std::exception( ( "Expected an identifier, got '" + identifier_token.token_string + "'" ).c_str() );
	}

	Parser::Slot slot;
	if ( !find_variable( parser, identifier_token.token_string, &slot ) ) {
		throw std::exception( ( "Identifier '" + identifier_token.token_string + "' not found" ).c_str() );
	}

	if ( !slot.is_defined ) {
		throw std::exception( ( "Can not refer to identifier '" + slot.name + "' before it is initialized" ).c_str() );
	}

	if ( slot.is_const ) {
		throw std::exception( ( "Can not reassign constant identifier '" + slot.name + "'" ).c_str() );
	}

	expression( parser );

	emit( parser, OpCode::op_set_slot );
	emit( parser, slot.slot_index );
}

void parse_identifier( Parser& parser, bool can_assign ) {
	auto identifier_token = get_previous_token( parser );

	Parser::Slot slot;
	int function_index;

	if ( find_variable( parser, identifier_token.token_string, &slot ) ) {
		if ( !slot.is_defined ) {
			throw std::exception( ( "Can not refer to identifier '" + slot.name + "' before it is initialized" ).c_str() );
		}

		if ( can_assign && match( parser, TokenId::token_equals ) ) {
			parse_assignment( parser );
		} else {
			emit( parser, OpCode::op_load_slot );
			emit( parser, slot.slot_index );
		}
	} else if ( find_function( parser, identifier_token.token_string, &function_index ) ) {
		// No-op
	} else {
		throw std::exception( ( "Identifier '" + identifier_token.token_string + "' not found" ).c_str() );
	}
}

void parse_grouping( Parser& parser ) {
	expression( parser );
	expect( parser, TokenId::token_paren_right, "Expected ')'" );
}

void parse_call( Parser& parser ) {
	auto identifier_token = get_previous_token( parser, 1 );

	if ( identifier_token.token_type != TokenId::token_identifier ) {
		throw std::exception( ( "Expected an identifier, got '" + identifier_token.token_string + "'" ).c_str() );
	}

	int function_index;
	if ( !find_function( parser, identifier_token.token_string, &function_index ) ) {
		throw std::exception( ( "Identifier '" + identifier_token.token_string + "' not found" ).c_str() );
	}

	if ( match( parser, TokenId::token_paren_right ) ) {
		emit( parser, OpCode::op_call );
		emit( parser, function_index );
		emit( parser, 0 );
	} else {
		int arg_count = 0;

		do {
			++arg_count;
			expression( parser );
		} while ( match( parser, TokenId::token_comma ) );

		emit( parser, OpCode::op_call );
		emit( parser, function_index );
		emit( parser, arg_count );

		expect( parser, TokenId::token_paren_right, "Expected ')' after argument list" );
	}
}

void parse_precedence( Parser& parser, int rbp ) {
	const Token* current_token = &advance_token( parser );

	auto can_assign = rbp <= ParserPrecedence::prec_assignment;
	
	switch ( current_token->token_type ) {
	case TokenId::token_number: parse_number( parser ); break;
	case TokenId::token_identifier: parse_identifier( parser, can_assign ); break;
	case TokenId::token_paren_left: parse_grouping( parser ); break;
	default:
		throw std::exception( "Expected oneof: token_number, token_identifier" );
	}

	while ( rbp < get_current_token( parser ).lbp ) {
		current_token = &advance_token( parser );

		switch ( current_token->token_type ) {
		case TokenId::token_plus:
		case TokenId::token_minus:
		case TokenId::token_star:
		case TokenId::token_slash:
		case TokenId::token_lessthan:
		case TokenId::token_morethan:
		case TokenId::token_2equals:
		case TokenId::token_notequals:
			parse_binary( parser );
			break;
		case TokenId::token_paren_left:
			parse_call( parser );
			break;
		default:
			throw std::exception( "Expected a binary operator" );
		}
	}
}

void expression( Parser& parser ) {
	parse_precedence( parser, ParserPrecedence::prec_assignment );
}

void const_declaration( Parser& parser ) {
	expect( parser, TokenId::token_identifier, "Expected identifier after 'Const'" );

	auto identifier_token = get_previous_token( parser );
	auto& slot = create_variable( parser, identifier_token.token_string, true );

	if ( match( parser, TokenId::token_equals ) ) {
		expression( parser );
	} else {
		emit( parser, OpCode::op_load_zero );
	}

	define_variable( parser, slot.slot_index );
	expect( parser, TokenId::token_semicolon, "Expected ';' after constant declaration" );
}

void any_declaration( Parser& parser ) {
	expect( parser, TokenId::token_identifier, "Expected identifier after 'Any'" );

	auto identifier_token = get_previous_token( parser );
	auto& slot = create_variable( parser, identifier_token.token_string, false );

	if ( match( parser, TokenId::token_equals ) ) {
		expression( parser );
	} else {
		emit( parser, OpCode::op_load_zero );
	}

	define_variable( parser, slot.slot_index );
	expect( parser, TokenId::token_semicolon, "Expected ';' after 'Any' declaration" );
}

void function_declaration( Parser& parser ) {
	expect( parser, TokenId::token_identifier, "Expected identifier after 'Fn'" );

	auto identifier_token = get_previous_token( parser );
	create_function( parser, identifier_token.token_string, FunctionType::fn_virtual );

	if ( !match( parser, TokenId::token_colon ) ) {
		do {
			expect( parser, TokenId::token_identifier, "Expected identifier or ':'" );
			auto arg_identifier = get_previous_token( parser );

			auto arg_variable = create_variable( parser, arg_identifier.token_string, true );
			define_variable( parser, arg_variable.slot_index );
		} while ( match( parser, TokenId::token_comma ) );

		expect( parser, TokenId::token_colon, "Expected ':' after argument list" );
	}

	while ( !match( parser, TokenId::token_end ) ) {
		statement( parser );
	}

	finish_function( parser );
	expect( parser, TokenId::token_function, "Expected 'Fn' after 'End'" );
}

void return_statement( Parser& parser ) {
	if ( match( parser, TokenId::token_semicolon ) ) {
		emit( parser, OpCode::op_load_zero );
		emit( parser, OpCode::op_return );
	} else {
		expression( parser );

		emit( parser, OpCode::op_return );
		expect( parser, TokenId::token_semicolon, "Expected ';' after return value" );
	}
}

void if_statement( Parser& parser ) {
	expression( parser );

	match( parser, TokenId::token_paren_right ); // skip ')'

	expect( parser, TokenId::token_then, "Expected 'Then'" );

	Parser::Label jzLabel( "jz_label" );
	Parser::Label jmpLabel( "jmp_Label" );

	emit( parser, OpCode::op_jz );
	label_emplace( parser, jzLabel );

	emit( parser, OpCode::op_pop );

	create_scope( parser );

	while ( !match( parser, TokenId::token_end ) ) {
		statement( parser );
	}

	destroy_scope( parser );

	emit( parser, OpCode::op_jmp );
	label_emplace( parser, jmpLabel );

	// Else-branch
	label_bind( parser, jzLabel );
	label_patch( parser, jzLabel );
	emit( parser, OpCode::op_pop );

	// End If
	label_bind( parser, jmpLabel );
	label_patch( parser, jmpLabel );

	expect( parser, TokenId::token_if, "Expected 'If' after 'End'" );
}

void while_statement( Parser& parser ) {
	Parser::Label jmpLabel( "jmp_label" );
	Parser::Label jzLabel( "jz_label" );
	label_bind( parser, jmpLabel );

	expression( parser );

	emit( parser, OpCode::op_jz );
	label_emplace( parser, jzLabel );

	match( parser, TokenId::token_paren_right ); // skip ')'

	expect( parser, TokenId::token_then, "Expected 'Then'" );

	emit( parser, OpCode::op_pop );

	create_scope( parser );

	while ( !match( parser, TokenId::token_end ) ) {
		statement( parser );
	}

	destroy_scope( parser );

	emit( parser, OpCode::op_jmp );
	label_emplace( parser, jmpLabel );
	label_patch( parser, jmpLabel );

	label_bind( parser, jzLabel );
	label_patch( parser, jzLabel );
	emit( parser, OpCode::op_pop );

	expect( parser, TokenId::token_while, "Expected 'While' after 'End'" );
}

void expression_statement( Parser& parser ) {
	expression( parser );
	emit( parser, OpCode::op_pop );
	expect( parser, TokenId::token_semicolon, "Expected ';' after expression" );
}

void statement( Parser& parser ) {
	if ( match( parser, TokenId::token_return ) ) {
		return_statement( parser );
	} else if ( match( parser, TokenId::token_if ) ) {
		if_statement( parser );
	}  else if ( match( parser, TokenId::token_while ) ) {
		while_statement( parser );
	} else if ( match( parser, TokenId::token_const ) ) {
		const_declaration( parser );
	} else if ( match( parser, TokenId::token_any ) ) {
		any_declaration( parser );
	} else {
		expression_statement( parser );
	}
}

void declaration( Parser& parser ) {
	if ( match( parser, TokenId::token_const ) ) {
		const_declaration( parser );
	}  else if ( match( parser, TokenId::token_function ) ) {
		function_declaration( parser );
	} else {
		throw std::exception( ( "Expected a declaration, got: '" + get_current_token( parser ).token_string + "'" ).c_str() );
	}
}

void parse( const std::vector< Token >& tokens, Program* program ) {
	Parser parser;

	parser.tokens = tokens;
	parser.token_iterator = parser.tokens.begin();
	parser.stack_depth = 0;

	create_function( parser, "<global>", FunctionType::fn_global );

	while ( !match( parser, token_eof ) ) {
		declaration( parser );
	}

	finish_function( parser );

	program->functions = parser.functions;
	program->global = 0;

	auto main_function = std::find_if( program->functions.begin(), program->functions.end(), []( const Function& fn ) {
		return fn.name == "Main";
	} );

	if ( main_function == program->functions.end() ) {
		throw std::exception( "Missing 'Main' method" );
	}

	main_function->type = FunctionType::fn_main;
	program->main = std::distance( program->functions.begin(), main_function );
}

struct VM {
	struct Frame {
		const uint32_t*		code;
		const uint32_t*		ip;
		double*				base;
	};

	double*							stack;
	double*							stack_top;
	std::vector< Frame >			frames;
	Program							program;
};

double stack_pop( VM& vm ) {
	--vm.stack_top;

	if ( vm.stack_top < vm.stack ) {
		throw std::exception( "Stack underflow" );
	}

	return *vm.stack_top;
}

void stack_push( VM& vm, double value ) {
	*vm.stack_top = value;
	++vm.stack_top;

	if ( vm.stack_top - vm.stack >= 255 ) {
		throw std::exception( "Maximum VM stack size exceeded" );
	}
}

#define arit_op( op ) { \
	auto b = stack_pop( vm ); \
	auto a = stack_pop( vm ); \
	stack_push( vm, a op b ); \
}

#define binary_op( op ) { \
	auto b = stack_pop( vm ); \
	auto a = stack_pop( vm ); \
	stack_push( vm, a op b ? 1.0 : 0.0 ); \
}

double execute( VM& vm, const Function& fn ) {
	const uint32_t* code = fn.code.data();
	double* base = vm.stack;

	for ( const uint32_t* ip = code;; ++ip ) {
		switch ( *ip ) {
		case OpCode::op_add: arit_op( + ); break;
		case OpCode::op_sub: arit_op( - ); break;
		case OpCode::op_mul: arit_op( * ); break;
		case OpCode::op_div: arit_op( / ); break;
		case OpCode::op_gt: binary_op( > ); break;
		case OpCode::op_lt: binary_op( < ); break;
		case OpCode::op_eq: binary_op( == ); break;
		case OpCode::op_ne: binary_op( != ); break;
		case OpCode::op_load_number: {
			encoded_value value;
			value.data.uint32[ 0 ] = *++ip;
			value.data.uint32[ 1 ] = *++ip;

			stack_push( vm, value.data.dbl );
			break;
		}
		case OpCode::op_load_zero: stack_push( vm, 0.0 ); break;
		case OpCode::op_load_slot: stack_push( vm, base[ *++ip ] ); break;
		case OpCode::op_set_slot: base[ *++ip ] = vm.stack_top[ -1 ]; break;
		case OpCode::op_pop: stack_pop( vm ); break;
		case OpCode::op_return: {
			auto return_value = stack_pop( vm );

			if ( vm.frames.size() == 0 ) {
				return return_value;
			}
			
			auto& return_frame = vm.frames[ vm.frames.size() - 1 ];

			vm.stack_top = base;
			base = return_frame.base;
			code = return_frame.code;
			ip = return_frame.ip;

			stack_push( vm, return_value );

			vm.frames.pop_back();
			break;
		}
		case OpCode::op_call: {
			auto function_index = *++ip;
			auto arg_count = *++ip;

			vm.frames.push_back( VM::Frame{ code, ip, base } );

			auto& function = vm.program.functions[ function_index ];

			base = vm.stack_top - arg_count;
			code = function.code.data();
			ip = code - 1;
			break;
		}
		case OpCode::op_jz: {
			encoded_value value;
			value.data.uint32[ 0 ] = *++ip;

			if ( vm.stack_top[ -1 ] == 0.0 ) {
				ip += value.data.int32[ 0 ];
			}

			break;
		}
		case OpCode::op_jmp: {
			encoded_value value;
			value.data.uint32[ 0 ] = *++ip;
			ip += value.data.int32[ 0 ];
			break;
		}
		default:
			throw std::exception( ( "Invalid instruction '" + std::to_string( *ip ) + "'" ).c_str() );
		}
	}
}

double run( Program program ) {
	VM vm;
	vm.program = program;
	vm.stack = new double[ 255 ];
	vm.stack_top = &vm.stack[ 0 ];

	execute( vm, vm.program.functions[ vm.program.global ] );

	auto time_start = std::chrono::steady_clock::now();
	auto return_value = execute( vm, vm.program.functions[ vm.program.main ] );
	auto time_end = std::chrono::steady_clock::now();

	auto d_s = std::chrono::duration_cast< std::chrono::milliseconds >( time_end - time_start );
	std::cout << "Interpreter took " << d_s.count() << " ms" << std::endl;

	delete vm.stack;
	return return_value;
}

std::string read_file( const std::string& path ) {
	std::string content = "";
	std::ifstream file_stream;

	file_stream.open( path );

	if ( file_stream.is_open() )
	{
		std::string line;
		while ( std::getline( file_stream, line ) )
			content += line + "\n";

		file_stream.close();
	} else
	{
		throw std::exception( "File not found" );
	}

	return content;
}

int main( int argc, char** argv ) {
	while ( true ) {
		std::string input = read_file( "F:\\Projects\\turbine-lang\\test.tb" );
		//std::getline( std::cin, input );

		try {
			auto tokens = tokenize( input );

			std::cout << "========== Tokenization ==========" << std::endl;

			for ( auto token : tokens ) {
				std::cout << ( token.keyword ? token.keyword->string : token.token_string ) << std::endl;
			}

			std::cout << "# of tokens: " << tokens.size() << std::endl;

			std::cout << "========== Compiler ==========" << std::endl;

			Program program;
			parse( tokens, &program );

			Disassembly disasm;
			if ( disassemble( program, &disasm ) ) {
				print_disassembly( disasm );
				std::cout << std::endl;
			} else {
				std::cout << "Disassembler: invalid bytecode" << std::endl;
			}

			uint32_t global_size = program.functions[ program.global ].code.size();
			uint32_t main_size = program.functions[ program.main ].code.size();

			std::cout << "# of functions " << program.functions.size() << std::endl;
			std::cout << "size of code (global scope): " << global_size << " (" << ( global_size * sizeof( uint32_t ) ) << " bytes)" << std::endl;
			std::cout << "size of code (Main): " << main_size << " (" << ( main_size * sizeof( uint32_t ) ) << " bytes)" << std::endl;

			std::cout << "========== Decompilation ==========" << std::endl;

			std::vector< AstNode* > ast;
			jit_decompile( program.functions[ program.main ], &ast );

			JitFunction jit_function;
			jit_compile( ast, &jit_function );

			auto time_start = std::chrono::steady_clock::now();
			DebugBreak();
			std::cout << "Jit result: " << jit_function.fn() << std::endl;
			auto time_end = std::chrono::steady_clock::now();
			auto d_s = std::chrono::duration_cast< std::chrono::milliseconds >( time_end - time_start );
			std::cout << "JIT took " << d_s.count() << " ms" << std::endl;

			std::cout << "========== Execution (VM) ==========" << std::endl;

			auto result = run( program );
			std::cout << "Return: " << result << std::endl;
		} catch ( const std::exception& err ) {
			std::cout << "Error: " << err.what() << std::endl;
		}

		std::cout << "========== Done ==========" << std::endl;
		break;
	}

	std::getchar();
	return 0;
}

bool disassemble( const Program& program, Disassembly* disasm ) {
	for ( auto fn : program.functions ) {
		disasm->functions.push_back( Disassembly::Fn{ fn.name, {} } );

		auto& opcodes = disasm->functions[ disasm->functions.size() - 1 ].opcodes;

		for (
			const uint32_t* ip = fn.code.data();
			( uint32_t ) ip - ( uint32_t ) fn.code.data() < fn.code.size() * sizeof( uint32_t );
			++ip
		) {
			uint32_t instruction_ip = ( uint32_t ) ip;
			switch ( *ip ) {
			case OpCode::op_add: opcodes.push_back( Disassembly::OpCode( 1, "op_add", "" ) ); break;
			case OpCode::op_sub: opcodes.push_back( Disassembly::OpCode( 1, "op_sub", "" ) ); break;
			case OpCode::op_mul: opcodes.push_back( Disassembly::OpCode( 1, "op_mul", "" ) ); break;
			case OpCode::op_div: opcodes.push_back( Disassembly::OpCode( 1, "op_div", "" ) ); break;
			case OpCode::op_gt: opcodes.push_back( Disassembly::OpCode( 1, "op_gt", "" ) ); break;
			case OpCode::op_lt: opcodes.push_back( Disassembly::OpCode( 1, "op_lt", "" ) ); break;
			case OpCode::op_eq: opcodes.push_back( Disassembly::OpCode( 1, "op_eq", "" ) ); break;
			case OpCode::op_ne: opcodes.push_back( Disassembly::OpCode( 1, "op_ne", "" ) ); break;
			case OpCode::op_load_number: {
				encoded_value value;
				value.data.uint32[ 0 ] = *++ip;
				value.data.uint32[ 1 ] = *++ip;

				opcodes.push_back( Disassembly::OpCode( 3, "op_load_number", std::to_string( value.data.dbl ) ) );
				break;
			}
			case OpCode::op_load_zero: opcodes.push_back( Disassembly::OpCode( 1, "op_load_zero", "" ) ); break;
			case OpCode::op_load_slot: {
				auto slot = *++ip;
				opcodes.push_back( Disassembly::OpCode( 2, "op_load_slot", std::to_string( slot ) ) );
				break;
			}
			case OpCode::op_set_slot: {
				auto slot_index = *++ip;
				opcodes.push_back( Disassembly::OpCode( 2, "op_set_slot", std::to_string( slot_index ) ) );
				break;
			}
			case OpCode::op_pop: opcodes.push_back( Disassembly::OpCode( 1, "op_pop", "" ) ); break;
			case OpCode::op_return: opcodes.push_back( Disassembly::OpCode( 1, "op_return", "" ) ); break;
			case OpCode::op_call: {
				auto function_index = *++ip;
				auto arg_count = *++ip;

				opcodes.push_back( Disassembly::OpCode( 3, "op_call", std::to_string( function_index ) + ", " + std::to_string( arg_count ) ) );
				break;
			}
			case OpCode::op_jmp:
			case OpCode::op_jz: {
				encoded_value value;
				value.data.uint32[ 0 ] = *++ip;

				uint32_t address = ( uint32_t ) ip + ( value.data.int32[ 0 ] * sizeof( uint32_t ) );
				uint32_t relative_address = address - ( uint32_t ) fn.code.data() + sizeof( uint32_t );

				opcodes.push_back( Disassembly::OpCode( 2, ip[ -1 ] == OpCode::op_jz ? "op_jz" : "op_jmp",
					std::to_string( value.data.int32[ 0 ] ) +", -> " + std::to_string( relative_address ) ) );
				break;
			}
			default:
				return false;
			}

			opcodes[ opcodes.size() - 1 ].address = instruction_ip - ( uint32_t ) fn.code.data();
		}
	}

	return true;
}

void print_disassembly ( const Disassembly& disasm ) {
	for ( auto fn : disasm.functions ) {
		std::cout << std::endl;
		std::cout << "Function " << fn.name << ":" << std::endl;

		for ( auto opcode : fn.opcodes ) {
			std::cout
				<< std::setfill( '0' ) << std::right << std::setw( 4 )
				<< opcode.address << " "
				<< std::setfill( ' ' ) << std::left << std::setw( 30 )
				<< opcode.name << " "
				<< std::setfill( ' ' ) << std::setw( 40 )
				<< ( opcode.args.length() > 0 ? "[" + opcode.args + "]" : "" )
				<< std::endl;
		}
	}
}

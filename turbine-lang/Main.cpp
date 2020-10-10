#include <iostream>
#include <vector>
#include <string>
#include <regex>
#include <unordered_map>
#include <stack>

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

#define encode_double_0( dbl ) (uint32_t)(( *(uint64_t*)(&dbl) & 0xFFFFFFFF00000000LL) >> 32);
#define encode_double_1( dbl ) (uint32_t)(( *(uint64_t*)(&dbl) & 0xFFFFFFFFLL));
#define decode_double( target, part0, part1 ) (*reinterpret_cast<uint64_t*>(&target) = ((uint64_t)part0 << 32 | part1));

enum FunctionType {
	fn_global,
	fn_main,
	fn_virtual,
};

struct Function {
	std::string								name;
	std::vector< uint32_t >					code;
	int										index;
	FunctionType							type;
};

struct Program {
	int										global;
	int										main;
	std::vector< Function >					functions;
};

enum OpCode : uint32_t {
	op_add,
	op_sub,
	op_mul,
	op_div,
	op_load_number,
	op_load_zero,
	op_load_slot,
	op_pop,
	op_return,
	op_call,
};

enum TokenType {
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
	token_return,
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
	TokenType		token_type;
};

struct Token {
	const Keyword*		keyword;
	std::string			token_string;
	bool				is_last;
	int					parse_distance;
	int					lbp;
	TokenType			token_type;
};

enum ParserPrecedence {
	prec_none = 0,
	prec_assignment = 10,
	prec_arithmetic_addsub = 20,
	prec_arithmetic_muldiv = 30,
	prec_left_paren = 40,
	prec_variable = 50,
};

const std::unordered_map< std::string, Keyword > keyword_list ={
	{ "Fn",			Keyword { "Fn",		Keyword::kw_word, ParserPrecedence::prec_variable,		TokenType::token_function } },
	{ "Const",		Keyword { "Const",	Keyword::kw_word, ParserPrecedence::prec_variable,		TokenType::token_const } },
	{ "End",		Keyword { "End",	Keyword::kw_word, ParserPrecedence::prec_variable,		TokenType::token_end } },
	{ "Return",		Keyword { "Return",	Keyword::kw_word, ParserPrecedence::prec_none,			TokenType::token_return } },
};

const std::regex rx_operator = std::regex( "([\\(\\)\\+\\-\\*\\/=;:,])" );
const std::regex rx_identifier_char = std::regex( "[_a-zA-Z0-9]" );
const std::regex rx_identifier = std::regex( "[_a-zA-Z0-9]+" );
const std::regex rx_number_char = std::regex( "[.0-9]" );
const std::regex rx_number = std::regex( "[.0-9]+" );

const std::unordered_map< std::string, Keyword > operator_list = {
	{ "+", Keyword { "+", Keyword::kw_operator, ParserPrecedence::prec_arithmetic_addsub,		TokenType::token_plus } },
	{ "-", Keyword { "-", Keyword::kw_operator, ParserPrecedence::prec_arithmetic_addsub,		TokenType::token_minus } },
	{ "/", Keyword { "/", Keyword::kw_operator, ParserPrecedence::prec_arithmetic_muldiv,		TokenType::token_slash } },
	{ "*", Keyword { "*", Keyword::kw_operator, ParserPrecedence::prec_arithmetic_muldiv,		TokenType::token_star } },
	{ ";", Keyword { ";", Keyword::kw_operator, ParserPrecedence::prec_none,					TokenType::token_semicolon } },
	{ "=", Keyword { "=", Keyword::kw_operator, ParserPrecedence::prec_assignment,				TokenType::token_equals } },
	{ "(", Keyword { "(", Keyword::kw_operator, ParserPrecedence::prec_left_paren,				TokenType::token_paren_left } },
	{ ")", Keyword { ")", Keyword::kw_operator, ParserPrecedence::prec_none,					TokenType::token_paren_right } },
	{ ":", Keyword { ":", Keyword::kw_operator, ParserPrecedence::prec_none,					TokenType::token_colon } },
	{ ",", Keyword { ",", Keyword::kw_operator, ParserPrecedence::prec_none,					TokenType::token_comma } },
};

bool scan( const std::string& input, const std::regex& mask, std::string* result ) {
	std::smatch match;
	auto res = std::regex_search( input, match, mask );

	*result = match.str();
	return res;
}

Token next_token( const std::string_view& input ) {
	auto make_token = [ &input ]( const std::string& token_string, int parse_dist, int lbp, TokenType type, const Keyword* kw ) -> Token {
		Token token;

		token.keyword = kw;
		token.token_string = token_string;
		token.parse_distance = parse_dist;
		token.is_last = token.parse_distance >= ( int ) input.length();
		token.lbp = lbp;
		token.token_type = type;

		return token;
	};

	for ( size_t i = 0; i < input.length(); ++i ) {
		auto current_char = input.at( i );
		std::string scan_string = std::string( input.substr( i ) );
		std::string token_string;

		bool is_whitespace = ( current_char == '\n' || current_char == ' ' );
		bool is_last = ( i == input.length() - 1 );

		if ( is_whitespace )
			continue;

		if ( std::regex_match( std::string( 1, current_char ), rx_operator ) ) {
			auto keyword_string = std::string( 1, current_char );
			auto keyword = &operator_list.at( keyword_string );

			return make_token( keyword_string, i + 1, keyword->lbp, keyword->token_type, keyword );
		}

		if ( std::regex_match( std::string( 1, current_char ), rx_number_char ) && scan( scan_string, rx_number, &token_string ) ) {
			return make_token( token_string, i + token_string.length(), 0, TokenType::token_number, NULL );
		}

		if ( std::regex_match( std::string( 1, current_char ), rx_identifier_char ) && scan( scan_string, rx_identifier, &token_string ) ) {
			auto found_keyword = keyword_list.find( token_string );

			if ( found_keyword != keyword_list.end() ) {
				const Keyword* kw = &found_keyword->second;
				return make_token( token_string, i + token_string.length(), kw->lbp, kw->token_type, kw );
			} else {
				return make_token( token_string, i + token_string.length(), 0, TokenType::token_identifier, NULL );
			}
		}
	}

	throw std::exception( "Parsing past EOF" );
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

	Token eofToken;

	eofToken.keyword = NULL;
	eofToken.token_string = "";
	eofToken.parse_distance = input.length();
	eofToken.is_last = true;
	eofToken.lbp = 0;
	eofToken.token_type = TokenType::token_eof;

	tokens.push_back( eofToken );

	return tokens;
}

// Parser
struct Parser {
	struct Slot {
		int							depth;
		int							local_index;
		int							slot_index;
		bool						is_defined;
		std::string					name;
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

const Token& advance_token( Parser& parser ) {
	return *parser.token_iterator++;
}

const Token& get_current_token( Parser& parser ) {
	return *parser.token_iterator;
}

const Token& get_previous_token( Parser& parser, int offset = 0 ) {
	return *( parser.token_iterator - 1 - offset );
}

int get_local_index_for_depth( Parser& parser, int depth = 0 ) {
	int local_index = -1;
	for ( auto it = parser.stack.rbegin(); it != parser.stack.rend(); ++it ) {
		if ( it->depth == depth ) {
			local_index = it->local_index;
			break;
		}
	}

	return local_index;
}

const Parser::Slot& create_variable( Parser& parser, const std::string& name ) {
	parser.stack.push_back(
		Parser::Slot{
			parser.stack_depth,
			get_local_index_for_depth( parser, parser.stack_depth ) + 1,
			( int ) parser.stack.size(),
			false,
			name,
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
			printf( "emit op_pop\n" );
		}
	}

	--parser.stack_depth;
}

void create_function( Parser& parser, const std::string& name, FunctionType type = FunctionType::fn_virtual ) {
	parser.functions.push_back( Function{ name, {}, ( int ) parser.functions.size(), type } );
	create_scope( parser );

	parser.current_function = parser.functions.size() - 1;

	printf( ">> Compiling function: %s\n", name.c_str() );
}

void finish_function( Parser& parser ) {
	emit( parser, OpCode::op_load_zero );
	emit( parser, OpCode::op_return );
	printf( "emit op_load_zero\n" );
	printf( "emit op_return\n" );

	printf( "<< Finished function: %s\n", parser.functions[ parser.current_function ].name.c_str() );

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

void expect( Parser& parser, TokenType token, const std::string& error ) {
	if ( parser.token_iterator->token_type == token ) {
		advance_token( parser );
		return;
	}

	throw std::exception( error.c_str() );
}

bool match( Parser& parser, TokenType token ) {
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
	uint32_t part0 = encode_double_0( number );
	uint32_t part1 = encode_double_1( number );

	emit( parser, OpCode::op_load_number );
	emit( parser, part0 );
	emit( parser, part1 );

	printf( "emit op_load_number: [%x, %x]: %.4f\n", part0, part1, number );
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
	case TokenType::token_plus: emit( parser, OpCode::op_add ); printf( "emit op_add\n" ); break;
	case TokenType::token_minus: emit( parser, OpCode::op_sub ); printf( "emit op_sub\n" ); break;
	case TokenType::token_star: emit( parser, OpCode::op_mul ); printf( "emit op_mul\n" ); break;
	case TokenType::token_slash: emit( parser, OpCode::op_div ); printf( "emit op_div\n" ); break;
	default: break;
	}
}

void parse_identifier( Parser& parser ) {
	auto identifier_token = get_previous_token( parser );

	Parser::Slot slot;
	int function_index;

	if ( find_variable( parser, identifier_token.token_string, &slot ) ) {
		if ( !slot.is_defined ) {
			throw std::exception( ( "Can not refer to identifier '" + slot.name + "' before it is initialized" ).c_str() );
		}

		emit( parser, OpCode::op_load_slot );
		emit( parser, slot.local_index );
		printf( "emit op_load_slot [%i]\n", slot.local_index );
	} else if ( find_function( parser, identifier_token.token_string, &function_index ) ) {
		// No-op
	} else {
		throw std::exception( ( "Identifier '" + identifier_token.token_string + "' not found" ).c_str() );
	}
}

void parse_call( Parser& parser ) {
	auto identifier_token = get_previous_token( parser, 1 );

	if ( identifier_token.token_type != TokenType::token_identifier ) {
		throw std::exception( ( "Expected an identifier, got '" + identifier_token.token_string + "'" ).c_str() );
	}

	int function_index;
	if ( !find_function( parser, identifier_token.token_string, &function_index ) ) {
		throw std::exception( ( "Identifier '" + identifier_token.token_string + "' not found" ).c_str() );
	}

	if ( match( parser, TokenType::token_paren_right ) ) {
		emit( parser, OpCode::op_call );
		emit( parser, function_index );
		emit( parser, 0 );

		printf( "emit op_call [%i, 0]\n", function_index );
	} else {
		int arg_count = 0;

		do {
			++arg_count;
			expression( parser );
		} while ( match( parser, TokenType::token_comma ) );

		emit( parser, OpCode::op_call );
		emit( parser, function_index );
		emit( parser, arg_count );

		printf( "emit op_call [%i, %i]\n", function_index, arg_count );
		expect( parser, TokenType::token_paren_right, "Expected ')' after argument list" );
	}
}

void parse_precedence( Parser& parser, int rbp ) {
	const Token* current_token = &advance_token( parser );
	
	switch ( current_token->token_type ) {
	case TokenType::token_number: parse_number( parser ); break;
	case TokenType::token_identifier: parse_identifier( parser ); break;
	default:
		throw std::exception( "Expected oneof: token_number, token_identifier" );
	}

	while ( rbp < get_current_token( parser ).lbp ) {
		current_token = &advance_token( parser );

		switch ( current_token->token_type ) {
		case TokenType::token_plus:
		case TokenType::token_minus:
		case TokenType::token_star:
		case TokenType::token_slash:
			parse_binary( parser );
			break;
		case TokenType::token_paren_left:
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
	expect( parser, TokenType::token_identifier, "Expected identifier after 'Const'" );

	auto identifier_token = get_previous_token( parser );
	auto& slot = create_variable( parser, identifier_token.token_string );

	if ( match( parser, TokenType::token_equals ) ) {
		expression( parser );
	} else {
		emit( parser, OpCode::op_load_zero );
		printf( "emit op_load_zero\n" );
	}

	define_variable( parser, slot.slot_index );
	expect( parser, TokenType::token_semicolon, "Expected ';' after constant declaration" );
}

void function_declaration( Parser& parser ) {
	expect( parser, TokenType::token_identifier, "Expected identifier after 'Fn'" );

	auto identifier_token = get_previous_token( parser );
	create_function( parser, identifier_token.token_string, FunctionType::fn_virtual );

	if ( !match( parser, TokenType::token_colon ) ) {
		do {
			expect( parser, TokenType::token_identifier, "Expected identifier or ':'" );
			auto arg_identifier = get_previous_token( parser );

			auto arg_variable = create_variable( parser, arg_identifier.token_string );
			define_variable( parser, arg_variable.slot_index );
		} while ( match( parser, TokenType::token_comma ) );

		expect( parser, TokenType::token_colon, "Expected ':' after argument list" );
	}

	while ( !match( parser, token_end ) ) {
		statement( parser );
	}

	finish_function( parser );
	expect( parser, TokenType::token_function, "Expected 'Fn' after 'End'" );
}

void return_statement( Parser& parser ) {
	if ( match( parser, TokenType::token_semicolon ) ) {
		emit( parser, OpCode::op_load_zero );
		emit( parser, OpCode::op_return );

		printf( "emit op_load_zero\n" );
		printf( "emit op_return\n" );
	} else {
		expression( parser );

		emit( parser, OpCode::op_return );
		printf( "emit op_return\n" );

		expect( parser, TokenType::token_semicolon, "Expected ';' after return value" );
	}
}

void statement( Parser& parser ) {
	if ( match( parser, TokenType::token_return ) ) {
		return_statement( parser );
	} else {
		throw std::exception( ( "Expected statement, got '" + get_current_token( parser ).token_string + "'" ).c_str() );
	}
}

void declaration( Parser& parser ) {
	if ( match( parser, TokenType::token_const ) ) {
		const_declaration( parser );
	}  else if ( match( parser, TokenType::token_function ) ) {
		function_declaration( parser );
	} else {
		statement( parser );
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

#define arithmetic_op( op ) { \
	auto b = stack_pop( vm ); \
	auto a = stack_pop( vm ); \
	stack_push( vm, a op b ); \
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

double stack_push( VM& vm, double value ) {
	*vm.stack_top = value;
	++vm.stack_top;

	if ( vm.stack_top - vm.stack >= 255 ) {
		throw std::exception( "Maximum VM stack size exceeded" );
	}
}

double execute( VM& vm, const Function& fn ) {
	const uint32_t* code = fn.code.data();
	double* base = vm.stack;

	for ( const uint32_t* ip = code;; ++ip ) {
		switch ( *ip ) {
		case op_add: arithmetic_op( + ); break;
		case op_sub: arithmetic_op( - ); break;
		case op_mul: arithmetic_op( * ); break;
		case op_div: arithmetic_op( / ); break;
		case op_load_number: {
			auto part0 = *++ip;
			auto part1 = *++ip;

			double result;
			decode_double( result, part0, part1 );

			stack_push( vm, result );
			break;
		}
		case op_load_zero: stack_push( vm, 0.0 ); break;
		case op_load_slot: stack_push( vm, base[ *++ip ] ); break;
		case op_pop: stack_pop( vm ); break;
		case op_return: {
			if ( vm.frames.size() == 0 ) {
				return stack_pop( vm );
			}
			
			auto& return_frame = vm.frames[ vm.frames.size() - 1 ];

			base = return_frame.base;
			code = return_frame.code;
			ip = return_frame.ip;

			vm.frames.pop_back();
			break;
		}
		case op_call: {
			auto function_index = *++ip;
			auto arg_count = *++ip;

			vm.frames.push_back( VM::Frame{ code, ip, base } );

			auto& function = vm.program.functions[ function_index ];

			base = vm.stack_top - arg_count;
			code = function.code.data();
			ip = code - 1;
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
	auto return_value = execute( vm, vm.program.functions[ vm.program.main ] );

	delete vm.stack;
	return return_value;
}

int main( int argc, char** argv ) {
	while ( true ) {
		std::string input;
		std::getline( std::cin, input );

		try {
			auto tokens = tokenize( input );

			printf( "========== Tokenization ==========\n" );

			for ( auto token : tokens ) {
				std::cout << ( token.keyword ? token.keyword->string : token.token_string ) << std::endl;
			}

			std::cout << "# of tokens: " << tokens.size() << std::endl;

			printf( "========== Compiler ==========\n" );

			Program program;
			parse( tokens, &program );

			std::cout << "# of functions " << program.functions.size() << std::endl;
			std::cout << "size of code (global scope): " << program.functions[ program.global ].code.size() << std::endl;
			std::cout << "size of code (Main): " << program.functions[ program.main ].code.size() << std::endl;

			printf( "========== Execution (VM) ==========\n" );

			auto result = run( program );
			printf( "Return: %f\n", result );
		} catch ( const std::exception& err ) {
			printf( "Error: %s\n", err.what() );
		}

		printf( "========== Done ==========\n" );
	}

	return 0;
}

#include <iostream>
#include <vector>
#include <string>
#include <regex>
#include <unordered_map>
#include <stack>

/*
	Fn FuncName arg0, arg1:
		While cond:
			Call FuncName;
			Call FuncName 1;
			Call FuncName (1, 2, 3);
		End While
	End Fn

	Const x = 1;
	Const y = x+1*2/FuncName(32, 64);
*/

#define encode_double_0( dbl ) (uint32_t)(( *(uint64_t*)(&dbl) & 0xFFFFFFFF00000000LL) >> 32);
#define encode_double_1( dbl ) (uint32_t)(( *(uint64_t*)(&dbl) & 0xFFFFFFFFLL));
#define decode_double( target, part0, part1 ) (*reinterpret_cast<uint64_t*>(&target) = ((uint64_t)part0 << 32 | part1));

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
		int							index;
		bool						is_defined;
		std::string					name;
	};

	std::vector< Token >					tokens;
	std::vector< Token >::const_iterator	iterator;

	std::vector< uint32_t >*				code;
	std::vector< Slot >						stack;
	int										stack_depth;
};

void parse_precedence( Parser& parser, int rbp = 0 );
void parse_number( Parser& parser );

void emit( Parser& parser, uint32_t byte ) {
	parser.code->push_back( byte );
}

const Token& advance_token( Parser& parser ) {
	return *parser.iterator++;
}

const Token& get_current_token( Parser& parser ) {
	return *parser.iterator;
}

const Token& get_previous_token( Parser& parser ) {
	return *( parser.iterator - 1 );
}

const Parser::Slot& create_variable( Parser& parser, const std::string& name ) {
	parser.stack.push_back( Parser::Slot{ parser.stack_depth, ( int ) parser.stack.size(), false, name } );
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

void define_variable( Parser& parser, int slot ) {
	parser.stack[ slot ].is_defined = true;
}

void expect( Parser& parser, TokenType token, const std::string& error ) {
	if ( parser.iterator->token_type == token ) {
		advance_token( parser );
		return;
	}

	throw std::exception( error.c_str() );
}

bool match( Parser& parser, TokenType token ) {
	if ( parser.iterator->token_type == token ) {
		advance_token( parser );
		return true;
	}

	return false;
}

bool is_finished( Parser& parser ) {
	return parser.iterator == parser.tokens.end();
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
	if ( find_variable( parser, identifier_token.token_string, &slot ) ) {
		if ( !slot.is_defined ) {
			throw std::exception( ( "Can not refer to identifier '" + slot.name + "' before it is initialized" ).c_str() );
		}

		emit( parser, OpCode::op_load_slot );
		emit( parser, slot.index );
		printf( "emit op_load_slot [%i]\n", slot.index );
	} else {
		throw std::exception( ( "Identifier '" + identifier_token.token_string + "' not found" ).c_str() );
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

	expect( parser, TokenType::token_semicolon, "Expected ';' after constant declaration" );

	define_variable( parser, slot.index );
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
	} else {
		statement( parser );
	}
}

bool parse( const std::vector< Token >& tokens, std::vector< uint32_t >* code ) {
	Parser parser;

	parser.tokens = tokens;
	parser.iterator = parser.tokens.begin();
	parser.code = code;
	parser.stack_depth = 0;

	create_scope( parser );

	while ( !match( parser, token_eof ) ) {
		declaration( parser );
	}

	destroy_scope( parser );

	emit( parser, OpCode::op_return );
	printf( "emit op_return\n" );

	return true;
}

#define arithmetic_op( op ) { auto b = stack.pop(); auto a = stack.pop(); stack.push( a op b ); }

template< typename V = double, typename C = std::stack< V > >
class tb_stack {
public:
	V pop() {
		auto value = container.top();
		container.pop();

		return value;
	}

	void push( V value ) {
		container.push( value );
	}

	V slot( int number ) {
		return container._Get_container().at( number );
	}
private:
	C container;
};

double execute( const std::vector< uint32_t >& code ) {
	const uint32_t* base = code.data();
	tb_stack< double > stack;

	for ( const uint32_t* ip = base;; ++ip ) {
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

			stack.push( result );
			break;
		}
		case op_load_zero: stack.push( 0.0 ); break;
		case op_load_slot: stack.push( stack.slot( *++ip ) ); break;
		case op_pop: stack.pop(); break;
		case op_return: return stack.pop();
		default:
			throw std::exception( ( "Invalid instruction '" + std::to_string( *ip ) + "'" ).c_str() );
		}
	}
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

			std::vector< uint32_t > code;
			parse( tokens, &code );

			std::cout << "size of code: " << code.size() << std::endl;

			printf( "========== Execution (VM) ==========\n" );

			auto result = execute( code );
			printf( "Return: %f\n", result );
		} catch ( const std::exception& err ) {
			printf( "Error: %s\n", err.what() );
		}

		printf( "========== Done ==========\n" );
	}

	return 0;
}

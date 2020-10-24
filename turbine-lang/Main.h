#pragma once

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
	op_jz,
	op_jmp,
	op_gt,
	op_lt,
	op_eq,
};

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

struct encoded_value {
	union {
		uint8_t		uint8[ 8 ];
		uint16_t	uint16[ 4 ];
		uint32_t	uint32[ 2 ];
		int32_t		int32[ 2 ];
		double		dbl;
	} data;
};
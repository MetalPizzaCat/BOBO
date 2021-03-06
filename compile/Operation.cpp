#include "Operation.h"
#include "ErrorHandling/Logger.h"

uchar SwitchBasedOnRegistry(char regName, uchar base, uchar offset, bool returnARegValue)

{
	switch (regName)
	{
	case 'b':case 'B':
		return base;
		break;
	case 'c':case 'C':
		return base + (offset);
		break;
	case 'd':case 'D':
		return base + (offset * 2);
		break;
	case 'e':case 'E':
		return base + (offset * 3);
		break;
	case 'h':case 'H':
		return base + (offset * 4);
		break;
	case 'l':case 'L':
		return base + (offset * 5);
		break;
	case 'a':case 'A':
		return returnARegValue ? base + (offset * 7) : 0x00;
	default:
		return 0x00;
		break;
	}
}

std::vector<Operation*> ProcessOperation(std::vector<std::string>& operators, std::string& program, VariableManager*& manager, std::string resultRegistryName)

{
	std::vector<Operation*> res = {};
	//look for operators in the string
	size_t operatorStart = NPOS;
	//find any operator
	for (size_t i = 0; i < operators.size(); i++)
	{
		operatorStart = program.find_first_of(operators[i], 0);
		if (operatorStart != NPOS)
		{
			std::vector<std::string> args = {};
			//process left hand argument
			//note that for "=" left argument must be a variable
			std::string arg1 = program.substr(0, operatorStart);
			std::vector<Operation*> proc1 = ProcessOperation(operators, arg1, manager, "&a");
			if (proc1.empty())
			{
				args.push_back(arg1);
			}
			else
			{
				res.insert(res.end(), proc1.begin(), proc1.end());
				args.push_back("&a");
			}
			//if there is an operation then we return the operation

			//process right hand argument
			//seek till end of the line(maybe add support for divding via ";" in the future?) 
			std::string arg2 = program.substr(operatorStart + 1);
			//arg2 will have to go though the same operation as this 
			//so recursion
			std::vector<Operation*> proc2 = ProcessOperation(operators, arg2, manager, "&b");
			if (proc2.empty())
			{
				args.push_back(arg2);
			}
			else
			{
				res.insert(res.end(), proc2.begin(), proc2.end());
				args.push_back("&b");
			}
			args.push_back(resultRegistryName);
			res.push_back(new Operation(operators[i], args, manager));
			return res;
		}
	}

	//no operations found - the operation higher on the tree will just add the whole string as argument
	return {};
}


bool Operation::read_array_variable_addr(std::string arg,std::vector<uchar> &res, uchar operationByte)
{
	if (arg.find('[', 1) != NPOS)
	{
		std::string name = arg.substr(0, arg.find(']') - 2);
		if (varManager->Exists(name))
		{
			std::unique_ptr<Variable>& array = varManager->Get(name);
			//convert value hidden between [ and ] to number
			unsigned short addr = array->GetElementAddress(stoi(arg.substr(
				(
					arg.find('[') + 1,
					arg.find(']') - 1
					)
			)));
			//sta addr
			//lda newaddr
			res.push_back(operationByte);
			res.push_back(addr & 0x00ff);
			res.push_back((addr & 0xff00) >> 8);

			return true;
		}
		else
		{
			Logger::PrintError("Attempted to use undefined variable \"" + name + "\"");
		}
	}
	return false;
}

bool Operation::read_variable_addr(std::string arg, std::vector<uchar>& res, uchar operationByte)
{
	if (varManager->Exists(arg))
	{
		std::unique_ptr<Variable>& var = varManager->Get(arg);
		unsigned short addr = (var->promisedOffset + +0x800 + varManager->program_lenght);
		
		res.push_back(operationByte);

		//addr
		res.push_back(addr & 0x00ff);
		res.push_back((addr & 0xff00) >> 8);
		return true;
	}
	return false;
}

std::vector<uchar> Operation::Compile(size_t currentProgramLenght)

{
	std::vector<uchar> res;

	//wr is a special function that writes directly to the registry
	//why does it need to exist?
	//idk
	if (name == "wr")
	{
		if (arguments.size() != 2)
		{
			Logger::PrintError("Invalid number of arguments passed for function WR!");
		}


		if (std::unique_ptr<Variable> &var = varManager->Get(arguments[1]))
		{
			unsigned short addr = (var->promisedOffset + 0x800 + varManager->program_lenght);
			res.push_back(0x3a);

			res.push_back(addr & 0x00ff);
			res.push_back((addr & 0xff00) >> 8);
		}
		else
		{
			//b,c,d,e,h,l,m,a
			uchar base = 0;
			if (arguments[0] == "a")
			{
				//base = 0x78;
				base = 0x3e;
			}
			else if (arguments[0] == "b")
			{
				//base = 0x40;
				base = 0x06;
			}
			else if (arguments[0] == "c")
			{
				//base = 0x48;
				base = 0x0e;
			}
			else if (arguments[0] == "d")
			{
				//base = 0x50;
				base = 0x16;
			}
			else if (arguments[0] == "e")
			{
				//base = 0x58;
				base = 0x1e;
			}
			else if (arguments[0] == "h")
			{
				//base = 0x60;
				base = 0x26;
			}
			else if (arguments[0] == "l")
			{
				//base = 0x68;
				base = 0x2e;
			}
			else if (arguments[0] == "m")
			{
				//base = 0x70;
				base = 0x36;
			}
			else
			{
				Logger::PrintError("Function WR expects a,b,c,d,e,h,l as first argument,but got " + arguments[0] + " isntead");
			}
			res.push_back(base);

			res.push_back((uchar)atoi(arguments[1].c_str()));
		}


	}
	else if (name == "+")
	{
		/*
		* lda var1
		* mov a,b
		* lxi loc(arg2)
		* add m
		* mov a,_result_registry_name
		*/

		//if any argmuent is prefixed with & means it's refering to registry name
		//otherwise assume it is a variable or a number. 
		// ISSUE: both variabels and numbers lack & in them
		//TODO:Add note somewhere to not allow language users to use &a cause accumulator is used by calculations

		//are both arguments temp values?
		size_t arg1_name_start = arguments[0].find('&');
		size_t arg2_name_start = arguments[1].find('&');

		bool is_arg1_number = arguments[0].find_first_not_of("1234567890") == NPOS;
		bool is_arg2_number = arguments[1].find_first_not_of("1234567890") == NPOS;

		//both are registers(or numbers)
		if (arg1_name_start != NPOS && arg2_name_start != NPOS)
		{
			/*
			* mov a,_arg1_reg_name_
			* add _arg2_reg_name
			* mov _result_registry_name,a
			*/
			//move one of the values to Accumulator
			if (uchar result = SwitchBasedOnRegistry(arguments[0][arg1_name_start + 1], 0x4f, 0x08) != 0x00)
			{
				res.push_back(result);
			}
			//chose correct addition
			res.push_back(SwitchBasedOnRegistry(arguments[1][arg1_name_start + 1], 0x80, 0x01));
			//move result to desired registry for futher use
			res.push_back(SwitchBasedOnRegistry(arguments[2][arg1_name_start + 1], 0x78, 0x01));
		}
		//is the second argument variable?
		else if (arg1_name_start != NPOS && arg2_name_start == NPOS)
		{
			/*
			* mov a,_arg1_reg_name_
			* lxi arg2addr
			* add m
			* mov _result_registry_name,a
			*/
			//or if number
			/*
			* mov a,_arg1_reg_name_
			* adi number
			* mov _result_registry_name,a
			*/

			//move one of the values to Accumulator

			uchar result = 0x00;
			if ((result = SwitchBasedOnRegistry(arguments[0][arg1_name_start + 1], 0x4f, 0x08)) != 0x00)
			{
				res.push_back(result);
			}
			if (is_arg2_number)
			{
				res.push_back(0xc6);
				res.push_back((uchar)std::stoi(arguments[1]));
			}
			else if (read_variable_addr(arguments[1],res,0x21/*lxi h,*/))
			{
				//add m
				res.push_back(0x86);
			}
			else
			{
				Logger::PrintError("Attempted to use undefined variable: " + arguments[1]);
			}

			//move result to desired registry for futher use
			res.push_back(SwitchBasedOnRegistry(arguments[2][arg1_name_start + 1], 0x78, 0x01));
		}
		//is the first argument variable
		else if (arg1_name_start == NPOS && arg2_name_start != NPOS)
		{
			/*
			* mov a,_arg1_reg_name_
			* lxi arg1addr
			* add m
			* mov _result_registry_name,a
			*/
			//or if number
			/*
			* mov a,_arg1_reg_name_
			* adi number
			* mov _result_registry_name,a
			*/

			//move one of the values to Accumulator
			uchar result = 0x00;
			if ((result = SwitchBasedOnRegistry(arguments[1][arg2_name_start + 1], 0x47, 0x08)) != 0x00)
			{
				res.push_back(result);
			}

			if (is_arg1_number)
			{
				res.push_back(0xc6);
				res.push_back((uchar)std::stoi(arguments[0]));
			}
			else if (read_variable_addr(arguments[0], res, 0x21/*lxi h,*/))
			{
				//add m
				res.push_back(0x86);
			}
			else
			{
				
				Logger::PrintError(" Attempted to use undefined variable: " + arguments[0]);
			}
			//move result to desired registry for futher use
			res.push_back(SwitchBasedOnRegistry(arguments[2][arg1_name_start + 1], 0x78, 0x01));
		}
		//are both variables(or could be just numbers)
		else
		{
			/*
			lda varaddr
			mov b,a
			lda var2addr
			add b
			mov _result_registry_name,a
			*/
			//or
			/*
				mvi a, value1
				adi value2
				mov _result_registry_name,a
			*/

			//if both are numbers, just calculate before hand, who cares
			if (is_arg1_number && is_arg2_number)
			{
				//mvi a,
				res.push_back(0x3e);
				res.push_back((uchar)std::stoi(arguments[1]));
			}
			else
			{
				if (is_arg1_number)
				{
					//mvi a,
					res.push_back(0x3e);
					res.push_back((uchar)std::stoi(arguments[0]));
				}
				else if (read_variable_addr(arguments[1], res, 0x3a/*lda*/))
				{
					//idk there was restuctruing so this is empty now
				}
				//if it fails to read as array -> throw compilation error
				else if (!read_array_variable_addr(arguments[0],res))
				{
					Logger::PrintError(" Attempted to use undefined variable: " + arguments[0]);
				}

				if (is_arg2_number)
				{
					res.push_back(0xc6);
					res.push_back((uchar)std::stoi(arguments[1]));
				}
				else if(read_variable_addr(arguments[1], res, 0x21/*lxi h,*/))
				{
					//add m
					res.push_back(0x86);
				}
				//if it fails to read as array -> throw compilation error
				else if (read_array_variable_addr(arguments[1], res, 0x21))
				{
					//add m
					res.push_back(0x86);
				}
				else
				{
					Logger::PrintError(" Attempted to use undefined variable: " + arguments[1]);
				}

			}
		}
	}
	else if (name == "=")
	{
		//unlike math operations this one is very simple
		//sta and that's it
		//sta
		bool is_array = false;
		//if only numbers, then prepare them
		if (arguments[1].find_first_not_of("1234567890") == NPOS)
		{
			res.push_back(0x3e);
			res.push_back((uchar)std::stoi(arguments[1]));
		}
		//if result of something, prepare that too
		else if (arguments[1].find("&") != NPOS)
		{
			//move to a
			res.push_back(SwitchBasedOnRegistry(arguments[1][arguments[1].find("&") + 1], 0x78, 0x01, true));
		}
		//check if it's any array defenition
		//this is used later for actual array initialisation procedure
		else if ((arguments[1][0] == '[' && arguments[1].find(']') != NPOS) || (arguments[1][0] == '(' && arguments[1].find(')') != NPOS))
		{
			is_array = true;
		}
		//find "[" on any position expect for the first one
		//if search is successful that means we have access to array element by index

		
		if (!read_array_variable_addr(arguments[1], res, 0x3a) && !read_variable_addr(arguments[1], res, 0x3a))
		{
			Logger::PrintError("Expected variable name,array defenition or number, got \"" + arguments[1] + "\"");
		}

		
		if (varManager->Exists(arguments[0]))
		{
			std::unique_ptr<Variable>& var = varManager->Get(arguments[0]);
			if (var->IsArray != is_array)
			{
				Logger::PrintError(std::string("Type mismatch! Attempted to assign ") + (is_array ? "array" : "value") + " to " + (var->IsArray ? "array" : "value"));
				return res;
			}
			unsigned short addr = (var->promisedOffset +  + 0x800 + varManager->program_lenght);
			res.push_back(0x32);

			res.push_back(addr & 0x00ff);
			res.push_back((addr & 0xff00) >> 8);
		}
		//if it's variable defenition + initialization 
		else if (arguments[0].find("var") != NPOS)
		{
			//declare variable and then use it
			varManager->AddNew(arguments[0].substr(arguments[0].find("var") + 3), Variable::Type::Byte, is_array);
			unsigned short addr = (varManager->variables[varManager->variables.size() - 1]->promisedOffset + 0x800 + varManager->program_lenght);
			/*
			* for offset we would need to know program lenght before compilation
			*/
			int32_t array_size = 0;
			uchar* array = nullptr;
			/*to find array size we perform one of two operations based on conditions
			* 1) If using [x1,x2,...,xn] syntax -> count how many elements we have
			* 2) if useing (x,y) syntax -> do as it says
			*/
			if (is_array)
			{
				if (arguments[1][0] == '[' && arguments[1].find(']') != NPOS)
				{
					size_t off = 0;
					while ((off = arguments[1].find(',', off + 1)) != NPOS)
					{
						array_size++;
					}
					size_t temp = 0;

					res.push_back(0x21);
					res.push_back(addr & 0x00ff);
					res.push_back((addr & 0xff00) >> 8);
					off = 0;
					while ((temp = arguments[1].find(',', off+ 1)) != NPOS)
					{
						res.push_back(0x36);
						res.push_back(std::stoi(arguments[1].substr(off + 1u, temp)));
						res.push_back(0x23);
						off = temp;
					}
					res.push_back(0x36);
					res.push_back(std::stoi(arguments[1].substr(off + 1u, arguments[1].size() - off - 2)));

				}
				else if (arguments[1][0] == '(' && arguments[1].find(')') != NPOS)
				{
					array_size = stoi(arguments[1].substr(arguments[1].find('(') + 1, arguments[1].find(',') - 1));
					uchar elem = stoi(arguments[1].substr(arguments[1].find(',') + 1, arguments[1].find(')') - 1));

					/*
						__asm
						{
								lxi h, 0900
								mvi b, 10
								lxi d, 0000
							loop:
								mvi m, 01
								inx h
								inx d
								mov a, d
								cmp b
								jnz loop
							hlt
						}*/

					//lxi h,addr
					res.push_back(0x21);
					res.push_back(addr & 0x00ff);
					res.push_back((addr & 0xff00) >> 8);

					//mvi b,arr_len
					res.push_back(0x06);
					res.push_back((uchar)array_size);

					//lxi d,0000
					res.push_back(0x11);
					res.push_back(0);
					res.push_back(0);

					//mvi m,var
					res.push_back(0x36);
					res.push_back(elem);

					//inx h
					res.push_back(0x23);

					//inx d
					res.push_back(0x13);
					//mov a,e
					res.push_back(0x7b);
					//cmp b
					res.push_back(0xb8);
					//jnz current - 8
					res.push_back(0xc2);
					unsigned short cur_addr = currentProgramLenght + 8u + 0x800;
					res.push_back(cur_addr & 0x00ff);
					res.push_back((cur_addr & 0xff00) >> 8);
				}
				varManager->variables[varManager->variables.size() - 1]->ArraySize = array_size;
				if (is_array && (array_size <= 0 || array_size > 255))
				{
					Logger::PrintError("Attempted to declare array of size " + std::to_string(array_size));
					Logger::PrintInfo("To declare array use [x1,x2,...,xn] or (size,default_value) syntax");
					return res;
				}
			}
			else
			{
				res.push_back(0x32);
				res.push_back(addr & 0x00ff);
				res.push_back((addr & 0xff00) >> 8);
			}
		}
		else if(!read_array_variable_addr(arguments[0], res, 0x3a))
		{
			Logger::PrintError("Expected variable name or number, got \"" + arguments[0] + "\"");
		}
	}
	else if (name == "main_end")
	{
		res.push_back(0x76);
	}
	return res;
}

size_t Operation::GetLenght()
{

	size_t res = 0u;
	if (name == "wr")
	{
		if (varManager->Exists(arguments[1]))
		{
			res += 3u;
		}
		else
		{
			res += 2u;
		}
	}
	else if (name == "+")
	{
		size_t arg1_name_start = arguments[0].find('&');
		size_t arg2_name_start = arguments[1].find('&');

		bool is_arg1_number = arguments[0].find_first_not_of("1234567890") == NPOS;
		bool is_arg2_number = arguments[1].find_first_not_of("1234567890") == NPOS;

		//both are registers(or numbers)
		if (arg1_name_start != NPOS && arg2_name_start != NPOS)
		{
			res += 3u;
		}
		//is the second argument variable?
		else if (arg1_name_start != NPOS && arg2_name_start == NPOS)
		{
			res += 2u;
			if (is_arg2_number)
			{
				res += 2u;
			}
			else
			{
				res += 3u;
			}
		}
		//is the first argument variable
		else if (arg1_name_start == NPOS && arg2_name_start != NPOS)
		{
			res += 2u;
			if (is_arg1_number)
			{
				res += 2u;
			}
			else
			{
				res += 3u;
			}
		}
		//are both variables(or could be just numbers)
		else
		{
			//if both are numbers, just calculate before hand, who cares
			if (is_arg1_number && is_arg2_number)
			{
				res += 2u;
			}
			else
			{
				if (is_arg1_number)
				{
					res += 2u;
				}
				else
				{
					res += 3u;
				}

				if (is_arg2_number)
				{
					res += 2u;
				}
				else
				{
					res += 4u;
				}

			}
		}
	}
	else if (name == "=")
	{
		//unlike math operations this one is very simple
		//sta and that's it
		//sta

		//if only numbers, then prepare them
		if (arguments[1].find_first_not_of("1234567890") == NPOS)
		{
			res += 2u;
		}
		//if result of something, prepare that too
		else if (arguments[1].find("&") != NPOS)
		{
			res += 1u;
		}
		
		if (varManager->Exists(arguments[0]))
		{
			res += 3u;
		}
		//if it's variable defenition + initialization 
		else if (arguments[0].find("var") != NPOS)
		{
			int32_t array_size = 0;
			if (arguments[1][0] == '['&& arguments[1].find(']') != NPOS)
			{
				res += 3;
				size_t off = 0;
				while ((off = arguments[1].find(',', off + 1)) != NPOS)
				{
					res += 3;
				}
				res += 2;
			}
			else if (arguments[1][0] == '('  && arguments[1].find(')') != NPOS)
			{
				res += 16u;
			}
			else if (arguments[1].find('[') != NPOS)
			{
				res += 3u;
			}
			else if (varManager->Exists(arguments[1]))
			{
				res += 3u;
			}
			
			else
			{
				res += 3u;
			}
		}
	}
	else if (name == "main_end")
	{
		res += 1u;
	}
	return res;
}

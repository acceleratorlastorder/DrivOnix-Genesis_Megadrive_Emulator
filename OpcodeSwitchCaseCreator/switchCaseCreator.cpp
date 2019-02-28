using namespace std;

#include <stdint.h>
#include <string>

typedef uint8_t byte;
typedef uint16_t word;
typedef uint32_t dword;
typedef int8_t signed_byte;
typedef int16_t signed_word;
typedef int32_t signed_dword;

int BitGetVal(word data, int position)
{
	word mask = 1 << position;
	return (data & mask) ? 1 : 0;
}

bool IsOpcode(word opcode, string mask)
{
	for(int i = 0; i < 16; i++)
	{
		char bit = '0' + BitGetVal(opcode, 15-i);

		if(mask[i] == 'x')
		{
			continue;
		}

		if(mask[i] != bit)
		{
			return false;
		}
	}

	return true;
}

void SwitchCaseCreator(word opcode)
{
	if(IsOpcode(opcode, "00xxxxxxxxxxxxxx"))
	{//00

		if(IsOpcode(opcode, "0000001000111100"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeANDI_To_CCR();");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "0000001001111100"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeANDI_To_SR();");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "0000101000111100"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeEORI_To_CCR();");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "0000101001111100"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeEORI_To_SR();");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "0000000000111100"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeORI_To_CCR();");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "0000000001111100"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeORI_To_SR();");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "0000100001xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeBCHGStatic(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "0000100010xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeBCLRStatic(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "0000100011xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeBSETStatic(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "0000100000xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeBTSTStatic(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "00000010xxxxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeANDI(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "00000000xxxxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeORI(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "00001010xxxxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeEORI(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "00001100xxxxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeCMPI(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "00000110xxxxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeADDI(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "00000100xxxxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeSUBI(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "0000xxx1xx001xxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeMOVEP(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "0000xxx101xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeBCHGDynamic(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "0000xxx110xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeBCLRDynamic(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "0000xxx111xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeBSETDynamic(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "0000xxx100xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeBTSTDynamic(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "001xxxx001xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeMOVEA(opcode);");

			printf("\t\tbreak;");
		}
		else
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeMOVE(opcode);");

			printf("\t\tbreak;");
		}
		printf("\n");
		return;
	}


	else if(IsOpcode(opcode, "1101xxxxxxxxxxxx"))
	{//1101

		if(IsOpcode(opcode, "1101xxxx11xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeADDA(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "1101xxx1xx00xxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeADDX(opcode);");

			printf("\t\tbreak;");
		}
		else
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeADD(opcode);");

			printf("\t\tbreak;");
		}
		printf("\n");
		return;
	}


	else if(IsOpcode(opcode, "1100xxxxxxxxxxxx"))
	{//1100

		if(IsOpcode(opcode, "1100xxx10000xxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeABCD(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "1100xxx111xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeMULS(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "1100xxx011xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeMULU(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "1100xxx1xx00xxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeEXG(opcode);");

			printf("\t\tbreak;");
		}
		else
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeAND(opcode);");

			printf("\t\tbreak;");
		}
		printf("\n");
		return;
	}


	else if(IsOpcode(opcode, "0110xxxxxxxxxxxx"))
	{//0110

		if(IsOpcode(opcode, "01100001xxxxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeBSR(opcode);");

			printf("\t\tbreak;");
		}
		else
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeBcc(opcode);");

			printf("\t\tbreak;");
		}
		printf("\n");
		return;
	}


	else if(IsOpcode(opcode, "1011xxxxxxxxxxxx"))
	{//1011

		if(IsOpcode(opcode, "1011xxxx11xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeCMP_CMPA(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "1011xxx1xx001xxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeCMPM(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "1011xxx1xxxxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeEOR(opcode);");

			printf("\t\tbreak;");
		}
		else
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeCMP_CMPA(opcode);");

			printf("\t\tbreak;");
		}
		printf("\n");
		return;
	}


	else if(IsOpcode(opcode, "1000xxxxxxxxxxxx"))
	{//1000

		if(IsOpcode(opcode, "1000xxx10000xxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeSBCD(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "1000xxx111xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeDIVS(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "1000xxx011xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeDIVU(opcode);");

			printf("\t\tbreak;");
		}
		else
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeOR(opcode);");

			printf("\t\tbreak;");
		}
		printf("\n");
		return;
	}

	else if(IsOpcode(opcode, "1001xxxxxxxxxxxx"))
	{//1001

		if(IsOpcode(opcode, "1001xxxx11xxxxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeSUBA(opcode);");

			printf("\t\tbreak;");
		}
		else if(IsOpcode(opcode, "1001xxx1xx00xxxx"))
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeSUBX(opcode);");

			printf("\t\tbreak;");
		}
		else
		{
			printf("\t\tcase 0x%X:\n", opcode);

			printf("\t\tget().OpcodeSUB(opcode);");

			printf("\t\tbreak;");
		}
		printf("\n");
		return;
	}


	//other
	else if(IsOpcode(opcode, "0100111001110110"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeTRAPV();");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100111001110000"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeRESET();");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100101011111100"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeILLEGAL();");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100111001110011"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeRTE();");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100111001110111"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeRTR();");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100111001110001"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeNOP();");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100111001110101"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeRTS();");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100111001110010"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeSTOP();");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100111001011xxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeUNLK(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100100001000xxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeSWAP(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100111001010xxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeLINK(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100100001xxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodePEA(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100100000xxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeNBCD(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100111011xxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeJMP(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100111010xxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeJSR(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100100x1x000xxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeEXT(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "01001x001xxxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeMOVEM(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0111xxx0xxxxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeMOVEQ(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0101xxxx11001xxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeDBcc(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0101xxxx11xxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeScc(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0101xxx0xxxxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeADDQ(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0101xxx1xxxxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeSUBQ(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "1110000x11xxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeASL_ASR_Memory(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "1110001x11xxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeLSL_LSR_Memory(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "1110011x11xxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeROL_ROR_Memory(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "1110010x11xxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeROXL_ROXR_Memory(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "1110xxxxxxx00xxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeASL_ASR_Register(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "1110xxxxxxx01xxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeLSL_LSR_Register(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "1110xxxxxxx11xxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeROL_ROR_Register(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "1110xxxxxxx10xxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeROXL_ROXR_Register(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "010011100100xxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeTRAP(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "010011100110xxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeMOVE_USP(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100101011xxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeTAS(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100010011xxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeMOVE_To_CCR(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100011011xxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeMOVE_To_SR(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "01000110xxxxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeNOT(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "01000100xxxxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeNEG(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100000011xxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeMOVE_From_SR(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "01000000xxxxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeNEGX(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "01000010xxxxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeCLR(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "01001010xxxxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeTST(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100xxx110xxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeCHK(opcode);");

		printf("\t\tbreak;");
	}
	else if(IsOpcode(opcode, "0100xxx111xxxxxx"))
	{
		printf("\t\tcase 0x%X:\n", opcode);

		printf("\t\tget().OpcodeLEA(opcode);");

		printf("\t\tbreak;");
	}
	else
	{
		return;
	}
	printf("\n");
}

int main()
{
	freopen("SwitchCase.txt", "w", stdout);

	printf("\tswitch(opcode)\n");
	printf("\t{\n");

	for(int opcode = 0x0; opcode <= 0xFFFF; ++opcode)
	{
		SwitchCaseCreator((word)opcode);
	}

	printf("\t\tdefault:\n");
	printf("\t\tstd::cout << \"Unimplemented Opcode\" << std::hex << opcode << std::endl;");
	printf("\t\tbreak;\n");

	printf("\t}\n");
	return 0;
}
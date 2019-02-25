#include "unitTests.hpp"
////////////////////////////////////////
//UTILS
////////////////////////////////////////
bool TestFlag(word CCR, int C, int V, int Z, int N, int X)
{
	int res = 0;

	res += (C == BitGetVal(CCR, 0));
	res += (V == BitGetVal(CCR, 1));
	res += (Z == BitGetVal(CCR, 2));
	res += (N == BitGetVal(CCR, 3));
	res += (X == BitGetVal(CCR, 4));

	if(res == 5)
	{
		return true;
	}

	return false;
}

////////////////////////////////////////
//OPCODE TESTS
////////////////////////////////////////
bool Test_ABCD()
{
	std::cout << "Start Test_ABCD()" << std::endl;
	const std::string testName = "ABCD";
	bool testResult = true;

	CPU_STATE_DEBUG state;

	//Test BCD Operation with X Reset
	state.CCR = 0x0000;
	state.registerData[4] = 0x4;
	state.registerData[6] = 0x9;

	M68k::SetCpuState(state);

	word opcode = 0xCD04;
	std::cout << "\t\texecute " << testName << " with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

	M68k::ExecuteOpcode(opcode);

	state = M68k::GetCpuState();

	if(state.registerData[6] == 0x13)
	{
		std::cout << "\t\tTest BCD Operation with X Reset Passed" << std::endl;
	}
	else
	{
		std::cout << "\t\tTest BCD Operation with X Reset Failed" << std::endl;
		testResult = false;
	}

	//Test BCD Operation with X Set
	state.CCR = 0x0000;
	BitSet(state.CCR, X_FLAG);
	state.registerData[4] = 0x4;
	state.registerData[6] = 0x9;

	M68k::SetCpuState(state);

	std::cout << "\t\texecute " << testName << " with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

	M68k::ExecuteOpcode(opcode);

	state = M68k::GetCpuState();

	if(state.registerData[6] == 0x14)
	{
		std::cout << "\t\tTest BCD Operation with X Set Passed" << std::endl;
	}
	else
	{
		std::cout << "\t\tTest BCD Operation with X Set Failed" << std::endl;
		testResult = false;
	}

	//Test C_FLAG and X_FLAG
	state.CCR = 0x0000;
	state.registerData[4] = 0x99;
	state.registerData[6] = 0x1;

	M68k::SetCpuState(state);

	std::cout << "\t\texecute " << testName << " with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

	M68k::ExecuteOpcode(opcode);

	state = M68k::GetCpuState();

	//et on test
	if(TestFlag(state.CCR, 1, 0, 0, 0, 1))
	{
		std::cout << "\t\tTest C_FLAG and X_FLAG Passed" << std::endl;
	}
	else
	{
		std::cout << "\t\tTest C_FLAG and X_FLAG Failed" << std::endl;
		testResult = false;
	}

	//Test Z_FLAG
	state.CCR = 0x0000;
	BitSet(state.CCR, Z_FLAG);
	state.registerData[4] = 0x2;
	state.registerData[6] = 0x1;

	M68k::SetCpuState(state);

	std::cout << "\t\texecute " << testName << " with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

	M68k::ExecuteOpcode(opcode);

	state = M68k::GetCpuState();

	//et on test
	if(TestFlag(state.CCR, 0, 0, 0, 0, 0))
	{
		std::cout << "\t\tTest Z_FLAG Passed" << std::endl;
	}
	else
	{
		std::cout << "\t\tTest Z_FLAG Failed" << std::endl;
		testResult = false;
	}

	//indique la fin du test
	std::cout << "End Test_ABCD()" << std::endl;

	return testResult;
}


bool Test_ADD()
	{
		/*	EMPTY SIGNATURE
		 *  1101 (register)000 (opmode)000 ((effective address)(mode)000 (register)000)
		 */
		//Indique le debut du test
		std::cout << "Start Test_ADD()" << std::endl;
		const std::string testName = "ADD";
		CPU_STATE_DEBUG state;
		bool testResult = true;
		//Test NZVC_FLAG
		state.CCR = 0x0000;
		BitSet(state.CCR, N_FLAG);
		BitSet(state.CCR, Z_FLAG);
		BitSet(state.CCR, V_FLAG);
		BitSet(state.CCR, C_FLAG);

		dword value_1 = 0x3;
		dword value_2 = 0x6;

		state.registerData[7] = value_1;
		state.registerAddress[6] = 0xE00000;
		Genesis::M68KWriteMemoryBYTE(state.registerAddress[6], value_2);

		M68k::SetCpuState(state);

		/*	USED OPCODE
		 *  1101 (register)111(7) (opmode)100 ((effective address)(mode)010 (register)110(6))
		 *  binary: 1101111100010110
		 *  hex: DF16
		 */
		word opcode = 0xDF16;
		std::cout << "\t\texecute " << testName << " with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

		//execute the opcode
		M68k::ExecuteOpcode(opcode);
		state = M68k::GetCpuState();

		dword result = Genesis::M68KReadMemoryBYTE(state.registerAddress[6]);
		if((value_1 + value_2) && TestFlag(state.CCR, 0, 0, 0, 0, 0))
		{
			std::cout << "\t\tTest normal byte add Operation with V Z N C Passed" << std::endl;
		}
		else
		{
			testResult = false;
			std::cout << "\t\tTest normal byte add Operation Failed" << std::endl;
			if(!TestFlag(state.CCR, 0, 0, 0, 0, 0)){
				std::cout << "\t\tclear flag V Z N C failed !" << std::endl;
			}
			if (result != (value_1 + value_2)) {
				std::cout << "\t\toperation result is false result: " << result << " should be: " << (value_1 + value_2) << std::endl;
			}
		}

		/*************
		 *TEST C FLAG*
		 ************/
		state.CCR = 0x0000;

		value_1 = 0x78;
		value_2 = 0xA;

		state.registerData[7] = value_1;
		state.registerAddress[6] = 0xE00000;
		Genesis::M68KWriteMemoryBYTE(state.registerAddress[6], value_2);

		M68k::SetCpuState(state);

		/*	USED OPCODE
		 *  1101 (register)111(7) (opmode)100 ((effective address)(mode)010 (register)110(6))
		 *  binary: 1101111100010110
		 *  hex: DF16
		 */
		opcode = 0xDF16;
		std::cout << "\t\texecute " << testName << " with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

		//execute the opcode
		M68k::ExecuteOpcode(opcode);
		state = M68k::GetCpuState();

		result = Genesis::M68KReadMemoryBYTE(state.registerAddress[6]);
		if((value_1 + value_2) && TestFlag(state.CCR, 0, 1, 0, 0, 0))
		{
			std::cout << "\t\tTest normal byte add Operation with V flag Passed" << std::endl;
		}
		else
		{
			testResult = false;
			std::cout << "\t\tTest normal byte add Operation Failed" << std::endl;
			if(!TestFlag(state.CCR, 0, 1, 0, 0, 0)){
				std::cout << "\t\traise V flag failed !" << std::endl;
			}
			if (result != (value_1 + value_2)) {
				std::cout << "\t\toperation result is false result: " << result << " should be: " << (value_1 + value_2) << std::endl;
			}
		}


		/*
		BitSet(state.CCR, X_FLAG);

		BitSet(state.CCR, N_FLAG);

		BitSet(state.CCR, Z_FLAG);

		BitSet(state.CCR, V_FLAG);

		BitSet(state.CCR, C_FLAG);
		*/

		//indique la fin du test
		std::cout << "End Test_ADD()" << std::endl;
		return testResult;
	}



	bool Test_NBCD()
	{
		/*	EMPTY SIGNATURE
		 *  0100100000 ((effective address)(mode)000 (register)000)
		 *  0x4800
		 */
		std::cout << "Start Test_NBCD()" << std::endl;
		const std::string testName = "NBCD";
		bool testResult = true;

		CPU_STATE_DEBUG state;

		dword value_1 = 0x3;
		dword value_2 = 0x9;


		dword value_1 = 0x3;
		dword value_2 = 0x6;

		state.registerData[7] = value_1;
		state.registerAddress[6] = 0xE00000;
		Genesis::M68KWriteMemoryBYTE(state.registerAddress[6], value_2);

		M68k::SetCpuState(state);
		/*	USED OPCODE
		 *  0100100000 ((effective address)(mode)010 (register)110(6))
		 *  binary: 0100100000010110
		 *  hex: 0x4806
		 */
		word opcode = 0x4816;
		std::cout << "\t\texecute " << testName << " with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

		//execute the opcode
		M68k::ExecuteOpcode(opcode);
		state = M68k::GetCpuState();

		dword result = Genesis::M68KReadMemoryBYTE(state.registerAddress[6]);

		if(state.registerData[6] == (value_2 - value_1))
		{
			std::cout << "\t\tTest BCD Operation with X Reset Passed" << std::endl;
		}
		else
		{
			std::cout << "\t\tTest BCD Operation with X Reset Failed" << std::endl;
			testResult = false;
		}

		//Test BCD Operation with X Set
		state.CCR = 0x0000;
		BitSet(state.CCR, X_FLAG);
		state.registerData[4] = 0x4;
		state.registerData[6] = 0x9;

		M68k::SetCpuState(state);

		std::cout << "\t\texecute " << testName << " with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

		M68k::ExecuteOpcode(opcode);

		state = M68k::GetCpuState();

		if(state.registerData[6] == 0x14)
		{
			std::cout << "\t\tTest BCD Operation with X Set Passed" << std::endl;
		}
		else
		{
			std::cout << "\t\tTest BCD Operation with X Set Failed" << std::endl;
			testResult = false;
		}

		//Test C_FLAG and X_FLAG
		state.CCR = 0x0000;
		state.registerData[4] = 0x99;
		state.registerData[6] = 0x1;

		M68k::SetCpuState(state);

		std::cout << "\t\texecute " << testName << " with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

		M68k::ExecuteOpcode(opcode);

		state = M68k::GetCpuState();

		//et on test
		if(TestFlag(state.CCR, 1, 0, 0, 0, 1))
		{
			std::cout << "\t\tTest C_FLAG and X_FLAG Passed" << std::endl;
		}
		else
		{
			std::cout << "\t\tTest C_FLAG and X_FLAG Failed" << std::endl;
			testResult = false;
		}

		//Test Z_FLAG
		state.CCR = 0x0000;
		BitSet(state.CCR, Z_FLAG);
		state.registerData[4] = 0x2;
		state.registerData[6] = 0x1;

		M68k::SetCpuState(state);

		std::cout << "\t\texecute " << testName << " with opcode 0x" << std::uppercase << std::hex << opcode << std::endl;

		M68k::ExecuteOpcode(opcode);

		state = M68k::GetCpuState();

		//et on test
		if(TestFlag(state.CCR, 0, 0, 0, 0, 0))
		{
			std::cout << "\t\tTest Z_FLAG Passed" << std::endl;
		}
		else
		{
			std::cout << "\t\tTest Z_FLAG Failed" << std::endl;
			testResult = false;
		}

		//indique la fin du test
		std::cout << "End Test_ABCD()" << std::endl;

		return testResult;
	}

////////////////////////////////////////
//MAIN
////////////////////////////////////////
int main()
{
	std::map<std::string, bool> TestResults;


	M68k::SetUnitTestsMode();

	Genesis::AllocM68kMemory();
	Genesis::M68KWriteMemoryLONG(0xE00000, 0xCAFE);


	TestResults.insert(std::pair<std::string, bool>("Test_ABCD", Test_ABCD()));
	TestResults.insert(std::pair<std::string, bool>("Test_ADD", Test_ADD()));
	TestResults.insert(std::pair<std::string, bool>("Test_NBCD", Test_NBCD()));


	/**
	 * END
	 **/
	std::cout << "!!!!!!All Test Completed!!!!!!" << std::endl;
	int testErrorCount = 0;
	for(auto iterator = TestResults.begin(); iterator != TestResults.end(); iterator++) {
		if(!iterator->second){
			testErrorCount++;
			std::cout << iterator->first << " did not pass the test" << std::endl;
		}
	}

	if(testErrorCount == 0){
		std::cout << "There is no known error GOOD JOB !" << std::endl;
	}else{
		std::cout << "There is " << testErrorCount << " known errors !" << std::endl;
	}

	system("pause");
}

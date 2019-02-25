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
	bool testResult = false;

	CPU_STATE_DEBUG state;

	//Test BCD Operation with X Reset
	state.CCR = 0x0000;
	state.registerData[4] = 0x4;
	state.registerData[6] = 0x9;

	M68k::SetCpuState(state);

	M68k::ExecuteOpcode(0xCD04);

	state = M68k::GetCpuState();

	if(state.registerData[6] == 0x13)
	{
		std::cout << "\t\tTest BCD Operation with X Reset Passed" << std::endl;
		testResult = true;
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

	M68k::ExecuteOpcode(0xCD04);

	state = M68k::GetCpuState();

	if(state.registerData[6] == 0x14)
	{
		std::cout << "\t\tTest BCD Operation with X Set Passed" << std::endl;
		testResult = true;
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

	M68k::ExecuteOpcode(0xCD04);

	state = M68k::GetCpuState();

	//et on test
	if(TestFlag(state.CCR, 1, 0, 0, 0, 1))
	{
		std::cout << "\t\tTest C_FLAG and X_FLAG Passed" << std::endl;
		testResult = true;
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

	M68k::ExecuteOpcode(0xCD04);

	state = M68k::GetCpuState();

	//et on test
	if(TestFlag(state.CCR, 0, 0, 0, 0, 0))
	{
		std::cout << "\t\tTest Z_FLAG Passed" << std::endl;
		testResult = true;
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
		bool testResult = true;

		//on declare un state
		//et on met les values qu'il doit avoir avant l'execution de l'opcode


		CPU_STATE_DEBUG state;

		//Test Z_FLAG
		state.CCR = 0x0000;
		BitSet(state.CCR, N_FLAG);
		BitSet(state.CCR, Z_FLAG);
		BitSet(state.CCR, V_FLAG);
		state.registerData[6] = 0x9;
		state.registerData[7] = 0x19;

		M68k::SetCpuState(state);


		/*	USED OPCODE
		 *  1101 (register)111(7) (opmode)100 ((effective address)(mode)010 (register)110(6))
		 *  binary: 1101111100010110
		 *  hex: DF16
		 */

		//on execute l'opcode désiré
		M68k::ExecuteOpcode(0xDF16);

		//on recupère notre state modifié par l'opcode
		state = M68k::GetCpuState();

		std::cout << "\t\tstate.registerData[6]" << std::hex <<state.registerData[6] << std::endl;
		std::cout << "\t\tstate.registerData[7]" << std::hex <<state.registerData[7] << std::endl;

		std::cout << "\t\tresult bit set N Z V " << TestFlag(state.CCR, 0, 1, 1, 1, 0) << std::endl;


		if(state.registerData[7] == (0x9 + 0x8))
		{
			std::cout << "\t\tTest normal byte add Operation Passed" << std::endl;
		}
		else
		{
			std::cout << "\t\tTest normal byte add Operation Failed" << std::endl;
			testResult = false;
		}


		/*



		BitSet(state.CCR, X_FLAG);

		BitSet(state.CCR, N_FLAG);

		BitSet(state.CCR, Z_FLAG);

		BitSet(state.CCR, V_FLAG);

		BitSet(state.CCR, C_FLAG);
		*/

		return testResult;
	}


////////////////////////////////////////
//MAIN
////////////////////////////////////////
int main()
{
	std::map<std::string, bool> TestResults;


	M68k::SetUnitTestsMode();

	//Genesis::M68KWriteMemoryLONG(0xE00000, 0xCAFE);


	TestResults.insert(std::pair<std::string, bool>("Test_ABCD", Test_ABCD()));
	TestResults.insert(std::pair<std::string, bool>("Test_ADD", Test_ADD()));





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

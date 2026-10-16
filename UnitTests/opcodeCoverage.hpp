#ifndef OPCODE_COVERAGE_HPP
#define OPCODE_COVERAGE_HPP

bool Test_ANDI_TO_CCR();
bool Test_ANDI_TO_SR();
bool Test_DIVU();
bool Test_EORI_TO_CCR();
bool Test_EORI_TO_SR();
bool Test_MOVE_TO_CCR();
bool Test_MOVE_FROM_SR();
bool Test_MOVE_TO_SR();
bool Test_MOVE_USP();
bool Test_MULU();

int RunOpcodeCoverage(bool optimized);

#endif

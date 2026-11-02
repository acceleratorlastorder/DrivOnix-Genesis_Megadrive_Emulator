#include <chrono>
#include <iostream>
#include <thread>
// Deliberately unsanitized external-process fixture: the production child
// memory limit must not inherit an ASan executable's virtual shadow mapping.
int main(){std::this_thread::sleep_for(std::chrono::seconds(8));std::cout<<"Path = delayed.bin\nSize = 512\nCRC = 00000000\nEncrypted = -\n\n";}

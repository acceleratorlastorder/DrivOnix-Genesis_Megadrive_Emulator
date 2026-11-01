#include "process.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
constexpr std::size_t ErrorLimit = 65536;
using Clock = std::chrono::steady_clock;
#ifdef _WIN32
std::wstring Wide(const std::string& value)
{
    if(value.find('\0')!=std::string::npos) throw std::runtime_error("NUL in process argument");
    if(value.empty()) return {};
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);
    if(!count) throw std::runtime_error("Invalid UTF-8 process argument");
    std::wstring result(count,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),&result[0],count);
    return result;
}
std::string Utf8(const std::wstring& value)
{
    if(value.empty()) return {};
    const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
    if(!count) throw std::runtime_error("Invalid Unicode executable path");
    std::string result(count,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),&result[0],count,nullptr,nullptr);
    return result;
}
std::wstring Environment(const wchar_t* name)
{
    const DWORD size=GetEnvironmentVariableW(name,nullptr,0);
    if(!size) return {};
    std::wstring result(size,L'\0');
    const DWORD count=GetEnvironmentVariableW(name,&result[0],size);
    if(!count||count>=size) return {};
    result.resize(count);return result;
}
struct Handle
{
    HANDLE value=nullptr;
    Handle()=default;
    explicit Handle(HANDLE handle):value(handle){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE) CloseHandle(value);}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
std::wstring Quote(const std::wstring& value)
{
    std::wstring result=L"\"";unsigned slashes=0;
    for(wchar_t character:value)
    {
        if(character==L'\\') {++slashes;continue;}
        result.append(character==L'"' ? slashes*2+1:slashes,L'\\');slashes=0;
        result.push_back(character);
    }
    result.append(slashes*2,L'\\');result.push_back(L'"');return result;
}
void Pipe(Handle& read,Handle& write)
{
    SECURITY_ATTRIBUTES security={sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    if(!CreatePipe(&read.value,&write.value,&security,0)||!SetHandleInformation(read.value,HANDLE_FLAG_INHERIT,0))
        throw std::runtime_error("Cannot create archive process pipe");
}
bool Drain(HANDLE pipe,std::vector<unsigned char>& output,std::string& error,bool standardOutput,std::size_t limit)
{
    DWORD available=0;
    if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr))
    {
        if(GetLastError()==ERROR_BROKEN_PIPE) return false;
        throw std::runtime_error("Cannot read archive process pipe");
    }
    if(!available) return true;
    unsigned char bytes[8192];DWORD count=0;
    if(!ReadFile(pipe,bytes,std::min<DWORD>(available,sizeof(bytes)),&count,nullptr))
        throw std::runtime_error("Cannot read archive process output");
    if(standardOutput)
    {
        if(count>limit-output.size()) throw std::runtime_error("Archive process output exceeds the allowed size");
        output.insert(output.end(),bytes,bytes+count);
    }
    else if(error.size()<ErrorLimit)
        error.append(reinterpret_cast<char*>(bytes),std::min<std::size_t>(count,ErrorLimit-error.size()));
    return true;
}
#else
struct Pipe
{
    int read=-1,write=-1;
    Pipe()
    {
        int descriptors[2];
        if(pipe(descriptors)!=0) throw std::runtime_error("Cannot create archive process pipe");
        read=descriptors[0];write=descriptors[1];
        if(fcntl(read,F_SETFL,O_NONBLOCK)<0)
        {close(read);close(write);read=write=-1;throw std::runtime_error("Cannot configure archive process pipe");}
    }
    ~Pipe(){if(read>=0)close(read);if(write>=0)close(write);}
    void CloseWrite(){if(write>=0){close(write);write=-1;}}
};
void Stop(pid_t child)
{
    kill(-child,SIGKILL);kill(child,SIGKILL);
    while(waitpid(child,nullptr,0)<0&&errno==EINTR){}
}
bool Drain(int pipe,std::vector<unsigned char>& output,std::string& error,bool standardOutput,std::size_t limit)
{
    unsigned char bytes[8192];
    const auto count=read(pipe,bytes,sizeof(bytes));
    if(count==0) return false;
    if(count<0)
    {
        if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR) return true;
        throw std::runtime_error("Cannot read archive process output");
    }
    if(standardOutput)
    {
        if(std::size_t(count)>limit-output.size()) throw std::runtime_error("Archive process output exceeds the allowed size");
        output.insert(output.end(),bytes,bytes+count);
    }
    else if(error.size()<ErrorLimit)
        error.append(reinterpret_cast<char*>(bytes),std::min<std::size_t>(std::size_t(count),ErrorLimit-error.size()));
    return true;
}
#endif
}

std::string Process::FindExecutable(const std::string& name)
{
    if(name.empty()||name.find_first_of("/\\\0",0,3)!=std::string::npos) return {};
#ifdef _WIN32
    const auto path=Environment(L"PATH");
    const auto filename=Wide(name);
    std::size_t start=0;
    while(start<=path.size())
    {
        const auto end=path.find(L';',start);
        auto directory=path.substr(start,end==std::wstring::npos ? end:end-start);
        if(directory.size()>=2&&directory.front()==L'"'&&directory.back()==L'"') directory=directory.substr(1,directory.size()-2);
        if(!directory.empty())
        {
            const auto candidate=directory+L"\\"+filename;
            const DWORD attributes=GetFileAttributesW(candidate.c_str());
            if(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY)) return Utf8(candidate);
        }
        if(end==std::wstring::npos) break;
        start=end+1;
    }
#else
    const char* environment=std::getenv("PATH");const std::string path=environment ? environment:"";
    std::size_t start=0;
    while(start<=path.size())
    {
        const auto end=path.find(':',start);
        const auto directory=path.substr(start,end==std::string::npos ? end:end-start);
        if(!directory.empty())
        {
            const auto candidate=directory+"/"+name;struct stat status={};
            if(stat(candidate.c_str(),&status)==0&&S_ISREG(status.st_mode)&&access(candidate.c_str(),X_OK)==0) return candidate;
        }
        if(end==std::string::npos) break;
        start=end+1;
    }
#endif
    return {};
}

Process::Result Process::Run(const std::vector<std::string>& arguments,std::size_t outputLimit,unsigned timeoutMilliseconds)
{
    if(arguments.empty()||arguments.front().empty()||!timeoutMilliseconds)
        throw std::invalid_argument("Invalid archive process invocation");
    for(const auto& argument:arguments)
        if(argument.find('\0')!=std::string::npos) throw std::invalid_argument("NUL in archive process argument");
    const auto deadline=Clock::now()+std::chrono::milliseconds(timeoutMilliseconds);
    Result result;
#ifdef _WIN32
    Handle outputRead,outputWrite,errorRead,errorWrite;
    Pipe(outputRead,outputWrite);Pipe(errorRead,errorWrite);
    SECURITY_ATTRIBUTES security={sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    Handle input(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(input.value==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot close archive process input");
    Handle job(CreateJobObjectW(nullptr,nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit=512u*1024u*1024u;
    if(!job.value||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
        throw std::runtime_error("Cannot constrain archive process memory");
    std::wstring command;
    for(const auto& argument:arguments) {if(!command.empty())command.push_back(L' ');command+=Quote(Wide(argument));}
    std::vector<wchar_t> commandLine(command.begin(),command.end());commandLine.push_back(L'\0');
    STARTUPINFOW startup={};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=input.value;startup.hStdOutput=outputWrite.value;startup.hStdError=errorWrite.value;
    PROCESS_INFORMATION process={};
    if(!CreateProcessW(Wide(arguments.front()).c_str(),commandLine.data(),nullptr,nullptr,TRUE,
                       CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup,&process))
        throw std::runtime_error("Cannot start installed 7-Zip executable");
    Handle processHandle(process.hProcess),threadHandle(process.hThread);
    if(!AssignProcessToJobObject(job.value,processHandle.value))
    {TerminateProcess(processHandle.value,1);throw std::runtime_error("Cannot constrain archive process");}
    if(ResumeThread(threadHandle.value)==DWORD(-1)) throw std::runtime_error("Cannot start archive process thread");
    CloseHandle(outputWrite.value);outputWrite.value=nullptr;CloseHandle(errorWrite.value);errorWrite.value=nullptr;
    bool outputOpen=true,errorOpen=true;
    while(outputOpen||errorOpen||WaitForSingleObject(processHandle.value,0)!=WAIT_OBJECT_0)
    {
        if(Clock::now()>=deadline) throw std::runtime_error("Archive process timed out");
        if(outputOpen)outputOpen=Drain(outputRead.value,result.output,result.error,true,outputLimit);
        if(errorOpen)errorOpen=Drain(errorRead.value,result.output,result.error,false,outputLimit);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    DWORD code=0;if(!GetExitCodeProcess(processHandle.value,&code)) throw std::runtime_error("Cannot read archive process exit status");
    result.exitCode=int(code);
#else
    Pipe output,error;
    std::vector<char*> argv;for(const auto& argument:arguments)argv.push_back(const_cast<char*>(argument.c_str()));argv.push_back(nullptr);
    const pid_t child=fork();
    if(child<0) throw std::runtime_error("Cannot start archive process");
    if(child==0)
    {
        setpgid(0,0);
        const int input=open("/dev/null",O_RDONLY);
        if(input<0||dup2(input,STDIN_FILENO)<0||dup2(output.write,STDOUT_FILENO)<0||dup2(error.write,STDERR_FILENO)<0) _exit(126);
        close(input);close(output.read);close(output.write);close(error.read);close(error.write);
        struct rlimit memory={512u*1024u*1024u,512u*1024u*1024u};
        if(setrlimit(RLIMIT_AS,&memory)!=0) _exit(126);
        execv(argv[0],argv.data());_exit(127);
    }
    output.CloseWrite();error.CloseWrite();setpgid(child,child);
    bool outputOpen=true,errorOpen=true,finished=false;int status=0;
    try
    {
        while(outputOpen||errorOpen||!finished)
        {
            if(Clock::now()>=deadline) throw std::runtime_error("Archive process timed out");
            struct pollfd descriptors[2]={{outputOpen ? output.read:-1,POLLIN,0},{errorOpen ? error.read:-1,POLLIN,0}};
            if(poll(descriptors,2,10)<0&&errno!=EINTR) throw std::runtime_error("Cannot wait for archive process output");
            if(outputOpen&&(descriptors[0].revents&(POLLIN|POLLHUP|POLLERR)))outputOpen=Drain(output.read,result.output,result.error,true,outputLimit);
            if(errorOpen&&(descriptors[1].revents&(POLLIN|POLLHUP|POLLERR)))errorOpen=Drain(error.read,result.output,result.error,false,outputLimit);
            if(!finished)
            {
                const auto waited=waitpid(child,&status,WNOHANG);
                if(waited==child)finished=true;
                else if(waited<0&&errno!=EINTR) throw std::runtime_error("Cannot read archive process exit status");
            }
        }
    }
    catch(...){Stop(child);throw;}
    result.exitCode=WIFEXITED(status) ? WEXITSTATUS(status):128+(WIFSIGNALED(status) ? WTERMSIG(status):0);
#endif
    return result;
}

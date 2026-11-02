#include "http.hpp"
#include "catalog.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <array>
#include <condition_variable>
#include <mutex>
#else
#include <curl/curl.h>
#endif

namespace {
bool Cancelled(const Metadata::CancelCheck& cancel) { return cancel&&cancel(); }
void Check(const Metadata::CancelCheck& cancel) {
    if(Cancelled(cancel))throw std::runtime_error("Metadata request cancelled");
}
std::string Lower(std::string text) {
    for(auto& c:text)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    return text;
}
std::string Trim(std::string text) {
    while(!text.empty()&&std::isspace(static_cast<unsigned char>(text.back())))text.pop_back();
    const auto start=text.find_first_not_of(" \t\r\n");return start==std::string::npos?std::string():text.substr(start);
}
#ifdef _WIN32
std::wstring Wide(const std::string& text) {
    const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
    if(!size)throw std::runtime_error("Invalid metadata URL encoding");
    std::wstring result(std::size_t(size),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),&result[0],size);return result;
}
std::string Utf8(const std::wstring& text) {
    if(text.empty())return {};
    const auto size=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    if(!size)throw std::runtime_error("Invalid metadata response header");
    std::string result(std::size_t(size),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),&result[0],size,nullptr,nullptr);return result;
}
struct Handle {
    HINTERNET value=nullptr;
    explicit Handle(HINTERNET h):value(h){if(!h)throw std::runtime_error("Cannot create metadata HTTP request");}
    ~Handle(){if(value)WinHttpCloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
// WinHTTP requires asynchronous request handles for safe cancellation. The
// worker waits for completion notifications, while the UI remains independent.
// Closing waits for HANDLE_CLOSING before releasing callback/buffer storage.
struct Interruptible {
    HINTERNET request=nullptr;
    std::mutex mutex;
    std::condition_variable changed;
    DWORD completed=0,error=0,available=0,count=0;
    bool closing=false,contextReady=false;
    std::array<unsigned char,16384> buffer{};
    static void CALLBACK Notice(HINTERNET,DWORD_PTR context,DWORD status,LPVOID info,DWORD length) {
        if(!context)return;
        auto& state=*reinterpret_cast<Interruptible*>(context);
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)state.closing=true;
            else if(status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
                state.error=length>=sizeof(WINHTTP_ASYNC_RESULT)?static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError:ERROR_INVALID_DATA;
            } else if(status==WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE) {
                if(length>=sizeof(DWORD)){state.available=*static_cast<DWORD*>(info);state.completed=status;}
                else state.error=ERROR_INVALID_DATA;
            } else if(status==WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
                state.count=length;state.completed=status;
            } else if(status==WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE||status==WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE)
                state.completed=status;
            // Notify before releasing the mutex: HANDLE_CLOSING lets the owner
            // destroy this state as soon as it acquires the mutex afterwards.
            state.changed.notify_all();
        }
    }
    explicit Interruptible(HINTERNET handle):request(handle) {
        if(!request)throw std::runtime_error("Cannot open metadata HTTP request");
        const auto flags=WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES;
        if(WinHttpSetStatusCallback(request,Notice,flags,0)==WINHTTP_INVALID_STATUS_CALLBACK) {
            WinHttpCloseHandle(request);request=nullptr;throw std::runtime_error("Cannot configure metadata HTTP completion");
        }
        DWORD_PTR context=reinterpret_cast<DWORD_PTR>(this);
        if(!WinHttpSetOption(request,WINHTTP_OPTION_CONTEXT_VALUE,&context,sizeof(context))) {
            WinHttpCloseHandle(request);request=nullptr;throw std::runtime_error("Cannot configure metadata HTTP context");
        }
        contextReady=true;
    }
    ~Interruptible() {
        if(!request)return;
        const auto handle=request;request=nullptr;
        if(WinHttpCloseHandle(handle)&&contextReady) {
            std::unique_lock<std::mutex> lock(mutex);changed.wait(lock,[this]{return closing;});
        }
    }
    void Begin(){std::lock_guard<std::mutex> lock(mutex);completed=error=available=count=0;}
    void Issued(BOOL result) {
        if(!result&&GetLastError()!=ERROR_IO_PENDING)
            throw std::runtime_error("Cannot start metadata HTTPS operation");
    }
    void Wait(DWORD expected,const Metadata::CancelCheck& cancel,std::chrono::steady_clock::time_point deadline) {
        std::unique_lock<std::mutex> lock(mutex);
        while(completed!=expected&&!error) {
            lock.unlock();Check(cancel);
            if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Metadata HTTP request timed out");
            lock.lock();changed.wait_for(lock,std::chrono::milliseconds(20));
        }
        if(error)throw std::runtime_error("Metadata HTTPS request failed (connection or certificate error; code "+std::to_string(error)+")");
        lock.unlock();Check(cancel);
    }
};
std::string Header(HINTERNET request,DWORD field) {
    DWORD length=0;WinHttpQueryHeaders(request,field,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&length,WINHTTP_NO_HEADER_INDEX);
    if(GetLastError()==ERROR_WINHTTP_HEADER_NOT_FOUND)return {};
    if(length>32768)throw std::runtime_error("Metadata HTTP header is too large");
    if(!length)return {};
    std::vector<wchar_t> buffer(length/sizeof(wchar_t)+1,L'\0');
    if(!WinHttpQueryHeaders(request,field,WINHTTP_HEADER_NAME_BY_INDEX,buffer.data(),&length,WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("Cannot read metadata HTTP header");
    return Utf8(buffer.data());
}
#else
struct CurlState {
    Metadata::HttpResponse response;
    const Metadata::HttpRequest& request;
    const Metadata::CancelCheck& cancel;
    bool overflow=false,cancelled=false,failed=false;
};
size_t Body(char* bytes,size_t unit,size_t count,void* user) {
    auto& state=*static_cast<CurlState*>(user);
    if(count&&unit>state.request.limit/count){state.overflow=true;return 0;}
    const auto length=unit*count;
    if(length>state.request.limit-state.response.bytes.size()){state.overflow=true;return 0;}
    try {
        state.response.bytes.insert(state.response.bytes.end(),bytes,bytes+length);return length;
    }catch(...){state.failed=true;return 0;}
}
size_t Headers(char* bytes,size_t unit,size_t count,void* user) {
    auto& state=*static_cast<CurlState*>(user);
    if(count&&unit>16384/count)return 0;
    const auto length=unit*count;if(length>16384)return 0;
    try {
        const std::string line(bytes,length);const auto colon=line.find(':');
        if(colon!=std::string::npos) {
            const auto name=Lower(line.substr(0,colon));
            if(name=="etag")state.response.etag=Trim(line.substr(colon+1));
            else if(name=="content-type")state.response.contentType=Trim(line.substr(colon+1));
        }
        return length;
    }catch(...){state.failed=true;return 0;}
}
int Progress(void* user,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {
    auto& state=*static_cast<CurlState*>(user);
    try {state.cancelled=Cancelled(state.cancel);}catch(...){state.cancelled=true;}
    return state.cancelled?1:0;
}
#endif
}

bool Metadata::AllowedUrl(const std::string& url) {
    if(url.find_first_of("\r\n\0",0,3)!=std::string::npos||url.find('?')!=std::string::npos||url.find('#')!=std::string::npos)return false;
    for(const auto* category:{"no-intro","developer","publisher","genre","releaseyear","maxusers"})
        if(url==CatalogUrl(category))return true;
    const std::string prefix="https://raw.githubusercontent.com/libretro-thumbnails/Sega_-_Mega_Drive_-_Genesis/master/Named_Boxarts/";
    if(url.compare(0,prefix.size(),prefix)!=0)return false;
    const auto filename=url.substr(prefix.size());const auto lower=Lower(filename);
    return filename.size()>4&&filename.size()<16384&&filename.compare(filename.size()-4,4,".png")==0&&
        filename.find_first_of("/\\")==std::string::npos&&lower.find("%2f")==std::string::npos&&lower.find("%5c")==std::string::npos;
}

Metadata::HttpResponse Metadata::Download(const HttpRequest& request,const CancelCheck& cancel) {
    if(!AllowedUrl(request.url))throw std::invalid_argument("Metadata URL is not an approved catalog/artwork endpoint");
    if(!request.limit||request.limit>32*1024*1024||!request.timeoutSeconds||request.timeoutSeconds>120)
        throw std::invalid_argument("Invalid metadata HTTP limits");
    if(request.etag.size()>1024||request.etag.find_first_of("\r\n\0",0,3)!=std::string::npos)
        throw std::invalid_argument("Invalid metadata HTTP validator");
    Check(cancel);
#ifdef _WIN32
    const auto url=Wide(request.url);URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);
    parts.dwHostNameLength=DWORD(-1);parts.dwUrlPathLength=DWORD(-1);parts.dwExtraInfoLength=DWORD(-1);
    if(!WinHttpCrackUrl(url.c_str(),DWORD(url.size()),0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS)
        throw std::runtime_error("Cannot parse metadata HTTPS URL");
    const std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
    const std::wstring path(parts.lpszUrlPath,parts.dwUrlPathLength);
    Handle session(WinHttpOpen(L"DrivOnix/1 metadata",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
    WinHttpSetTimeouts(session.value,5000,5000,int(request.timeoutSeconds*1000),int(request.timeoutSeconds*1000));
    Handle connection(WinHttpConnect(session.value,host.c_str(),parts.nPort,0));
    const auto raw=WinHttpOpenRequest(connection.value,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    Interruptible operation(raw);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(request.timeoutSeconds);
    DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if(!WinHttpSetOption(raw,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects)))
        throw std::runtime_error("Cannot configure metadata redirect policy");
    std::wstring headers;
    if(!request.etag.empty())headers=L"If-None-Match: "+Wide(request.etag)+L"\r\n";
    operation.Begin();
    operation.Issued(WinHttpSendRequest(raw,headers.empty()?WINHTTP_NO_ADDITIONAL_HEADERS:headers.c_str(),DWORD(headers.size()),
                          WINHTTP_NO_REQUEST_DATA,0,0,reinterpret_cast<DWORD_PTR>(&operation)));
    operation.Wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE,cancel,deadline);
    operation.Begin();operation.Issued(WinHttpReceiveResponse(raw,nullptr));
    operation.Wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,cancel,deadline);
    HttpResponse result;DWORD status=0,length=sizeof(status);
    if(!WinHttpQueryHeaders(raw,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("Cannot read metadata HTTP status");
    result.status=status;result.etag=Header(raw,WINHTTP_QUERY_ETAG);result.contentType=Header(raw,WINHTTP_QUERY_CONTENT_TYPE);
    for(;;) {
        Check(cancel);operation.Begin();operation.Issued(WinHttpQueryDataAvailable(raw,nullptr));
        operation.Wait(WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE,cancel,deadline);
        if(!operation.available)break;
        const auto amount=std::min<DWORD>(operation.available,DWORD(operation.buffer.size()));
        operation.Begin();operation.Issued(WinHttpReadData(raw,operation.buffer.data(),amount,nullptr));
        operation.Wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE,cancel,deadline);
        if(!operation.count)break;
        if(operation.count>request.limit-result.bytes.size())throw std::runtime_error("Metadata response exceeds allowed size");
        result.bytes.insert(result.bytes.end(),operation.buffer.begin(),operation.buffer.begin()+operation.count);
    }
    Check(cancel);return result;
#else
    struct CurlGlobal { CurlGlobal(){if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)throw std::runtime_error("Cannot initialize metadata HTTP");} };
    static CurlGlobal global;(void)global;
    struct CurlHandle {CURL* value=curl_easy_init();~CurlHandle(){if(value)curl_easy_cleanup(value);}} handle;
    if(!handle.value)throw std::runtime_error("Cannot create metadata HTTP request");
    CurlState state{{},request,cancel,false,false,false};
    curl_easy_setopt(handle.value,CURLOPT_URL,request.url.c_str());
    curl_easy_setopt(handle.value,CURLOPT_PROTOCOLS_STR,"https");
    curl_easy_setopt(handle.value,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(handle.value,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(handle.value,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(handle.value,CURLOPT_SSLVERSION,long(CURL_SSLVERSION_TLSv1_2));
    curl_easy_setopt(handle.value,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(handle.value,CURLOPT_CONNECTTIMEOUT,5L);curl_easy_setopt(handle.value,CURLOPT_TIMEOUT,long(request.timeoutSeconds));
    curl_easy_setopt(handle.value,CURLOPT_USERAGENT,"DrivOnix/1 metadata");
    curl_easy_setopt(handle.value,CURLOPT_WRITEFUNCTION,Body);curl_easy_setopt(handle.value,CURLOPT_WRITEDATA,&state);
    curl_easy_setopt(handle.value,CURLOPT_HEADERFUNCTION,Headers);curl_easy_setopt(handle.value,CURLOPT_HEADERDATA,&state);
    curl_easy_setopt(handle.value,CURLOPT_XFERINFOFUNCTION,Progress);curl_easy_setopt(handle.value,CURLOPT_XFERINFODATA,&state);
    curl_easy_setopt(handle.value,CURLOPT_NOPROGRESS,0L);
    struct HeaderList {curl_slist* value=nullptr;~HeaderList(){if(value)curl_slist_free_all(value);}} headers;
    if(!request.etag.empty()) {
        headers.value=curl_slist_append(nullptr,("If-None-Match: "+request.etag).c_str());
        if(!headers.value)throw std::runtime_error("Cannot allocate metadata request headers");
        curl_easy_setopt(handle.value,CURLOPT_HTTPHEADER,headers.value);
    }
    const auto code=curl_easy_perform(handle.value);Check(cancel);
    if(state.overflow)throw std::runtime_error("Metadata response exceeds allowed size");
    if(state.failed)throw std::runtime_error("Cannot buffer metadata response");
    if(code!=CURLE_OK)throw std::runtime_error(std::string("Metadata HTTPS request failed: ")+curl_easy_strerror(code));
    long status=0;curl_easy_getinfo(handle.value,CURLINFO_RESPONSE_CODE,&status);state.response.status=unsigned(status);
    return state.response;
#endif
}

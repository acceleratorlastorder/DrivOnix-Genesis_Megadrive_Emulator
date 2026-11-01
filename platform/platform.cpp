#include "platform.hpp"
#include "settings.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#else
#include <gtk/gtk.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
#ifndef _WIN32
std::string ExecutableDirectory()
{
    std::vector<char> path(256);
    for (;;)
    {
        const ssize_t length = readlink("/proc/self/exe", path.data(), path.size());
        if (length < 0)
            throw std::runtime_error("Cannot find the executable directory");
        if (static_cast<std::size_t>(length) < path.size())
        {
            const std::string executable(path.data(), length);
            return executable.substr(0, executable.find_last_of("/\\"));
        }
        path.resize(path.size() * 2);
    }
}
#endif

void CheckPath(const std::string& path)
{
    if(path.empty() || path.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid settings path; use --config PATH or --portable");
}
bool Separator(char c)
{
#ifdef _WIN32
    return c=='/' || c=='\\';
#else
    return c=='/';
#endif
}
std::string Join(const std::string& directory,const std::string& child)
{
    CheckPath(directory);
    return directory + (Separator(directory.back()) ? "":"/") + child;
}
std::string Parent(const std::string& path)
{
    std::size_t last=path.size();
    while(last && !Separator(path[last-1])) --last;
    if(!last) return {};
    if(last==1 || (last>1 && path[last-2]==':')) return path.substr(0,last);
    return path.substr(0,last-1);
}
#ifdef _WIN32
std::wstring Wide(const std::string& text)
{
    CheckPath(text);
    if(text.size()>std::size_t(std::numeric_limits<int>::max())) throw std::runtime_error("Settings path is too long");
    const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0);
    if(!length) throw std::runtime_error("Settings path is not valid UTF-8: "+text);
    std::wstring result(std::size_t(length),L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),&result[0],length))
        throw std::runtime_error("Cannot decode settings path: "+text);
    return result;
}
std::string Utf8(const std::wstring& text)
{
    if(text.empty()) return {};
    const int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    if(!length) throw std::runtime_error("Cannot encode settings path as UTF-8");
    std::string result(std::size_t(length),'\0');
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),&result[0],length,nullptr,nullptr))
        throw std::runtime_error("Cannot encode settings path as UTF-8");
    return result;
}
bool Absolute(const std::string& path)
{
    return (path.size()>=3 && ((path[0]>='A' && path[0]<='Z') || (path[0]>='a' && path[0]<='z'))
        && path[1]==':' && Separator(path[2]))
        || (path.size()>=3 && Separator(path[0]) && Separator(path[1]));
}
std::string FullPath(const std::string& path)
{
    const auto wide=Wide(path);
    std::vector<wchar_t> buffer(256);
    for(;;)
    {
        const DWORD length=GetFullPathNameW(wide.c_str(),DWORD(buffer.size()),buffer.data(),nullptr);
        if(!length) throw std::runtime_error("Cannot resolve settings path: "+path);
        if(length<buffer.size()) return Utf8(std::wstring(buffer.data(),length));
        buffer.resize(std::size_t(length)+1);
    }
}
std::string SettingsExecutableDirectory()
{
    std::vector<wchar_t> path(256);
    for(;;)
    {
        const DWORD length=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));
        if(!length) throw std::runtime_error("Cannot find the settings executable directory");
        if(length<path.size()) return Parent(Utf8(std::wstring(path.data(),length)));
        path.resize(path.size()*2);
    }
}
std::string Environment(const wchar_t* name)
{
    std::vector<wchar_t> value(256);
    for(;;)
    {
        const DWORD length=GetEnvironmentVariableW(name,value.data(),DWORD(value.size()));
        if(!length) return {};
        if(length<value.size()) return Utf8(std::wstring(value.data(),length));
        value.resize(std::size_t(length)+1);
    }
}
void EnsureDirectory(const std::string& directory)
{
    const auto wide=Wide(directory);
    const DWORD attributes=GetFileAttributesW(wide.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES)
    {
        if(!(attributes&FILE_ATTRIBUTE_DIRECTORY)) throw std::runtime_error("Settings parent is not a directory: "+directory);
        return;
    }
    const DWORD error=GetLastError();
    if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND)
        throw std::runtime_error("Cannot inspect settings directory: "+directory);
    const auto parent=Parent(directory);
    if(parent.empty() || parent==directory) throw std::runtime_error("Cannot locate settings directory root: "+directory);
    EnsureDirectory(parent);
    if(!CreateDirectoryW(wide.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)
        throw std::runtime_error("Cannot create settings directory: "+directory);
    const DWORD created=GetFileAttributesW(wide.c_str());
    if(created==INVALID_FILE_ATTRIBUTES || !(created&FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("Settings parent is not a directory: "+directory);
}
bool ReadLegacy(const std::string& path,std::string& bytes)
{
    const auto wide=Wide(path);
    HANDLE file=CreateFileW(wide.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(file==INVALID_HANDLE_VALUE)
    {
        const DWORD error=GetLastError();
        if(error==ERROR_FILE_NOT_FOUND) return false;
        if(error==ERROR_PATH_NOT_FOUND)
        {
            // Windows uses PATH_NOT_FOUND for a regular file used as a parent
            // as well as for a genuinely missing directory. Do not silently
            // treat that existing obstruction as a new/missing configuration.
            auto parent=Parent(path);
            while(!parent.empty())
            {
                const DWORD attributes=GetFileAttributesW(Wide(parent).c_str());
                if(attributes!=INVALID_FILE_ATTRIBUTES)
                {
                    if(!(attributes&FILE_ATTRIBUTE_DIRECTORY)) throw std::runtime_error("Legacy settings parent is not a directory: "+parent);
                    return false;
                }
                const DWORD parentError=GetLastError();
                if(parentError!=ERROR_FILE_NOT_FOUND && parentError!=ERROR_PATH_NOT_FOUND)
                    throw std::runtime_error("Cannot inspect legacy settings parent: "+parent);
                const auto next=Parent(parent);if(next==parent) break;parent=next;
            }
            return false;
        }
        throw std::runtime_error("Cannot open legacy settings: "+path);
    }
    struct Close { HANDLE file; ~Close() {CloseHandle(file);} } close{file};
    char buffer[4096];DWORD count=0;
    do
    {
        if(!ReadFile(file,buffer,sizeof(buffer),&count,nullptr)) throw std::runtime_error("Cannot read legacy settings: "+path);
        bytes.append(buffer,count);
        if(bytes.size()>1024*1024) throw std::runtime_error("Legacy settings file exceeds 1 MiB: "+path);
    } while(count);
    return true;
}
#else
bool Absolute(const std::string& path) { return !path.empty() && path[0]=='/'; }
std::string CurrentDirectory()
{
    std::vector<char> buffer(256);
    while(!getcwd(buffer.data(),buffer.size()))
    {
        if(errno!=ERANGE) throw std::runtime_error("Cannot resolve settings working directory");
        buffer.resize(buffer.size()*2);
    }
    return buffer.data();
}
std::string FullPath(const std::string& path)
{
    CheckPath(path);
    if(path==".") return CurrentDirectory();
    return Absolute(path) ? path:Join(CurrentDirectory(),path);
}
std::string SettingsExecutableDirectory() { return ExecutableDirectory(); }
void EnsureDirectory(const std::string& directory)
{
    struct stat info={};
    if(stat(directory.c_str(),&info)==0)
    {
        if(!S_ISDIR(info.st_mode)) throw std::runtime_error("Settings parent is not a directory: "+directory);
        return;
    }
    if(errno!=ENOENT) throw std::runtime_error("Cannot inspect settings directory: "+directory);
    const auto parent=Parent(directory);
    if(parent.empty() || parent==directory) throw std::runtime_error("Cannot locate settings directory root: "+directory);
    EnsureDirectory(parent);
    if(mkdir(directory.c_str(),0700)!=0 && errno!=EEXIST) throw std::runtime_error("Cannot create settings directory: "+directory);
    if(stat(directory.c_str(),&info)!=0 || !S_ISDIR(info.st_mode)) throw std::runtime_error("Settings parent is not a directory: "+directory);
}
bool ReadLegacy(const std::string& path,std::string& bytes)
{
    const int file=open(path.c_str(),O_RDONLY|O_NONBLOCK);
    if(file<0)
    {
        if(errno==ENOENT) return false;
        throw std::runtime_error("Cannot open legacy settings: "+path);
    }
    struct Close { int file; ~Close() {close(file);} } closeFile{file};
    struct stat info={};
    if(fstat(file,&info)!=0 || !S_ISREG(info.st_mode)) throw std::runtime_error("Legacy settings is not a regular file: "+path);
    char buffer[4096];
    for(;;)
    {
        const auto count=read(file,buffer,sizeof(buffer));
        if(count<0) { if(errno==EINTR) continue;throw std::runtime_error("Cannot read legacy settings: "+path); }
        if(!count) break;
        bytes.append(buffer,std::size_t(count));
        if(bytes.size()>1024*1024) throw std::runtime_error("Legacy settings file exceeds 1 MiB: "+path);
    }
    return true;
}
#endif
std::string Trim(const std::string& text)
{
    const auto first=text.find_first_not_of(" \t\r\n");
    if(first==std::string::npos) return {};
    return text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
}
unsigned LegacyScreen(const std::string& text,const std::string& path)
{
    bool block=false;std::size_t first=0,line=0;
    while(first<text.size())
    {
        ++line;
        auto last=text.find('\n',first);if(last==std::string::npos) last=text.size();
        std::string clean;
        for(std::size_t i=first;i<last;)
        {
            if(block) { if(i+1<last && text[i]=='*' && text[i+1]=='/') {block=false;i+=2;} else ++i; }
            else if(i+1<last && text[i]=='/' && text[i+1]=='*') {block=true;i+=2;}
            else if(i+1<last && text[i]=='/' && text[i+1]=='/') break;
            else clean+=text[i++];
        }
        if(line==1 && clean.compare(0,3,"\xef\xbb\xbf")==0) clean.erase(0,3);
        const auto equal=clean.find('=');
        if(equal!=std::string::npos && Trim(clean.substr(0,equal))=="ScreenSize")
        {
            auto value=clean.substr(equal+1);const auto comment=value.find_first_of(";#");
            if(comment!=std::string::npos) value.resize(comment);
            value=Trim(value);
            if(value.size()!=1 || value[0]<'1' || value[0]>'3')
                throw std::runtime_error(path+": legacy settings line "+std::to_string(line)+" (ScreenSize): expected 1, 2 or 3");
            return unsigned(value[0]-'0');
        }
        first=last==text.size() ? last:last+1;
    }
    throw std::runtime_error(path+": legacy settings has no ScreenSize assignment");
}
}

std::string Platform::SelectRom()
{
#ifdef _WIN32
    wchar_t path[32768] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"Genesis/Megadrive ROMs and archives\0*.bin;*.md;*.gen;*.smd;*.zip;*.7z\0All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = DWORD(sizeof(path)/sizeof(path[0]));
    dialog.lpstrTitle = L"Select a ROM";
    dialog.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog))
        return Utf8(path);
    if (CommDlgExtendedError() != 0)
        throw std::runtime_error("Cannot open the ROM file dialog");
    return {};
#else
    if (!gtk_init_check(nullptr, nullptr))
        throw std::runtime_error("Cannot open the ROM picker. Run DrivOnix on a desktop or pass a ROM path.");
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Select a ROM", nullptr,
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Genesis/Megadrive ROMs and archives");
    for (const char* pattern : {"*.[bB][iI][nN]", "*.[mM][dD]", "*.[gG][eE][nN]", "*.[sS][mM][dD]", "*.[zZ][iI][pP]", "*.7[zZ]"})
        gtk_file_filter_add_pattern(filter, pattern);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    GtkFileFilter* allFiles = gtk_file_filter_new();
    gtk_file_filter_set_name(allFiles, "All files");
    gtk_file_filter_add_pattern(allFiles, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), allFiles);

    std::string selection;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
    {
        char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (path)
        {
            selection = path;
            g_free(path);
        }
    }
    gtk_widget_destroy(dialog);
    gdk_display_flush(gdk_display_get_default());
    return selection;
#endif
}

namespace {
#ifdef _WIN32
struct MemberDialog {
    const std::vector<std::wstring>& names;
    const std::wstring& archive;
    int selected = -1;
};
INT_PTR CALLBACK ArchiveDialog(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* state = reinterpret_cast<MemberDialog*>(GetWindowLongPtrW(window, DWLP_USER));
    if(message == WM_INITDIALOG) {
        state = reinterpret_cast<MemberDialog*>(lparam);
        SetWindowLongPtrW(window, DWLP_USER, lparam);
        SetWindowTextW(window, L"Select a game from archive");
        RECT area;GetClientRect(window, &area);
        const auto font = GetStockObject(DEFAULT_GUI_FONT);
        const auto instance = GetModuleHandleW(nullptr);
        const auto label = CreateWindowExW(0, L"STATIC", state->archive.c_str(), WS_CHILD|WS_VISIBLE,
            10, 8, area.right-20, 32, window, nullptr, instance, nullptr);
        const auto list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD|WS_VISIBLE|WS_TABSTOP|
            WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT, 10, 42, area.right-20, area.bottom-86,
            window, reinterpret_cast<HMENU>(100), instance, nullptr);
        if(!label || !list) {EndDialog(window, -2);return TRUE;}
        SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(list, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        for(const auto& name: state->names)
            if(SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()))<0) {EndDialog(window, -2);return TRUE;}
        SendMessageW(list, LB_SETCURSEL, 0, 0);
        const auto okay = CreateWindowExW(0, L"BUTTON", L"Open", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
            area.right-190, area.bottom-34, 80, 25, window, reinterpret_cast<HMENU>(IDOK), instance, nullptr);
        const auto cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            area.right-100, area.bottom-34, 90, 25, window, reinterpret_cast<HMENU>(IDCANCEL), instance, nullptr);
        if(!okay || !cancel) {EndDialog(window, -2);return TRUE;}
        SendMessageW(okay, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SetFocus(list);return FALSE;
    }
    if(message == WM_COMMAND && state) {
        if(LOWORD(wparam) == IDCANCEL) { EndDialog(window, IDCANCEL);return TRUE; }
        if(LOWORD(wparam) == IDOK || (LOWORD(wparam) == 100 && HIWORD(wparam) == LBN_DBLCLK)) {
            const auto index = SendDlgItemMessageW(window, 100, LB_GETCURSEL, 0, 0);
            if(index != LB_ERR) {state->selected = int(index);EndDialog(window, IDOK);}
            return TRUE;
        }
    }
    if(message == WM_CLOSE) {EndDialog(window, IDCANCEL);return TRUE;}
    return FALSE;
}
#else
gboolean SearchMember(GtkTreeModel* model, gint column, const gchar* query, GtkTreeIter* iter, gpointer)
{
    gchar* name=nullptr;gtk_tree_model_get(model, iter, column, &name, -1);
    gchar* foldedName=g_utf8_casefold(name ? name:"", -1);
    gchar* foldedQuery=g_utf8_casefold(query, -1);
    const bool absent=std::strstr(foldedName, foldedQuery)==nullptr;
    g_free(name);g_free(foldedName);g_free(foldedQuery);return absent;
}
#endif
}

std::string Platform::SelectArchiveMember(const std::string& archive, const std::vector<std::string>& members)
{
    if(members.empty()) throw std::runtime_error("Archive contains no supported ROMs: "+archive);
    if(members.size()==1) return members.front();
#ifdef _WIN32
    // A resource-free native dialog keeps the release independent of a UI toolkit.
    struct alignas(DWORD) Template { DLGTEMPLATE dialog; WORD menu=0, windowClass=0, title=0; } layout{};
    static_assert(offsetof(Template,menu)==sizeof(DLGTEMPLATE), "Dialog template words must follow its header");
    layout.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER;
    layout.dialog.cx=410;layout.dialog.cy=245;
    std::vector<std::wstring> names;for(const auto& name: members) names.push_back(Wide(name));
    const auto wideArchive=Wide(archive);
    MemberDialog state{names, wideArchive, -1};
    const auto result=DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &layout.dialog, nullptr,
        ArchiveDialog, reinterpret_cast<LPARAM>(&state));
    if(result<0) throw std::runtime_error("Cannot open the archive game chooser");
    return result==IDOK && state.selected>=0 ? members.at(std::size_t(state.selected)):std::string();
#else
    if(!gtk_init_check(nullptr, nullptr))
        throw std::runtime_error("Archive has multiple games; use --list-archive and --archive-member NAME, or open it on a desktop");
    auto* dialog=gtk_dialog_new_with_buttons("Select a game from archive", nullptr, GTK_DIALOG_MODAL,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 760, 460);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    auto* content=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    auto* label=gtk_label_new(archive.c_str());gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 6);
    auto* model=gtk_list_store_new(1, G_TYPE_STRING);
    for(const auto& name: members) {
        GtkTreeIter iter;gtk_list_store_append(model, &iter);gtk_list_store_set(model, &iter, 0, name.c_str(), -1);
    }
    auto* tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Game (type to search)", gtk_cell_renderer_text_new(), "text", 0, nullptr);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(tree), 0);
    gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(tree), SearchMember, nullptr, nullptr);
    auto* first=gtk_tree_path_new_first();gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree), first, nullptr, FALSE);gtk_tree_path_free(first);
    g_signal_connect(tree, "row-activated", G_CALLBACK(+[](GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer data) {
        gtk_dialog_response(GTK_DIALOG(data), GTK_RESPONSE_ACCEPT);
    }), dialog);
    auto* scroll=gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(scroll), tree);gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 6);
    gtk_widget_show_all(dialog);gtk_widget_grab_focus(tree);
    std::string selection;
    if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) {
        GtkTreeIter iter;
        if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), nullptr, &iter)) {
            gchar* name=nullptr;gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 0, &name, -1);
            if(name) {selection=name;g_free(name);}
        }
    }
    gtk_widget_destroy(dialog);g_object_unref(model);gdk_display_flush(gdk_display_get_default());return selection;
#endif
}

std::string Platform::DataFile(const std::string& relativePath)
{
    const std::string directory = SettingsExecutableDirectory();
    // Preserve local configuration overrides and support both build and install trees.
    const std::vector<std::string> candidates = {
        relativePath,
        directory + "/" + relativePath,
        directory + "/../share/drivonix/" + relativePath,
#ifdef DRIVONIX_INSTALL_DATA_DIR
        std::string(DRIVONIX_INSTALL_DATA_DIR) + "/" + relativePath,
#endif
    };
    for (const auto& candidate : candidates) {
#ifdef _WIN32
        auto* file = _wfopen(Wide(candidate).c_str(), L"rb");
#else
        auto* file = std::fopen(candidate.c_str(), "rb");
#endif
        if(file) {std::fclose(file);return candidate;}
    }
    throw std::runtime_error("Cannot find " + relativePath + ". Keep the bios and config directories with drivonix.");
}

std::string Platform::SettingsPath(const std::string& custom,bool portable)
{
    if(!custom.empty() && portable) throw std::invalid_argument("--config and --portable cannot be used together");
    if(!custom.empty()) return FullPath(custom);
    if(portable) return Join(SettingsExecutableDirectory(),"config/drivonix.ini");
#ifdef _WIN32
    std::string base=Environment(L"LOCALAPPDATA");
    if(base.empty() || !Absolute(base))
    {
        struct Folder { PWSTR path=nullptr;~Folder(){CoTaskMemFree(path);} } folder;
        if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&folder.path)))
            throw std::runtime_error("Cannot locate settings directory; use --config PATH or --portable");
        base=Utf8(folder.path);
    }
    return Join(FullPath(base),"drivonix/drivonix.ini");
#else
    const char* xdg=std::getenv("XDG_CONFIG_HOME");
    std::string base=xdg ? xdg:"";
    if(base.empty() || !Absolute(base))
    {
        const char* home=std::getenv("HOME");
        if(!home || !Absolute(home)) throw std::runtime_error("Cannot locate settings directory; set HOME or use --config PATH or --portable");
        base=Join(home,".config");
    }
    return Join(base,"drivonix/drivonix.ini");
#endif
}

void Platform::EnsureSettingsDirectory(const std::string& filePath)
{
    const auto parent=Parent(FullPath(filePath));
    if(!parent.empty()) EnsureDirectory(parent);
}

bool Platform::ImportLegacySettings(Settings::Values& values,std::string& sourcePath)
{
    const auto executable=SettingsExecutableDirectory();
    std::vector<std::string> bases={FullPath("."),executable,Join(executable,"../share/drivonix")};
#ifdef DRIVONIX_INSTALL_DATA_DIR
    bases.push_back(DRIVONIX_INSTALL_DATA_DIR);
#endif
    for(const auto& base:bases) for(const char* relative:{"config/config.txt","Config/config.txt"})
    {
        const auto path=Join(base,relative);std::string bytes;
        if(!ReadLegacy(path,bytes)) continue;
        const auto screen=LegacyScreen(bytes,path);
        auto imported=values;imported.scale=screen==2 ? 2:1;imported.fullscreen=screen==3;
        Settings::Validate(imported);
        values=imported;sourcePath=path;return true;
    }
    return false;
}

void Platform::CheckDisplay()
{
#ifndef _WIN32
    const char* display = std::getenv("DISPLAY");
    if (!display || !*display)
        throw std::runtime_error("DrivOnix needs an X11 display (or XWayland) to show the emulator window.");
#endif
}

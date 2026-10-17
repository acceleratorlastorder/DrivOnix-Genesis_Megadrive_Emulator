#include "platform.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#else
#include <gtk/gtk.h>
#include <unistd.h>
#endif

namespace
{
std::string ExecutableDirectory()
{
    std::vector<char> path(256);
    for (;;)
    {
#ifdef _WIN32
        const DWORD length = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0)
            throw std::runtime_error("Cannot find the executable directory");
        if (length < path.size())
#else
        const ssize_t length = readlink("/proc/self/exe", path.data(), path.size());
        if (length < 0)
            throw std::runtime_error("Cannot find the executable directory");
        if (static_cast<std::size_t>(length) < path.size())
#endif
        {
            const std::string executable(path.data(), length);
            return executable.substr(0, executable.find_last_of("/\\"));
        }
        path.resize(path.size() * 2);
    }
}
}

std::string Platform::SelectRom()
{
#ifdef _WIN32
    char path[32768] = {};
    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = "Genesis/Megadrive ROMs\0*.bin;*.md;*.gen;*.smd\0All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = sizeof(path);
    dialog.lpstrTitle = "Select a ROM";
    dialog.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&dialog))
        return path;
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
    gtk_file_filter_set_name(filter, "Genesis/Megadrive ROMs");
    for (const char* pattern : {"*.[bB][iI][nN]", "*.[mM][dD]", "*.[gG][eE][nN]", "*.[sS][mM][dD]"})
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

std::string Platform::DataFile(const std::string& relativePath)
{
    const std::string directory = ExecutableDirectory();
    // Preserve local configuration overrides and support both build and install trees.
    const std::vector<std::string> candidates = {
        relativePath,
        directory + "/" + relativePath,
        directory + "/../share/drivonix/" + relativePath,
#ifdef DRIVONIX_INSTALL_DATA_DIR
        std::string(DRIVONIX_INSTALL_DATA_DIR) + "/" + relativePath,
#endif
    };
    for (const auto& candidate : candidates)
        if (std::ifstream(candidate, std::ios::binary).good())
            return candidate;
    throw std::runtime_error("Cannot find " + relativePath + ". Keep the Bios and Config directories with DrivOnix.");
}

void Platform::CheckDisplay()
{
#ifndef _WIN32
    const char* display = std::getenv("DISPLAY");
    if (!display || !*display)
        throw std::runtime_error("DrivOnix needs an X11 display (or XWayland) to show the emulator window.");
#endif
}

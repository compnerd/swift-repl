#include <iostream>
#include <string>
#include <mutex>

#include "CommandLineOptions.h"
#include "REPL.h"
#include "Strings.h"

#include <windows.h>
#include <windowsx.h>
#include <winuser.h>
#include <richedit.h>
#include <namedpipeapi.h>
#include <io.h>
#include <fcntl.h>

CommandLineOptions g_opts;

HANDLE g_stdout_write_pipe;
HANDLE g_stdout_read_pipe;
HWND g_output;
std::vector<char> g_output_text;
std::mutex g_output_text_lock;

void SetFontToConsolas(HWND window_handle)
{
    HFONT font_handle = CreateFont(
        18, 0, 0, 0,
        FW_NORMAL,
        false, false, false,
        ANSI_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY,
        FF_DONTCARE,
        "Consolas");
    SendMessage(window_handle, WM_SETFONT, reinterpret_cast<WPARAM>(font_handle), true);
}

HWND CreateRichEdit(HINSTANCE instance_handle,
                    HWND owner_handle,
                    int x, int y,
                    int width, int height)
{
    LoadLibrary(TEXT("Riched32.dll"));
    HWND edit_handle = CreateWindowEx(
        0, "RICHEDIT", TEXT(""),
        ES_MULTILINE | WS_VISIBLE | WS_CHILD | WS_BORDER | WS_TABSTOP | WS_VSCROLL,
        x, y, width, height,
        owner_handle, nullptr, instance_handle, nullptr);
    SendMessage(edit_handle, EM_SETEVENTMASK, 0, static_cast<LPARAM>(ENM_CHANGE));
    return edit_handle;
}

HWND CreateButton(HINSTANCE instance_handle,
                  HWND owner_handle,
                  int x, int y,
                  int width, int height,
                  const char *text)
{
    HWND btn_handle = CreateWindowEx(
        0, "BUTTON", text,
        WS_VISIBLE | WS_CHILD,
        x, y, width, height,
        owner_handle, nullptr, instance_handle, nullptr);
    Button_Enable(btn_handle, true);
    return btn_handle;
}

std::string GetTextboxText(HWND textbox)
{
    GETTEXTLENGTHEX length_info = { GTL_CLOSE, CP_ACP };
    LONG buff_size = SendMessage(textbox, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&length_info), 0) + 1; // Add 1 for null terminator
    std::vector<char> buff(buff_size);
    GETTEXTEX get_text_info = { static_cast<DWORD>(buff_size), GT_DEFAULT, CP_ACP, nullptr, nullptr };
    SendMessage(textbox, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&get_text_info), reinterpret_cast<LPARAM>(buff.data()));
    return std::string(buff.data());
}

unsigned long UpdateOutputTextbox(void *)
{
    for(;;)
    {
        char buff[128];
        DWORD bytes_read;
        ReadFile(g_stdout_read_pipe, buff, sizeof(buff), &bytes_read, nullptr);

        {
            std::lock_guard<std::mutex> guard(g_output_text_lock);
            g_output_text.insert(g_output_text.end(),
                                 buff, buff + bytes_read);
            g_output_text.push_back('\0');
            Edit_SetText(g_output, g_output_text.data());
            g_output_text.pop_back();
        }
    }
}

void ClearOutputTextBox()
{
    std::lock_guard<std::mutex> guard(g_output_text_lock);
    g_output_text.clear();
    char null_char = '\0';
    Edit_SetText(g_output, &null_char);
}

struct Playground
{
    void ResetREPL();
    void UpdateContinueButtonText();
    void HandleTextChange();
    void RecompileEverything();
    void ContinueExecution();

    HWND m_recompile_btn;
    HWND m_continue_btn;
    HWND m_text;
    int m_min_line;
    int m_num_lines;
    std::string m_prev_text;
    std::string m_curr_text;
    std::unique_ptr<REPL> m_repl;
};

void Playground::ResetREPL()
{
    llvm::Expected<std::unique_ptr<REPL>> repl = REPL::Create(g_opts.is_playground);
    if(!repl)
    {
        std::string err_str;
        llvm::raw_string_ostream stream(err_str);
        stream << repl.takeError();
        stream.flush();
        SetCurrentLoggingArea(LoggingArea::All);
        Log(err_str, LoggingPriority::Error);
    }

    std::for_each(g_opts.include_paths.begin(), g_opts.include_paths.end(),
                  [&](auto s) { (*repl)->AddModuleSearchPath(s); });
    std::for_each(g_opts.link_paths.begin(), g_opts.link_paths.end(),
                  [&](auto s) { (*repl)->AddLoadSearchPath(s); });

    m_repl = std::move(*repl);
}

void Playground::UpdateContinueButtonText()
{
    std::string new_btn_text = "Continue Execution from Line " + std::to_string(m_min_line);
    if(m_min_line == 0)
        new_btn_text = "Recompile Everything";
    Button_SetText(m_continue_btn, new_btn_text.c_str());
}

void Playground::HandleTextChange()
{
    m_curr_text = GetTextboxText(m_text);
    Trim(m_curr_text);
    m_curr_text.erase(std::remove(m_curr_text.begin(), m_curr_text.end(), '\r'), m_curr_text.end()); // Effectively changes \r\n to \n
    if(!StartsWith(m_curr_text, m_prev_text))
        m_min_line = 0;
    m_num_lines = std::count(m_curr_text.begin(), m_curr_text.end(), '\n') + 1;
    UpdateContinueButtonText();
}

void Playground::RecompileEverything()
{
    ResetREPL();
    ClearOutputTextBox();
    m_repl->ExecuteSwift(m_curr_text);
    m_prev_text = m_curr_text;
    m_min_line = m_num_lines;
    UpdateContinueButtonText();
}

void Playground::ContinueExecution()
{
    if(m_min_line == 0)
        return RecompileEverything();

    int i = 0;
    for(int curr_line = 1; i < m_curr_text.size(); i++)
    {
        if(m_curr_text[i] == '\n')
            curr_line++;
        if(curr_line == m_min_line + 1)
            break;
    }
    std::string to_execute = m_curr_text.substr(i);
    m_repl->ExecuteSwift(to_execute);
    m_prev_text = m_curr_text;
    m_min_line = m_num_lines;
    UpdateContinueButtonText();
}

LRESULT CALLBACK PlaygroundWindowProc(HWND window_handle, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_COMMAND:
    {
        Playground *playground = reinterpret_cast<Playground *>(GetWindowLongPtr(window_handle, GWLP_USERDATA));
        assert(playground);
        if(HIWORD(wparam) == EN_CHANGE && reinterpret_cast<HWND>(lparam) == playground->m_text)
        {
            playground->HandleTextChange();
        }
        else
        {
            if(HIWORD(wparam) == BN_CLICKED  && reinterpret_cast<HWND>(lparam) == playground->m_recompile_btn)
                playground->RecompileEverything();
            else if(HIWORD(wparam) == BN_CLICKED && reinterpret_cast<HWND>(lparam) == playground->m_continue_btn)
                playground->ContinueExecution();
            fflush(stdout);
        }
        return 0;
    }
    case WM_DESTROY:
    {
        PostQuitMessage(0);
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(window_handle, &ps);
        FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));
        EndPaint(window_handle, &ps);
        return 0;
    }
    default:
        return DefWindowProc(window_handle, message, wparam, lparam);
    }
}

int main(int argc, char **argv)
{
    g_opts = ParseCommandLineOptions(argc, argv);
    g_opts.is_playground = true;
    SetLoggingOptions(g_opts.logging_opts);

    HINSTANCE instance_handle = GetModuleHandle(nullptr);
    WNDCLASS wc = {};
    wc.lpfnWndProc   = PlaygroundWindowProc;
    wc.hInstance     = instance_handle;
    wc.lpszClassName = "Playground Class";
    RegisterClass(&wc);

    HWND window = CreateWindowEx(
        0, "Playground Class", "Swift Playground", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, instance_handle, nullptr);

    if(!window)
    {
        Log(std::string("Failed to open window. Last Error: ") + std::to_string(GetLastError()), LoggingPriority::Error);
        return 1;
    }

    Playground playground;
    playground.m_recompile_btn = CreateButton(instance_handle, window, 350, 100, 300, 100, "Recompile Everything");
    playground.m_continue_btn = CreateButton(instance_handle, window, 350, 200, 300, 100, "Continue Execution From Line 1");
    playground.m_min_line = 0;
    playground.m_num_lines = 1;
    playground.m_text = CreateRichEdit(instance_handle, window, 50, 50, 300, 600);
    SetFontToConsolas(playground.m_text);

    g_output = CreateRichEdit(instance_handle, window, 50, 650, 600, 300);
    Edit_SetReadOnly(g_output, true);
    SetFontToConsolas(g_output);

    SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&playground));
    ShowWindow(window, SW_SHOW);

    //NOTE(sasha): There are \Bigg{\emph{\textbf{SEVERE}}} performance issues if
    //             pipe buffer size is too small. We just pass the standard on Linux,
    //             which is 0xFFFF.
    if(!CreatePipe(&g_stdout_read_pipe, &g_stdout_write_pipe, nullptr, 0xFFFF))
    {
        Log(std::string("Failed to create pipe. Last Error: ") + std::to_string(GetLastError()), LoggingPriority::Error);
        return 1;
    }
    if(!SetStdHandle(STD_OUTPUT_HANDLE, g_stdout_write_pipe))
    {
        Log(std::string("Failed to set std handle. Last Error: ") + std::to_string(GetLastError()), LoggingPriority::Error);
        return 1;
    }
    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(g_stdout_write_pipe),
                             _O_APPEND | _O_TEXT);
    _dup2(fd, _fileno(stdout));

    HANDLE update_thread = CreateThread(
        nullptr, 0, UpdateOutputTextbox,
        nullptr, 0, nullptr);

    MSG msg = {};
    while(GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    _close(fd);
    return 0;
}
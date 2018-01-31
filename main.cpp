
// eudo - Elevate User and DO something!
//
// Contributors, if you're into that sort of thing:
//     Jake Stine
//

#define _WIN32_WINNT _WIN32_WINNT_VISTA
#define NTDDI_VERSION NTDDI_VISTA

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <intrin.h>

// Windows things...
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include <crtdbg.h>
#include <Shlwapi.h>
#include <VersionHelpers.h>

// disable warning C4201: nonstandard extension used: nameless struct/union
// This program has no goal or intention of being cross-compiled or cross-platform compatible.
#pragma warning(disable:4201)

// No reason not to use OutputDebugString at all times.  It's a perfectly valid use case that a developer
// might want to attach a debugger to the release build of this process and capture it's output.
#if !defined(USE_OUTPUT_DEBUG_STRING)
#   ifdef _DEBUG
#       define USE_OUTPUT_DEBUG_STRING          1
#   else
#       define USE_OUTPUT_DEBUG_STRING          1
#   endif
#endif

using ArgContainer = std::vector<std::wstring>;

static const int xMaxEnviron = 32768;
static const int xMaxPath    = 32768;

static bool g_Verbose = false;

void xFormatInto(std::wstring& dest, const WCHAR* fmt, va_list list)
{
    int destSize = _vscwprintf( fmt, list );

    // bypasses error that will occur on &m_string[0] (msvc runtime iterator checking)
    if (destSize>0) {
        // _vscprintf doesn't count terminating '\0', and resize() doesn't expect it either.
        // Thus, the following resize() will ensure +1 room for the null that vsprintf_s
        // will write.
        dest.resize( destSize );
        vswprintf_s( &dest[0], destSize+1, fmt, list );
    }
}

void log_error_v(const WCHAR* msg, va_list list)
{
    if (!msg) return;

#if USE_OUTPUT_DEBUG_STRING
    std::wstring dest;
    xFormatInto(dest, msg, list);
    fwprintf(stderr, L"%s", dest.c_str());
    OutputDebugString(dest.c_str());
#else
    vfwprintf(stderr, msg, list);
#endif
}

void log_console_v(const WCHAR* msg, va_list list)
{
    if (!msg) return;

#if USE_OUTPUT_DEBUG_STRING
    std::wstring dest;
    xFormatInto(dest, msg, list);
    fwprintf(stdout, L"%s", dest.c_str());
    OutputDebugString(dest.c_str());
#else
    vfwprintf(stdout, msg, list);
#endif
}

void log_error(const WCHAR* msg=nullptr, ...)
{
    if (!msg) return;

    std::wstring dest;
    va_list list;
    va_start(list, msg);
    log_error_v(msg, list);
    va_end(list);
}

void log_console(const WCHAR* msg=nullptr, ...)
{
    if (!msg) return;

    std::wstring dest;
    va_list list;
    va_start(list, msg);
    log_console_v(msg, list);
    va_end(list);
}

struct AssertionContextInfo {
    WCHAR*  cond;
    WCHAR*  file;
    int     line;
};

void _log_bug_cond(const AssertionContextInfo& ctx, const WCHAR* msg=nullptr, ...)
{
    log_error(L"%s(%d): ", ctx.file, ctx.line);
    if (!msg) {
        log_error(L"%s\n", msg);
    }
    else {
        va_list list;
        va_start(list, msg);
        log_error_v(msg, list);
        log_error  (L"\n");
        va_end(list);
    }
}

void _log_abort_cond(const AssertionContextInfo& ctx, const WCHAR* msg=nullptr, ...)
{
    if (!msg) {
        log_error(L"%s\n", ctx.cond);
    }
    else {
        va_list list;
        va_start(list, msg);
        log_error_v(msg, list);
        log_error  (L"\n");
        va_end(list);
    }
}

void x_abortbreak()
{
    abort();
}


#ifdef _DEBUG
#   define debug_log(fmt, ...)        log_console(fmt, ## __VA_ARGS__)
#   define bug_on(cond, ...)          ((cond) &&    (_log_bug_cond  ( { _T("bugged on: "   #cond),   _T(__FILE__), __LINE__ }, __VA_ARGS__ ), __debugbreak(), false))
#else
#   define debug_log(fmt, ...)        (void(0))
#   define bug_on(cond, ...)          (void(0))
#endif

#define x_abort(...)                  (             (_log_abort_cond( { _T("aborted"),               _T(__FILE__), __LINE__ }, __VA_ARGS__ ), x_abortbreak(), false))
#define x_abort_on(cond, ...)         ((cond) &&    (_log_abort_cond( { _T("aborted on: "  #cond),   _T(__FILE__), __LINE__ }, __VA_ARGS__ ), x_abortbreak(), false))

// note that I use MIPS notation for things, because it makes more sense in 32 and 64-bit architectures:
//      mips : byte, halfword,  word,        doubleword (64 bits), quadword        (128 bits),  double-quadword
//      i86  : byte, word,      doubleword,  quadword   (64 bits), double-quadword (128 bits),  ..failoverflow..

union Ev_ShellExecFlags {
    uint32_t        w;
    struct {
        uint32_t    ComspecRemains      : 1;
        uint32_t    DoNotWaitForProc    : 1;
        uint32_t    HideWindow          : 1;
    };
};

std::wstring HRESULT_to_string(HRESULT result)
{
    if(!result) return {};

    WCHAR* messageBuffer = nullptr;
    size_t size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, result, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (WCHAR*)&messageBuffer, 0, NULL);

    std::wstring message(messageBuffer, size);
    LocalFree(messageBuffer);
    return message;
}

std::wstring xStringFormat(const WCHAR* fmt, ...)
{
    // avoid microsoft's _vsnwprintf_s() because it's just loaded with more unwanted behavior that
    // interferes with or complicates the simple process of getting the length of string.

    va_list list;
    va_list copy;
    va_start(list, fmt);
    va_copy (copy,list);
    auto amt = _vscwprintf(fmt, copy);
    std::wstring result;
    if (amt > 0) {
        result.resize(amt);
        _vsnwprintf(result.data(), amt, fmt, list);
    }
    va_end(copy);
    va_end(list);
    return result;
}

HRESULT ev_GetEnvironmentVariable(const WCHAR* varname, std::wstring& dest)
{
    WCHAR temp[xMaxEnviron];
    if (auto hr = GetEnvironmentVariable(varname, temp, _countof(temp))) {
        dest = temp;
        return 0;
    }
    else {
        return GetLastError();
    }
}

std::wstring ev_GetCurrentDir()
{
    auto reqsize = GetCurrentDirectory(0, nullptr);
    std::wstring result;
    result.resize(reqsize-1);
    auto newsize = GetCurrentDirectory(reqsize, result.data());
    bug_on(newsize != (reqsize-1));
    return result;
}


std::wstring ev_AssocQueryString(ASSOCSTR str, const std::wstring& extension)
{
    // get required buffer size by passing null, then return the result.
    // abort on errors, since there's no known runtime error for AssocQueryString that we deem recoverable.
    // paranoia: msdn is unclear whether pcchOut includes null character or not, so assume not.
    // (why is there no StrSafe version of _this_ function ??)

    DWORD buflen = 0;
    auto hr = AssocQueryString(0, str, extension.c_str(), nullptr, nullptr, &buflen);
    if (hr == HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION)) {
        return {};
    }
    x_abort_on(hr && (hr != S_FALSE), L"AssocQueryString('%s') failed.\nWindows error 0x%08x - %s",
        extension.c_str(), hr, HRESULT_to_string(hr).c_str()
    );

    std::wstring result;
    result.resize(buflen);
    buflen += 1;
    hr = AssocQueryString(ASSOCF_NOTRUNCATE, str, extension.c_str(), nullptr, result.data(), &buflen);
    x_abort_on(hr, L"AssocQueryString('%s') failed.\nWindows error 0x%08x - ",
        extension.c_str(), hr, HRESULT_to_string(hr).c_str()
    );

    return result;
}


int ShellExec(const WCHAR* ApplicationName, const WCHAR* CommandLine, const Ev_ShellExecFlags& flags)
{
    if (g_Verbose) {
        log_console(L"ShellExec(\n  App  = %s\n  Args = %s\n)\n", ApplicationName, CommandLine);
    }

    SHELLEXECUTEINFO Shex = {};
    Shex.cbSize         = sizeof( SHELLEXECUTEINFO );
    Shex.fMask          = SEE_MASK_NO_CONSOLE | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS;

    // TODO: test these!
    // |= SEE_MASK_NOASYNC;
    // |= SEE_MASK_NO_CONSOLE;

    auto cwd = ev_GetCurrentDir();

    Shex.lpVerb         = L"runas";
    Shex.lpFile         = ApplicationName;
    Shex.lpParameters   = CommandLine;
    Shex.nShow          = flags.HideWindow ? SW_HIDE : SW_SHOW;

    if (!ShellExecuteEx(&Shex))
    {
        HRESULT Err = HRESULT_FROM_WIN32(GetLastError());

        log_error(
            L"%s could not be launched\nWindows Error 0x%08x - %s \n",
            ApplicationName,
            Err,
            HRESULT_to_string(Err).c_str()

        );
        return EXIT_FAILURE;
    }

    _ASSERTE(Shex.hProcess);

    if (!flags.DoNotWaitForProc)
    {
        DWORD procExitCode;
        WaitForSingleObject(Shex.hProcess, INFINITE);
        GetExitCodeProcess (Shex.hProcess, &procExitCode);
    }
    CloseHandle (Shex.hProcess);
    return EXIT_SUCCESS;
}

std::wstring xStringJoin(const WCHAR* joiner, const ArgContainer& container)
{
    std::wstring result;
    for(const auto& item : container) {
        if (!result.empty()) {
            result += joiner;
        }
        result += item;
    }
    return result;
}


std::wstring FindBestExt(const std::wstring& appName)
{
    const WCHAR* extpos = nullptr;

    // PathFindExtension() - verified works for LPN, no need to use PathCchFindExtension version.
    extpos = PathFindExtension(appName.c_str());

    if (extpos && extpos[0] == '.') {
        return extpos;
    }

    // no extension, use $PATHEXT to figure out what the extension might be.

    std::wstring environVarBuffer;
    std::wstring thisExt;
    std::wstring appTry;

    HRESULT hr = ev_GetEnvironmentVariable(L"PATHEXT", environVarBuffer);
    if (hr) {
        log_error(L"WARN- GetEnvironmentVariable('PATHEXT') failed.\nWindow error 0x%08x - %s\n", hr, HRESULT_to_string(hr).c_str());
        return {};
    }

    // Need to slice this string according to semi-colon.
    // after much deliberation, strtok the old-old-fashioned way:
    int i = 0;
    while(1) {
        auto curch = (i < environVarBuffer.length()) ? environVarBuffer[i] : L'\0';
        i += 1;
        if (!curch || curch == L';') {
            if (!thisExt.empty()) {
                appTry = appName + thisExt;
                if (PathFileExists(appTry.c_str())) {
                    return thisExt;
                }
                thisExt.clear();
            }

            if (!curch) { return {}; }
        }
        else {
            thisExt += curch;
        }
    }
}

std::wstring escape_quotes(const WCHAR* src)
{
    if (!src) return {};

    bool hasSpace = 0;
    std::wstring result;
    while(src[0]) {
        if (src[0] == L'"') {
            result += L"\\";
        }
        hasSpace = hasSpace || (src[0] == L' ') || (src[0] == L'\t') || (src[0] == L'\r') || (src[0] == L'\n');
        result += src[0];
        ++src;
    }

    // only add double quotes if whitespace detected.
    // Ensures we get as much out of the max-allowed xMaxEnviron length as possible.

    if (hasSpace) {
        return L"\"" + result + L"\"";
    }
    else {
        return result;
    }
}

int ExecComspec(const ArgContainer& cmdargs, const Ev_ShellExecFlags& flags_in)
{
    std::wstring environVarBuffer;
    std::wstring CmdLineBuffer;

    Ev_ShellExecFlags flags = flags_in;
    if (flags.HideWindow && flags.ComspecRemains) {
        log_error(L"WARN- refusing to hide an interactive COMSPEC since it leads to\n");
        log_error(L"  an orphaned process.\n");
    }

    if (HRESULT hr = ev_GetEnvironmentVariable( L"COMSPEC", environVarBuffer)) {
        log_error(L"ERROR- %%COMSPEC%% environment variable is undefined or empty.\n");
        log_error(L"  %%COMSPEC%% must be defined when specifying switches -c|-k\n");
        _exit(EXIT_FAILURE);
    }

    // As of Windows 8, there's a security restriction that prevents cmd.exe from running inside
    // the CWD when elevated to administrator.  This is no doubt some hurried cheap workaround
    // for some silly user-opt-in security problem that is more correctly solved by telling
    // users to stop downloading and running untrusted software.  But since this is Microsoft's
    // world, we all have to pay the piper for the idiocy of the lowest common denominator.
    //
    // The workaround is instead of running the command directly, to do this:
    //    cd /d %CWD%  && [actual-command]
    //
    // (if you're wondering how the Win8 policy change is supposed to protect anything when the
    //  workaround is really that easy, then please join the club and scratch your head).
    //
    // But there is one small caveat:
    //
    //   $ cmd /C cd /d c:\mydir && echo success
    //
    // ... somehow executes cmd /C cd /d c:\mydir, and then runs `echo success` after the CMD is closed.
    //     This happens from both the cmd prompt and from ShellExecuteEx().  But there's a workaround:
    //
    //   $ cmd /C " cd /d c:\mydir && echo success
    //
    // Yup, just add that quote.  CMD has some twisted bizarre way of handling quotes and nested quotes.
    // while parsing /C and /K:  As far as I can tell, a double-quote at the start of the command will
    // direct cmd.exe to interpret the rest of the command line as a single command -- no need to escape
    // nested quotes or even provide the closing quote.  So that's what we do!

    //                                  \/  and there's our mystery quote that makes it all work.
    CmdLineBuffer = xStringFormat(L"/%c \" cd /d \"%s\" %s %s",
        flags.ComspecRemains ? L'K' : L'C',
        ev_GetCurrentDir().c_str(),
        cmdargs.size() ? L"&&" : L"",
        xStringJoin(L" ", cmdargs).c_str()
    );
    return ShellExec(environVarBuffer.c_str(), CmdLineBuffer.c_str(), flags);
}

int ExecAssoc(const std::wstring& executable_fullpath, const ArgContainer& cmdargs, const Ev_ShellExecFlags& flags_in)
{
    // Basic rules for executing a program on Windows are according to extension, which might seem odd to
    // anyone with a strong background in software engineering.  Windows is also structured in such a way
    // that it's tricky to launch processes directly and have them behave in a desired fashion, due to
    // filetype associations requiring some amount of command shell variable expansion.
    //
    // The first step is determining the correct extension to look up out of the association table.
    // If a file has _no extension_ then possible matches are created using $PATHEXT.  If any of the
    // $PATHEXT extensions create a valid existing file, then that file and it's extension are used.
    //
    // A typical command shell association looks something like these:
    //   Msi.Package="%SystemRoot%\System32\msiexec.exe" /i "%1" %*
    //   sh_auto_file="C:\Program Files\Git\git-bash.exe" --no-cd "%L" %*
    //
    //      %1 - is the command itself
    //      %L - appears to just be an alias for %1
    //      %* - expands as all other parameters for the command
    //
    // The Good News:
    //   Other types of expansion do _not_ appear to be supported.  For example, %~dp1 is not processed.
    //   This reduces the scope of complexity to something we can reasonably simulate here.
    //

    auto extension    = FindBestExt(executable_fullpath);
    auto exe_fullname = executable_fullpath + extension;

    if (!extension.empty()) {
        auto strCmd                 = ev_AssocQueryString(ASSOCSTR_COMMAND,         extension.c_str());

        if (g_Verbose) {
            auto strFriendlyProgramName = ev_AssocQueryString(ASSOCSTR_FRIENDLYAPPNAME, extension.c_str());
            auto strExe                 = ev_AssocQueryString(ASSOCSTR_EXECUTABLE,      extension.c_str());
            log_console(L"Assoc FriendlyName = %s\n", strFriendlyProgramName.c_str());
            log_console(L"Assoc Command      = %s\n", strCmd.c_str());
            log_console(L"Assoc Exe Fullpath = %s\n", strExe.c_str());
        }

        if (strCmd.empty()) {
            log_error(L"%s: is not an executable program.", executable_fullpath.c_str());
            _exit(EXIT_FAILURE);
        }

        // token replacement time!  Replace %1, %*, %L, etc.

        exe_fullname = executable_fullpath;

        std::wstring CmdLineBuffer;
        if (1) {
            int i = 0;
            while(i < strCmd.length()) {
                auto curch = (i < strCmd.length()) ? strCmd[i] : L'\0';
                i += 1;
                if (!curch) break;

                if (curch == L'%') {
                    auto nextch = (i < strCmd.length()) ? strCmd[i] : L'\0';
                    i += 1;

                    switch(nextch)
                    {
                        case L'%':
                            CmdLineBuffer += L'%';
                        break;

                        case L'L':
                        case L'l':
                        case L'1':
                            CmdLineBuffer += exe_fullname;
                        break;

                        case L'*':
                            CmdLineBuffer += xStringJoin(L" ", cmdargs);
                        break;

                        case L'2': CmdLineBuffer += cmdargs[1]; break;
                        case L'3': CmdLineBuffer += cmdargs[2]; break;
                        case L'4': CmdLineBuffer += cmdargs[3]; break;
                        case L'5': CmdLineBuffer += cmdargs[4]; break;
                        case L'6': CmdLineBuffer += cmdargs[5]; break;
                        case L'7': CmdLineBuffer += cmdargs[6]; break;
                        case L'8': CmdLineBuffer += cmdargs[7]; break;
                        case L'9': CmdLineBuffer += cmdargs[8]; break;

                        default:
                            // mimic windows CMD.exe behavior, which is to just output the % unmodified if the command isn't supported.
                            CmdLineBuffer += L'%';
                            CmdLineBuffer += curch;
                        break;
                    }
                }
                else {
                    CmdLineBuffer += curch;
                }
            }
        }

        debug_log(L"Expanded Invocation= %s\n", CmdLineBuffer.c_str());

        // ShellExecuteEx needs to have the Application/Command separated from the Command Arguments.
        // Unfortunately the AssocQueryString returns everything together all-at-once.  Since it's _possible_ the
        // association does something clever with %1 or %L, it's necessary for us to walk the string and get the
        // filename out of it.
        //
        // Shortcut: use CommandLineToArgvW() and then re-quote the arguments.  It's a bit wasteful on cycles but
        // it's oh-so-easy and 100% consistent with CMD behavior.

        if (1) {
            int numArgs;
            auto* reparsed = ::CommandLineToArgvW(CmdLineBuffer.c_str(), &numArgs);
            if (!reparsed || numArgs <= 0) {
                log_error(L"ERROR- command line expansion failed.");
                log_error(L"File type associated as -> %s", strCmd.c_str());
            }
            std::wstring reargs;
            for (int n=1; n<numArgs; ++n) {
                if (!reargs.empty()) {
                    reargs += L" ";
                }
                reargs += escape_quotes(reparsed[n]);
            }
            return ShellExec(reparsed[0], reargs.c_str(), flags_in);
        }
    }
    return ShellExec(exe_fullname.c_str(), xStringJoin(L" ", cmdargs).c_str(), flags_in);
}

// returns a string description of compiler toolchain and version information
std::wstring GetToolchainDesc()
{
    // It's extremely unliekly this could be built by anything other than MSVC, but I had this
    // code lying around from other projects, so might as well...

    // intel compiler must always be first, since it evily masquerades itself as other
    // compilers by implicitly defining _MSC_VER and __GNUC__ and stuff.
#ifdef __INTEL_COMPILER
    static const int major      = __INTEL_COMPILER / 100;
    static const int minor      = __INTEL_COMPILER % 100;
    return xStringFormat(L"Intel C++ %d.%d", major, minor);
#elif defined(_MSC_VER)
    static const int major      = _MSC_VER / 100;
    static const int minor      = _MSC_VER % 100;
    return xStringFormat(L"MSVC %d.%d", major, minor);
#elif defined(__clang__)
#   if defined(__VERSION__)
        return _T(__VERSION__);
#   else
        static const char* const compiler = "GCC";
        static const int major      = __clang_major__;
        static const int minor      = __clang_minor__;
        return xStringFormat(L"clang %s", _T(__clang_version__));
#   endif
#elif defined(__GNUC__)
#   if defined(__VERSION__)
        return xFormatString(L"GCC %s", _T(__VERSION__));
#   else
        static const int major      = __GNUC__;
        static const int minor      = __GNUC_MINOR__;
        return xStringFormat(L"GCC  %d.%d", major, minor);
#   endif
#elif defined(__VERSION__)
#else
#   error Unknown compiler/toolchain - Please implement a version message!
    return L"-Unknown-";
#endif
}

int __cdecl wmain(int Argc, WCHAR* Argv[])
{
    Ev_ShellExecFlags shflags = { 0 };
    bool startComspec   = false;
    bool FlagsRead      = false;
    bool showHelp       = false;
    bool showVersion    = false;

    // Because CMD shell defers cli parsing to individual applications, there are two ways to process the command line:
    //   A. Parse the original command line ourselves and then feed the original string arguments into ShellExec
    //   B. Parse the command line argv[] and re-escape quotes characters
    //
    // One caveat with Type B is that the actual type of argv parsing performed depends on the version of Microsoft's
    // libc that the program is linked against, and is limited to it's parsing rules which don't support things like
    // unescaped string literals using single quotes.  This is probably OK since, as a windows native application,
    // the expectation is that it wouldn't handle single-quoting anyway.

    //auto cli = GetCommandLineW();

    int total_len = 0;
    std::wstring executable_fullpath;
    ArgContainer cmd_arguments;
    if (!IsWindowsVistaOrGreater())
    {
        fwprintf( stderr, L"ERROR- This tool requires Windows Vista/7 or newer.\n" );
        return EXIT_FAILURE;
    }

    for (int i=1; i<Argc; i++)
    {
        if (!FlagsRead) {
            // removed support for '/' switch parsing, to make it easier to support unix-stype path names.

            if (Argv[i][0] != L'-') { //&& Argv[i][0] != L'/') {
                FlagsRead = 1;
            }
        }
        if (!FlagsRead) {
            if (!Argv[i][1]) {
                log_error(L"Warning- Ignoring empty switch argument at argpos %d.\n", i);
                continue;
            }

            // double-dash for long-form switch names
            if (Argv[i][1] == L'-')
            {
                // double-dash with no other content means all remaining items on the CLI belong to the command to be executed.
                if (!Argv[i][2]) {
                    FlagsRead = 1;
                    continue;
                }
                auto switchName = &Argv[i][2];

                if (0) { }      // just for else if code alignment
                else if (wcscmp(switchName, L"help") == 0) {
                    showHelp = 1;
                }
                else if (wcscmp(switchName, L"wait") == 0) {
                    shflags.DoNotWaitForProc = 0;
                }
                else if (wcscmp(switchName, L"nowait") == 0) {
                    shflags.DoNotWaitForProc = 1;
                }
                else if (wcscmp(switchName, L"version") == 0) {
                    showVersion = 1;
                }
                else if (wcscmp(switchName, L"verbose") == 0) {
                    g_Verbose = 1;
                }
                else {
                    log_error(L"ERROR- Unrecognized Switch `%s`\n", Argv[i]);
                    if (!showHelp) {
                        return EXIT_FAILURE;
                    }
                }
                continue;
            }

            // short-form switch names.  Process as many as exist until nullchar.

            int cur = 1;
            while (Argv[i][cur]) {
                auto flag = Argv[i][cur];  ++cur;


                if (0) { }      // just for else if code alignment
                else if (flag == L'?' || flag == L'h') {
                    showHelp = 1;
                }
                else if (flag == L'K' || flag == L'k') {
                    if (startComspec && !shflags.ComspecRemains) {
                        log_error(L"Warning- Duplicate specification of `/K` after `/C`, previous switch is ignored.\n");
                    }
                    startComspec            = 1;
                    shflags.ComspecRemains  = 1;
                }
                else if ((flag == L'C' || flag == L'c')) {
                    if (startComspec && shflags.ComspecRemains) {
                        log_error(L"Warning- Duplicate specification of `/C` after `/K`, previous switch is ignored.\n");
                    }
                    startComspec            = 1;
                    shflags.ComspecRemains  = 0;
                }
                else {
                    log_error(L"ERROR- Unrecognized Flag `%c` in argument `%s`\n", flag, Argv[i]);
                    if (!showHelp) {
                        return EXIT_FAILURE;
                    }
                }
            }
        }
        else {
            FlagsRead = 1;
            if (executable_fullpath.empty() && !startComspec) {
                executable_fullpath = Argv[i];
            }
            else if (Argv[i] && Argv[i][0]) {
                auto escaped = escape_quotes(Argv[i]);
                total_len += int(escaped.length()) + 1;
                cmd_arguments.push_back(escaped);

                if (total_len >= xMaxEnviron) {
                    log_error(L"ERROR- Command Line too long\n" );
                    return EXIT_FAILURE;
                }
            }
        }
    }

    // TODO: Provide build number information?
    // TODO: Provide date and time of build
    //   (both need to be done via pre-build step and revision.h method)
    const WCHAR* version = L"0.1";

    if (showHelp)
    {
        log_console(
            L"Executes specified command/program with elevated security profile\n"
            L"Version %s (%s), toolchain %s\n",
            version, _T(__DATE__), GetToolchainDesc().c_str()
        );

        log_console(
            L"\n"
            L"Usage: eudo [switches] [--] [program] [args]\n"
            L" -? | --help    - Shows this help\n"
            L" --wait         - Waits until program terminates (default)\n"
            L" --nowait       - Runs the program and then immediately returns\n"
            L" --hide         - Hides the program from view; may not be honored by all programs\n"
            L"                  Hide is ignored when -k is specified.\n"
            L" --show         - Shows program/console window (default)\n"
            L" -k             - Invokes the specified command using CMD /K\n"
            L"                  An interactive CMD prompt will remain open.\n"
            L" -c             - Invokes the specified command using CMD /C\n"
            L"                  This does not offer any specific advantages over normal elevation and\n"
            L"                  is provided primarily for diagnostic purposes\n"
            L" --version      - Print app version to STDOUT and exit immediately.\n"
            L" --verbose      - Enables diagnostic logging.\n"
            L"\n"
            L" program        - The program to execute; required unless -c|-k is specified\n"
            L" args           - command line arguments passed through to the program (optional)\n"
            L"\n"
            L"Use `--` to forcibly stop options parsing and begin program and arguments parsing.\n"
            L"This should be used when the target executable filename begins with a dash or double dash.\n"
            L"\n"
            L"Use -k to open interactive command prompts such as a Visual Studio Tools Prompt.\n"
        );

        return EXIT_SUCCESS;
    }

    if (showVersion)
    {
        log_console(
            L"eudo %s (%s)\n",
            version, _T(__DATE__)
        );

        if (g_Verbose) {
            log_console(
                L"Build toolchain: %s\n",
                GetToolchainDesc().c_str()
            );
        }
        return EXIT_SUCCESS;
    }

    if (g_Verbose) {
        log_console(
            L"Application        = %s\n"
            L"App Arguments      = %s\n",
            startComspec ? L"cmd.exe" : executable_fullpath.c_str(),
            xStringJoin(L" ", cmd_arguments).c_str()
        );
    }

    if (1) {
        bool willHideWindow = shflags.HideWindow & !(startComspec && shflags.ComspecRemains);
        debug_log(
            L"startComspec       = %c\n"
            L"ComspecRemains     = %c\n"
            L"HideWindow         = %c\n"
            L"WaitForProcess     = %c\n",
            startComspec                ? L'Y' : L'N',
            shflags.ComspecRemains      ? L'Y' : L'N',
            willHideWindow              ? L'Y' : L'N',
            shflags.DoNotWaitForProc    ? L'N' : L'Y'
        );
    }


    if (startComspec) {
        return ExecComspec(cmd_arguments, shflags);
    }

    if (executable_fullpath.empty()) {
        fwprintf( stderr, L"ERROR- missing required target application path to elevate.\n" );
        fwprintf( stderr, L"Specify --help for command line usage information.\n" );
        return EXIT_FAILURE;
    }

    return ExecAssoc(executable_fullpath, cmd_arguments, shflags);
}


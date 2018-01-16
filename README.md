## eudo

Allows executing a specified command or executable process with elevated `Admin` rights, as defined by the Windows security model.

### Why not call it `sudo`?

Elevate does not provide the ability to run under different user account credentials, therefore it is not analogous to `sudo` on Linux systems.
I believe that feature could be added to the utility in the future, however there are some caveats to that process since technically Linux and
Windows fundamentally differ in their security models -- Linux does not have a concept of *elevation* of the current user.  If that caveat can
be resolved cleanly to allow for interoperable behavior between `bash/sh` scripts run on either Windows/Linux OS, then at that point this utility
could be re-released under the `sudo` moniker.  Ideally such a utility could then also be bundled with the likes of **MSYS/MinGW** or **Git for Windows**. 

### History

This project was inspired by the somewhat-widely used `Elevate.exe` utility by Johannes Passing (hosted on github at https://github.com/jpassing/elevate).
I had wanted to use it for a build script that I was working on and discovered that it had a limitation that prevented me from using it in the way I
wanted. Specifically I wanted the ability to invoke bash-style shell scripts which are available via Git for Windows, without having to manually perform
`ftype` and `assoc` resolution via some proxy batch command script.

In the process of adding this feature, I realized that large swaths of the program could be vastly improved.  So I set about to write my own, from
scratch, with the following new features:

* file/path names and arguments lists are no longer limited to 260/520 characters
* handles argument quotations correctly
* easy way to specify path names that began with a forward slash (`/`) or dash (`-`)
* ability to execute via `COMSPEC` and exit (analogous to `cmd /c` )
* support for unix-style cmdline switchesm eg `--help`
* performs full windows filename extension association resolution -- allowing one to open `.txt` files via the registered text editor, or `.sh` files via **Git Bash**.
* Allows handling return codes from elevated processes



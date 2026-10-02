ccxview -- a fast viewer for CalculiX results and models (.frd, .inp, .dat, .fbd)
https://github.com/BasemRajjoub/ccxview

Linux:   ccxview -- 64-bit, glibc 2.34 or newer (Ubuntu 22.04, Debian 12,
         Fedora 35, RHEL 9 and later). Double-click it or run it from a
         terminal. The lib/ folder holds the X11 and OpenGL dispatch libraries
         for minimal systems; keep it next to the binary. The GPU driver comes
         from your system; without one, ccxview restarts itself on Mesa's
         software renderer.
Windows: ccxview.exe -- 64-bit Windows 10/11, no installation, no DLLs.
         Without a GPU driver, put Mesa's opengl32.dll in a "mesa" folder
         beside it.
Browser: ccxview.html -- WebGL2, one file: open it from disk or put it on any
         web server. Open... and drag and drop take files from your machine;
         exports arrive as downloads. Live: https://basemrajjoub.github.io/ccxview/

If ccxview crashes it writes ccxview-crash.txt (in the folder it was started
from, else in the temp folder) and says where. Please attach that file to an
issue at https://github.com/BasemRajjoub/ccxview/issues.

licenses/ holds the licence of ccxview (GPL-2.0-or-later), of the fonts
compiled into it: Inter and Noto Sans Math (SIL Open Font License 1.1) and
Lucide (ISC), and of the libraries TinyExpr (zlib) and miniz (MIT).

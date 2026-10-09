@echo off
rem ccxview -- Windows build (MSVC). Run from a "Developer Command Prompt".
rem Output: build\ccxview.exe, a single file with no DLLs beyond what Windows ships.
rem No console window behind the viewer; "build.bat console" keeps one (debugging).
set SUBSYS=WINDOWS
if /i "%1"=="console" set SUBSYS=CONSOLE
if not exist build mkdir build
set /p VER=<VERSION
rc /nologo /fo build\ccxview.res res\ccxview.rc
if errorlevel 1 exit /b 1
set SRC=src\app.c src\app_field.c src\app_overlay.c src\app_tensor.c src\app_traj.c src\app_path.c src\app_linearize.c src\app_cam.c src\app_load.c src\app_settings.c src\app_gauss.c src\app_deck.c src\app_fail.c src\app_mesh.c src\app_shell.c src\app_loads.c src\app_fbd.c src\app_select.c src\app_seltools.c src\app_selfilter.c src\app_seluse.c src\app_label.c src\app_labelpos.c src\app_sidecar.c src\app_integ.c src\app_stl.c src\app_measure.c src\app_title.c src\render.c src\ui.c src\ui_style.c src\ui_panels.c src\ui_view.c src\ui_bars.c src\ui_windows.c src\ui_fail.c src\ui_mesh.c src\ui_info.c src\ui_deck.c src\ui_menu.c src\ui_label.c src\ui_stl.c src\ui_measure.c src\ui_title.c src\ui_plots.c src\ui_integ.c src\ui_rebar.c src\ui_select.c src\ui_test.c src\font_data.c src\gpu.c src\frd.c src\units.c src\failure.c src\quality.c src\integ.c src\shell.c src\rebar.c src\mesh.c src\field.c src\glyph.c src\traj.c src\calc.c src\os.c src\filedlg.c src\dat.c src\gauss.c src\inp.c src\inp_localsys.c src\inp_loads.c src\inp_steps.c src\cap.c src\label.c src\fbd.c src\cgx.c src\sta.c src\log.c src\cfg.c src\idlist.c src\selset.c src\seltopo.c src\selfilter.c src\export.c src\path.c src\stl.c src\measure.c src\video.c src\video_h264.c src\video_mp4.c vendor\tinyfiledialogs.c src\sokol_impl.c
cl /nologo /O2 /MT /W3 /DSOKOL_GLCORE /D_CRT_SECURE_NO_WARNINGS /DCV_VERSION_NUM=%VER% /Ivendor %SRC% ^
   /Fobuild\ /Fe:build\ccxview.exe ^
   /link build\ccxview.res /SUBSYSTEM:%SUBSYS% kernel32.lib user32.lib gdi32.lib shell32.lib opengl32.lib comdlg32.lib ole32.lib
if errorlevel 1 exit /b 1
echo built build\ccxview.exe

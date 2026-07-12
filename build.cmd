@echo off
setlocal
for %%I in ("%~dp0..\..") do set "OMNETPP_ROOT=%%~fI"
call "%OMNETPP_ROOT%\opp_shell.cmd" -no-start -c "cd \"$HOME/samples/marl_wsn\" && opp_makemake -f --deep -o marl_wsn && make -j4 MODE=release"
endlocal

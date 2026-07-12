@echo off
setlocal
for %%I in ("%~dp0..\..") do set "OMNETPP_ROOT=%%~fI"
call "%OMNETPP_ROOT%\opp_shell.cmd" -no-start -c "cd \"$HOME/samples/marl_wsn\" && ./marl_wsn.exe -u Cmdenv -c MARL && ./marl_wsn.exe -u Cmdenv -c CloudMARL && ./marl_wsn.exe -u Cmdenv -c SPMH && ./marl_wsn.exe -u Cmdenv -c LEACH && ./marl_wsn.exe -u Cmdenv -c Paper3D"
endlocal

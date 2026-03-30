Set WshShell = CreateObject("WScript.Shell") 
WshShell.Run "cmd /c ""py scripts\service.py > .backend.log 2>&1""", 0, False 
WshShell.Run "cmd /c ""server.exe > .server.log 2>&1""", 0, False 

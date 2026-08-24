Option Explicit

Dim objShell, objEnv
Dim strWatcom, strCurrentPath, strWatcomPath, strInclude, strEdpath

Set objShell = CreateObject("WScript.Shell")
' Using "User" environment variables avoids needing admin rights
Set objEnv = objShell.Environment("User")

' Define the base OpenWatcom directory
strWatcom = "C:\WATCOM"
objEnv("WATCOM") = strWatcom

' 1. Update PATH (BINNT contains the 32/64-bit Windows host tools like wcc, BINW contains standard tools)
strCurrentPath = objEnv("PATH")
strWatcomPath = strWatcom & "\BINNT;" & strWatcom & "\BINW"

' Check if Watcom is already in the path to avoid duplicate entries
If InStr(1, strCurrentPath, strWatcomPath, vbTextCompare) = 0 Then
    If strCurrentPath <> "" And Right(strCurrentPath, 1) <> ";" Then
        strCurrentPath = strCurrentPath & ";"
    End If
    ' Prepend Watcom paths to the beginning of the PATH variable
    objEnv("PATH") = strWatcomPath & ";" & strCurrentPath
End If

' 2. Update INCLUDE (Headers for standard C, Win16, and Win32)
strInclude = strWatcom & "\H;" & strWatcom & "\H\WIN;" & strWatcom & "\H\NT"
objEnv("INCLUDE") = strInclude

' 3. Update EDPATH (Required for Watcom's vi editor/tools)
strEdpath = strWatcom & "\EDDAT"
objEnv("EDPATH") = strEdpath

WScript.Echo "OpenWatcom environment variables configured successfully!" & vbCrLf & vbCrLf & "Please restart any open command prompts for the changes to take effect."
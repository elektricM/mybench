; Optional WinUSB driver step of the MYBENCH installer (packaging/CMakeLists.txt, only when
; MYBENCH_WDI_SIMPLE is set). libwdi's wdi-simple generates a signed INF for the inbox WinUSB
; driver and binds it to the NI myDAQ (USB 3923:755b). This replaces NI's driver for the device:
; NI ELVISmx / DAQmx will no longer see it until the driver is rolled back (docs/INSTALL.md).
; Default answer: No; skipped in silent installs (/S).
IfSilent mybench_driver_done
MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Install the WinUSB driver for the NI myDAQ now?$\r$\n$\r$\nMYBENCH talks to the myDAQ through WinUSB. This replaces NI's driver for the device: NI ELVISmx and NI-DAQmx will not see the myDAQ until you roll the driver back (see the installation guide).$\r$\n$\r$\nPlug the myDAQ in before answering Yes." IDNO mybench_driver_done
  DetailPrint "Installing the WinUSB driver for the NI myDAQ (3923:755b)..."
  nsExec::ExecToLog '"$INSTDIR\driver\wdi-simple.exe" --vid 0x3923 --pid 0x755B --type 0 --name "NI myDAQ (WinUSB, MYBENCH)" --dest "$TEMP\mybench-mydaq-driver"'
  Pop $0
  StrCmp $0 "0" mybench_driver_done
    MessageBox MB_OK|MB_ICONEXCLAMATION "The driver installation returned $0. You can bind WinUSB to the myDAQ later with Zadig (see the installation guide)."
mybench_driver_done:

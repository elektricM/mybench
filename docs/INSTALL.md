# Installing MYBENCH

MYBENCH needs no NI software. It talks to the NI myDAQ over USB through libusb, which is
bundled with every package. What differs per OS is how the program gets permission to use the
USB device:

- **macOS**: nothing to do.
- **Linux**: a udev rule, which the `.deb`/`.rpm` install for you.
- **Windows**: the WinUSB driver must be bound to the myDAQ in place of NI's driver.

Without a myDAQ, MYBENCH runs with its built-in simulated device.

The myDAQ must run NI's firmware. It then appears as USB device `3923:755b`. A myDAQ that has
been used once with NI ELVISmx or NI-DAQmx already has it, because NI's driver updates the
firmware on first use.

## macOS

1. Open `MYBENCH-<version>-macOS-<arch>.dmg` and drag **MYBENCH** to **Applications**.
2. The release builds are signed ad hoc, not with an Apple Developer ID, so Gatekeeper blocks
   the first launch. Unblock it once with either method:
   - Control-click MYBENCH in Applications, choose **Open**, then **Open** again.
   - Or run `xattr -dr com.apple.quarantine /Applications/MYBENCH.app`.
3. Plug in the myDAQ. It appears in the device menu at the top left. macOS needs no driver and
   no permission prompt.

Requirements: the DMG's minimum macOS version is in the app's Info.plist
(`LSMinimumSystemVersion`). For release builds it is macOS 14 or later, universal
(Apple Silicon and Intel).

Uninstall:
1. Delete `/Applications/MYBENCH.app`.
2. To remove the settings too, delete `~/Library/Preferences/com.mybench.MYBENCH.plist`.

## Linux

### Debian / Ubuntu (.deb) and Fedora (.rpm)

The distribution packages use your distribution's Qt, which must be 6.9 or newer. That means
Ubuntu 25.10 or 26.04+, Debian 14 (forky) or testing, or Fedora 42+.

```sh
sudo apt install ./mybench_<version>_amd64.deb       # Debian / Ubuntu
sudo dnf install ./mybench-<version>-1.x86_64.rpm    # Fedora
```

The package installs:
- `/usr/bin/mybench`
- the desktop entry and AppStream metadata
- the icons
- the `.mybench` session file type
- the udev rule `/usr/lib/udev/rules.d/60-mybench-mydaq.rules`

Installing the package reloads udev. **Re-plug the myDAQ** once afterwards.

### AppImage (any distribution with glibc ≥ 2.35)

```sh
chmod +x MYBENCH-<version>-linux-x86_64.AppImage
./MYBENCH-<version>-linux-x86_64.AppImage
```

An AppImage cannot install system files, so install the udev rule once by hand (see below).
Integration with the desktop menu is up to your AppImage launcher, such as Gear Lever or
AppImageLauncher.

### USB permission (udev rule)

Without the rule, only root can open the myDAQ. MYBENCH then reports the device as busy or
inaccessible.

The rule is in the repository at `packaging/linux/60-mybench-mydaq.rules`. The AppImage also
carries it at `usr/lib/udev/rules.d/`, which you can see with `--appimage-extract`. The rule
itself is one line:

```
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="3923", ATTR{idProduct}=="755b", MODE="0660", GROUP="plugdev", TAG+="uaccess"
```

Install it and reload udev:

```sh
sudo cp 60-mybench-mydaq.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Then re-plug the myDAQ.

The rule grants access in two ways:
- `TAG+="uaccess"` gives the user logged in at the seat access, through systemd-logind.
- `GROUP="plugdev"` covers setups without logind. For this path, add yourself to the group
  with `sudo usermod -aG plugdev $USER`, then log in again.

**Fedora (KDE Plasma, GNOME, Wayland or X11)** has no `plugdev` group, and none is needed: udev
ignores the unknown group (a harmless "Unknown group 'plugdev'" line may appear in
`journalctl -b -u systemd-udevd`) and `uaccess` gives your desktop session access. Do not add
yourself to any group. This works for a local, logged-in graphical session. Over plain SSH there
is no seat, so no ACL: use `sudo`.

To check that the rule applies:

```sh
lsusb -d 3923:755b                  # the myDAQ is listed
getfacl /dev/bus/usb/BBB/DDD        # BBB/DDD from lsusb: your user has rw
```

No kernel driver claims the myDAQ on Linux. It is a vendor-class device.

## Windows

1. Run `MYBENCH-<version>-windows-x64.exe`.
2. The installer adds MYBENCH to the Start menu and the desktop, with an uninstaller in
   *Apps & features*.
3. Windows 10 and 11 (x64) are supported.
4. Before MYBENCH can use a real myDAQ, bind the WinUSB driver to it, as described below.

### Windows USB driver

libusb, which MYBENCH's driver uses, can only open a USB device that uses a generic driver
such as **WinUSB**. After NI's software has been installed, the myDAQ (USB `3923:755b`) is
bound to NI's own kernel driver instead. **Only one driver can own the device.**

- With WinUSB, MYBENCH works. NI ELVISmx and NI-DAQmx no longer see the myDAQ.
- With NI's driver, NI's software works, and MYBENCH shows only the simulated device.

The change applies only to this one USB device. You can switch back at any time (see
"Going back to NI's driver" below).

**With Zadig (recommended)**

1. Download Zadig from <https://zadig.akeo.ie> (free, open source, from the libusb/libwdi
   authors).
2. Plug in the myDAQ and close NI software (NI MAX, ELVISmx).
3. Start Zadig and choose **Options → List All Devices**.
4. Select **NI myDAQ**. Check that the USB ID shows `3923` `755B`.
5. Choose **WinUSB** as the target driver and click **Replace Driver** (or **Install
   Driver**).
6. Unplug and re-plug the myDAQ, then start MYBENCH.

**With the installer (optional)**

Installers built with `-DMYBENCH_WDI_SIMPLE=<path to wdi-simple.exe>` ask at the end of
setup whether to do the same binding. The default answer is No, and the question is skipped
in silent installs. This uses libwdi's `wdi-simple` example tool, which does what Zadig does
for one device:

```
wdi-simple.exe --vid 0x3923 --pid 0x755B --type 0 --name "NI myDAQ (WinUSB, MYBENCH)"
```

The CI release installer does not bundle it yet: libwdi has to be built from source together
with the WDK redistributables.

**Going back to NI's driver**

1. In Device Manager, open the myDAQ, which is now under *Universal Serial Bus devices*.
2. Choose **Uninstall device** and tick **Attempt to remove the driver for this device**.
3. Re-plug the myDAQ. Windows picks NI's driver again.
4. If Windows does not, choose **Update driver → Browse → Let me pick** and select NI's
   driver for the myDAQ.

**Future**: a DAQmx backend could let MYBENCH use NI's installed driver through the NI-DAQmx
C API instead of libusb, with no driver swap. It would sit next to the libusb backend behind
the same C++ `mydaq::Device` interface (`core/`). This is not implemented; NI-DAQmx is
closed-source and Windows/Linux only.

## Checking the installation

```sh
mybench --version
mybench --simulated                          # no USB at all
```

On macOS the binary is `/Applications/MYBENCH.app/Contents/MacOS/MYBENCH`. On Windows it is
`"C:\Program Files\MYBENCH\bin\mybench.exe"`.

To open the event log, click the status bar or press Ctrl/⌘ K and choose *Log*. It shows the
device list and any USB error. `busy` means that another program has the device, or on
Linux, that the udev rule is missing. `not found` means that no `3923:755b` device is
attached.

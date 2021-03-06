DESCRIPTION

This project ports the Windows application Emu71 written in C to Android.
It uses the Android NDK. The former Emu71 source code (written by Christoph Giesselink) remains untouched because of a thin win32 emulation layer above Linux/NDK!
This win32 layer will allow to easily update from the original Emu71 source code.
It can open or save the exact same state files (state.e??) than the original Windows application!

This application does NOT come with the ROM files!
You will need KML scripts and ROM files already copied into your Android filesystem.
You can download the KML scripts here: http://regis.cosnier.free.fr/soft/androidEmu71/Emu71-KML-original.zip
Or you can download the KML scripts from the original Emu71 Windows application archive (https://hp.giesselink.com/emu71.htm)
and you can extract the ROM file from a real calculator (or be lucky on internet).
Be careful about the case sensitivity of the filename in the KML script (Linux is case sensitive, not Windows).

The application does not request any permission (because it opens the files or the KML folders using the content:// scheme).

The application is distributed with the same license under GPL and you can find the source code here:
https://github.com/dgis/emu71android


QUICK START

1. From the left side, slide your finger to open the menu.
2. Touch the "New..." menu item.
3. "Select a Custom KML script folder..." where you have copied the KML scripts and ROM files.
4. Pick a calculator.
5. And the calculator should now be opened.


NOTES

- For technical reason, this application need the Android 5.0 (API 21).
- The Help menu displays Emu71's original help HTML page and may not accurately reflect the behavior of this Android version.
- When using a custom KML script by selecting a folder, you must take care of the case sensitivity of its dependency files.
- This Emulator does not include the ROM files or the KML files.
- The port configuration is very similar to Emu71 for Windows


NOT WORKING YET

- Disassembler
- Debugger
- HP-IL


CHANGES

Version 1.1 (2020-11-XX)

- Update from the original source code Emu71 for Windows version 1.12 from Christoph Gießelink.
- Intercept the ESC keyboard key to allow the use of the BACK soft key.
- Add LCD pixel borders.
- Add support for the dark theme.
- Remove the non loadable file from the MRU file list.
- Fix: Overlapping window source position when Background/Offset is not (0,0).
- Wrap the table of content in the former help documentation.
- Save the settings at the end of the state file.
- Transform all child activities with dialog fragments (to prevent unwanted state save).
- Fix an issue with the numpad keys which send the arrow keys and the numbers at the same time.
- If the KML folder does not exist (like the first time), prompt the user to choose a new KML folder.
- Move the KML folder in the JSON settings embedded in the state file because Windows cannot open the state file with KML url longer than 256 byte.
- Prevent to auto save before launching the "Open...", "Save As...", "Load Object...", "Save Object...", etc...
- Prevent app not responding (ANR) in NativeLib.buttonUp().
- In the menu header, switch the pixel format RGB to BGR when an icon of type BMP is defined in the KML script.
- Replaces the haptic feedback switch with a slider to adjust the vibration duration.
- Fix transparency issue (RGB -> BGR).
- Fix a printer issue from Christoph Gießelink's HP82240B Printer Simulator version 1.12.
- Fix the KML button Type 3 with a Background offset which was not display at the right location (Fix #15). But Type 3 does not work very well with Emu42.
- Fix a timer issue.


Version 1.0 (2019-12-12)

- First public version available. It is based on Emu71 for Windows version 1.10 from Christoph Gießelink.


LICENSES

Android version by Régis COSNIER.
This program is based on Emu71 for Windows version, copyrighted by Christoph Gießelink & Sébastien Carlier, with the addition of a win32 layer to run on Android.

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.
This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.


TODO

- Make the HPIL socket works.


BUILD

Emu71 for Android is built with Android Studio 3.4 (2019).
And to generate an installable APK file with a real Android device, it MUST be signed.

Either use Android Studio:
* In menu "Build"
* Select "Generate Signed Bundle / APK..."
* Select "APK", then "Next"
* "Create new..." (or use an existing key store file)
* Enter "Key store password", "Key alias" and "Key password", then "Next"
* Select a "Destination folder:"
* Select the "Build Variants:" "release"
* Select the "Signature Versions:" "V1" (V1 only)
* Finish

Or in the command line, build the signed APK:
* In the root folder, create a keystore.jks file with:
** keytool -genkey -keystore ./keystore.jks -keyalg RSA -validity 9125 -alias key0
** (keytool -genkeypair -v -keystore ./keystore.jks -keyalg RSA -validity 9125 -alias key0)
* create the file ./keystore.properties , with the following properties:
    storeFile=../keystore.jks
    storePassword=myPassword
    keyAlias=key0
    keyPassword=myPassword
* gradlew build
* The APK should be in the folder app/build/outputs/apk/release

Then, you should be able to use this fresh APK file with an Android device.

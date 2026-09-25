set /p version="Enter version: "

..\7z a -r -tzip ..\Emu71ForAndroid-%date:~6,4%-%date:~0,2%-%date:~3,2%-%version%-Source.zip * -x!*.iml -x!.*\ -x!build\ -x!captures\ -x!local.properties -x!app\build -x!app\release -x!make-archives.bat
..\7z a -tzip ..\Emu71ForAndroid-%date:~6,4%-%date:~0,2%-%date:~3,2%-%version%-Binary.zip COPYING.TXT ReadMe.txt

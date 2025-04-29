# Support

* If you like my work, you can support me through https://ko-fi.com/DualTachyon

# DMR monitor for the RT-4D

DigiMonitoR is a simple Windows application that allows capturing the DMR logs produced by the Radtel RT-4D.
The application is a simple proof of concept and will largely be unsupported and unmaintained. It is not meant to be fast, reliable or bug free.
The protocol decoding is largely incomplete, but enough was implemented to show what is possible with the RT-4D.

# How to build

Load the .sln file into Visual Studio 2022 Community Edition and build.

Alternatively, you can use MSBuild:
```
msbuild DigiMonitoR.sln -p:Configuration=Release
```

# How to use

* Launch the executable with -l to list available COM ports.
* Launch the executable with -p COMx to monitor your RT-4D.
* While the transceiver is off, hold the SIDE 1 button and turn it on.
* Release the button after 1-2 seconds.
* Enjoy the view

# Restrictions

Due to the nature of the Kenwood port, you cannot hear any audio or transmit speech without an AIO cable.
How to get such a cable is beyond the scope of this project.

# License

Copyright 2025 Dual Tachyon
https://github.com/DualTachyon

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.


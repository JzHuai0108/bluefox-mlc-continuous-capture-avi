# Use mvBluefox mlc202dg to acquire continuous video streams on windows 10

## 1. Install mvIMPACT Acquire Driver
The instructions in this section generally follows [Sec 1.7.1 in the mvBlueFox technical manual](https://www.matrix-vision.com/USB2.0-single-board-camera-mvbluefox-mlc.html?file=files/mv11/support/Manuals/mvBlueFOX_technical_manual.pdf).

Just note that we need to download the [x64 msi](https://www.matrix-vision.com/USB2.0-single-board-camera-mvbluefox-mlc.html?file=files/mv11/support/mvIMPACT_Acquire/01/mvBlueFOX-x86_64-2.40.1.msi) from [mvBluefox mlc page](https://www.matrix-vision.com/USB2.0-single-board-camera-mvbluefox-mlc.html).

## 2. Test that the driver works

Open the application wxPropView which is just installed, and try to capture a few images.
See also Section 1.11 of the mvBluefox technical manual.

## 3. Build the visual studio project SequenceCapture which captures images into memory and then save to an AVI file.

You may build it in the latest visual C++ IDE.
Then run the program with
```
SequenceCapture of=myfile.avi frameRate=25 et=5000 pc=40 bf=1500
```
Depending on the exposure time and pixel clock, the tenable frame rate may fall below 25 FPS.
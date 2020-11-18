#include <conio.h>
#include <deque>
#include <fstream>
#include <iostream>
#include <apps/Common/aviwrapper.h>
#include <apps/Common/exampleHelper.h>
#include <mvDisplay/Include/mvIMPACT_acquire_display.h>
#include <mvIMPACT_CPP/mvIMPACT_acquire_helper.h>

using namespace std;
using namespace mvIMPACT::acquire;
using namespace mvIMPACT::acquire::display;

using ImageQueue = deque<ImageBufferDesc>;

struct FrameInfo {
    int requestNumber;
    int64_t infoTimeStamp_us;
    int infoExposeStart_us;
    int infoExposeTime_us;
    int64_t infoFrameNr;

	FrameInfo(int _requestNumber,
		int64_t _infoTimeStamp_us,
		int _infoExposeStart_us,
		int _infoExposeTime_us,
		int64_t _infoFrameNr) :
		requestNumber(_requestNumber),
		infoTimeStamp_us(_infoTimeStamp_us),
		infoExposeStart_us(_infoExposeStart_us),
		infoExposeTime_us(_infoExposeTime_us),
		infoFrameNr(_infoFrameNr) {
	}
};

//-----------------------------------------------------------------------------
class ThreadParameter
//-----------------------------------------------------------------------------
{
    Device*                 pDev_;
    unsigned int            requestsCaptured_;
    Statistics              statistics_;
    ImageDisplayWindow      displayWindow_;
    ImageQueue&             imageQueue_;
    ImageQueue::size_type   maxQueueSize_;
    int                     frameRate_;
    deque<FrameInfo>&       frameInfoQueue_;
    int                     cullStartFrames_;

public:
    ThreadParameter( Device* p, ImageQueue& q, ImageQueue::size_type maxSize, int fr,
        deque<FrameInfo>& frameInfoList, int cullStartFrames)
        : pDev_( p ), requestsCaptured_( 0 ), statistics_( p ), 
        displayWindow_( "mvIMPACT_acquire sample, Device " + p->serial.read() ), 
        imageQueue_( q ), maxQueueSize_( maxSize ), frameRate_( fr ),
        frameInfoQueue_(frameInfoList), cullStartFrames_(cullStartFrames) {}

    ThreadParameter( const ThreadParameter& src ) = delete;
    ThreadParameter& operator=( const ThreadParameter& rhs ) = delete;

    void incRequestsCaptured( void )
    {
        ++requestsCaptured_;
    }
    unsigned int getRequestsCaptured( void ) const
    {
        return requestsCaptured_;
    }
    const Statistics& getStatistics( void ) const
    {
        return statistics_;
    }
    int getFrameRate( void ) const
    {
        return frameRate_;
    }
    void setFrameRate( int fr )
    {
        frameRate_ = fr;
    }
    Device* getDevice( void ) const
    {
        return pDev_;
    }
    ImageDisplayWindow& getDisplayWindow( void )
    {
        return displayWindow_;
    }
    ImageQueue& getImageQueue( void ) const
    {
        return imageQueue_;
    }
    deque<FrameInfo>& getFrameInfoList(void) const {
        return frameInfoQueue_;
    }
    ImageQueue::size_type getMaxQueueSize( void ) const
    {
        return maxQueueSize_;
    }
    int cullStartFrames(void) const
    {
        return cullStartFrames_;
    }
    int decreaseCullStartFrames(void) {
        return --cullStartFrames_;
    }
};

//-----------------------------------------------------------------------------
void displayCommandLineOptions(void)
//-----------------------------------------------------------------------------
{
    cout << "Available parameters:" << endl
        << "  'outputFile' or 'of' to specify the name of the resulting AVI file" << endl
        << "  'frameRate' or 'fr' to specify the frame rate(frames per second for playback) of the resulting AVI file" << endl
        << "  'exposureTime' or 'et' to specify the exposure time in us which is 5000us by default." << endl
        << "  'pixelClock' or 'pc' to specify the pixel clock in MHz which is 40 MHz by default." << endl
        << "  'bufferFrames' or 'bf' to specify maximum number of buffered frames in memory which is 1500 by default." << endl
        << "  'replay' or 'rp' to specify whether to replay after the capture which is 0 by default." << endl
        << "  'cullStartFrames' or 'cs' to specify the number of the beginning"
        << " frames to discard which is 7 by default." << endl
        << "USAGE EXAMPLE:" << endl
        << "  SequenceCapture of=myfile.avi frameRate=25 et=5000 pc=40 bf=1500" << endl << endl;
}

//-----------------------------------------------------------------------------
void inplaceHorizontalMirror( const ImageBuffer* p )
//-----------------------------------------------------------------------------
{
    int upperHalfOfLines = p->iHeight / 2; // the line in the middle (if existent) doesn't need to be processed!
    size_t pitch = p->pChannels[0].iLinePitch; // this only works for image formats where each channel has the same line pitch!
    char* pLowerLine = static_cast<char*>( p->vpData ) + ( ( p->iHeight - 1 ) * pitch );
    char* pUpperLine = static_cast<char*>( p->vpData );
    unique_ptr<char[]> pTmpLine( new char[pitch] );

    for( int y = 0; y < upperHalfOfLines; y++ )
    {
        memcpy( pTmpLine.get(), pUpperLine, pitch );
        memcpy( pUpperLine, pLowerLine, pitch );
        memcpy( pLowerLine, pTmpLine.get(), pitch );
        pUpperLine += pitch;
        pLowerLine -= pitch;
    }
}

//-----------------------------------------------------------------------------
// Currently only the mvBlueFOX supports HRTC and thus the definition of an
// absolute frame rate during the capture process.
void setupBlueFOXFrameRate(Device* pDev, int frameRate_Hz, unsigned int exposureTimeUs, unsigned int pixelClockMHz)
//-----------------------------------------------------------------------------
{
 /*   cout << "To use the HRTC to configure the mvBlueFOX to capture with a defined frequency press 'y'." << endl;
    if (_getch() != 'y')
    {
        return;
    }*/
    // mvBlueFOX devices can define a fixed frame frequency
    cout << "Trying to capture at " << frameRate_Hz << " frames per second. Please make sure the device can deliver this frame rate" << endl
        << "as otherwise the resulting AVI stream will be replayed with an incorrect speed" << endl;

    int frametime_us = static_cast<int>(1000000.0 * (1.0 / static_cast<double>(frameRate_Hz)));
    const int TRIGGER_PULSE_WIDTH_us = 100;
    if (frametime_us < 2 * TRIGGER_PULSE_WIDTH_us)
    {
        cout << "frame rate too high (" << frameRate_Hz << "). Using 10 Hz." << endl;
        frametime_us = 100000;
    }

    CameraSettingsBlueFOX bfs(pDev);

    bfs.autoExposeControl.write(aecOff);
    bfs.expose_us.write(exposureTimeUs); // adjust exposure time according to user input.
    //if( bfs.expose_us.read() > frametime_us / 2 )
    //{
    //    ostringstream oss;
    //    oss << "Reducing frame-time from " << bfs.expose_us.read() << " us to " << frametime_us / 2 << " us." << endl
    //        << "Higher values are possible but require a more sophisticated HRTC program" << endl;
    //    bfs.expose_us.write( frametime_us / 2 );
    //}
    mvIMPACT::acquire::TCameraPixelClock clockFrequencyMHz =
        mvIMPACT::acquire::TCameraPixelClock::cpc40000KHz;
    switch (pixelClockMHz) {
    case 12:
        clockFrequencyMHz =
            mvIMPACT::acquire::TCameraPixelClock::cpc12000KHz;
        break;
    case 20:
        clockFrequencyMHz =
            mvIMPACT::acquire::TCameraPixelClock::cpc20000KHz;
        break;
    case 32:
        clockFrequencyMHz =
            mvIMPACT::acquire::TCameraPixelClock::cpc32000KHz;
        break;
    case 40:
        clockFrequencyMHz =
            mvIMPACT::acquire::TCameraPixelClock::cpc40000KHz;
        break;
    default:
        break;
    }
    bfs.pixelClock_KHz.write(clockFrequencyMHz);
    cout << "Current pixel clock kHz selection: " << bfs.pixelClock_KHz.readS()
        << " and the line delay clk: " << bfs.lineDelay_clk.read() << endl;

    IOSubSystemBlueFOX bfIOs(pDev);
    // define a HRTC program that results in a define image frequency
    // the hardware real time controller shall be used to trigger an image
    bfs.triggerSource.write(ctsRTCtrl);
    // when the hardware real time controller switches the trigger signal to
    // high the exposure of the image shall start
    string product = pDev->product.read();
    if (product.find("IGC202dG") != std::string::npos) {
        bfs.triggerMode.write(ctmContinuous);
        cerr << "The mode ctmOnRisingEdge caused writing exception with "
            "mvBlueFox MLC202DG, so ctmContinuous is used." << endl;
    }
    else {
        bfs.triggerMode.write(ctmOnRisingEdge);
    }
    // In release mode with compression, the maximum FPS of 
    // ContinuousCaptureToAVIFile is a bit more than 10.
    // In comparison, SequenceCapture project uses a RequestProvider
    // instead of FunctionInterface for generating requests.
    // RequestProvider uses FunctionInterface internally.

    // error checks
    if (bfIOs.RTCtrProgramCount() == 0)
    {
        // no HRTC controllers available (this never happens for the mvBlueFOX)
        cout << "This device (" << pDev->product.read() << ") doesn't support HRTC" << endl;
        return;
    }

    RTCtrProgram* pRTCtrlprogram = bfIOs.getRTCtrProgram(0);
    if (!pRTCtrlprogram)
    {
        // this only should happen if the system is short of memory
        cout << "Error! No valid program. Short of memory?" << endl;
        return;
    }

    // start of the program
    // we need 5 steps for the program
    pRTCtrlprogram->setProgramSize(5);

    // wait a certain amount of time to achieve the desired frequency
    int progStep = 0;
    RTCtrProgramStep* pRTCtrlStep = pRTCtrlprogram->programStep(progStep++);
    pRTCtrlStep->opCode.write(rtctrlProgWaitClocks);
    pRTCtrlStep->clocks_us.write(frametime_us - TRIGGER_PULSE_WIDTH_us);

    // trigger an image
    pRTCtrlStep = pRTCtrlprogram->programStep(progStep++);
    pRTCtrlStep->opCode.write(rtctrlProgTriggerSet);

    // high time for the trigger signal (should not be smaller than 100 us)
    pRTCtrlStep = pRTCtrlprogram->programStep(progStep++);
    pRTCtrlStep->opCode.write(rtctrlProgWaitClocks);
    pRTCtrlStep->clocks_us.write(TRIGGER_PULSE_WIDTH_us);

    // end trigger signal
    pRTCtrlStep = pRTCtrlprogram->programStep(progStep++);
    pRTCtrlStep->opCode.write(rtctrlProgTriggerReset);

    // restart the program
    pRTCtrlStep = pRTCtrlprogram->programStep(progStep++);
    pRTCtrlStep->opCode.write(rtctrlProgJumpLoc);
    pRTCtrlStep->address.write(0);

    // start the program
    pRTCtrlprogram->mode.write(rtctrlModeRun);

    // Now this camera will deliver images at exactly the desired frequency
    // when it is constantly feed with image requests and the camera can deliver
    // images at this frequency.
}

//-----------------------------------------------------------------------------
void myThreadCallback(shared_ptr<Request> pRequest, ThreadParameter& threadParameter)
//-----------------------------------------------------------------------------
{
    threadParameter.incRequestsCaptured();
    // display some statistical information every 100th image
    if (threadParameter.getRequestsCaptured() % 100 == 0)
    {
        const Statistics& s = threadParameter.getStatistics();
        cout << "Info from " << threadParameter.getDevice()->serial.read()
            << ": " << s.framesPerSecond.name() << ": " << s.framesPerSecond.readS()
            << ", " << s.errorCount.name() << ": " << s.errorCount.readS()
            << ", " << s.captureTime_s.name() << ": " << s.captureTime_s.readS() << endl;
    }
    if (pRequest->isOK())
    {
        if (threadParameter.cullStartFrames() > 0) {
            threadParameter.decreaseCullStartFrames();
            return;
        }
        threadParameter.getDisplayWindow().GetImageDisplay().SetImage( pRequest );
        threadParameter.getDisplayWindow().GetImageDisplay().Update();
        // append a new image at the end of the queue (the image will be deep-copied)
        threadParameter.getImageQueue().push_back( pRequest->getImageBufferDesc().clone() );
		threadParameter.getFrameInfoList().emplace_back(pRequest->getNumber(), 
            pRequest->infoTimeStamp_us.read(), pRequest->infoExposeStart_us.read(), 
            pRequest->infoExposeTime_us.read(), pRequest->infoFrameNr.read());

        // if the queue has the user defined max. number of entries remove the oldest one
        // with this method we always keep the most recent images
        if( threadParameter.getImageQueue().size() > threadParameter.getMaxQueueSize() )
        {
            threadParameter.getImageQueue().pop_front();
            threadParameter.getFrameInfoList().pop_front();
        }
    }
    else
    {
        cout << "Error: " << pRequest->requestResult.readS() << endl;
    }
}

void chooseAviPixelFormat(Device* pDev) {
    // display available destination formats
    ImageDestination id(pDev);
    vector<pair<string, TImageDestinationPixelFormat> > vAvailableDestinationFormats;
    id.pixelFormat.getTranslationDict(vAvailableDestinationFormats);
    cout << "Available destination formats: " << endl;
    for (const auto& format : vAvailableDestinationFormats)
    {
        cout << "[" << format.first << "]: " << format.second << endl;
    }
    cout << endl << endl;
    cout << "If AVI files shall be written please note, that most AVI compression handlers" << endl
        << "accept RGB888Packed formats only. Apart from that planar formats are not supported" << endl
        << "by this sample in order to keep things simple." << endl << endl
        << "Destination format (as integer): ";
    int destinationPixelFormat = { 0 };
    cin >> destinationPixelFormat;

    // set destination format
    try
    {
        id.pixelFormat.write(static_cast<TImageDestinationPixelFormat>(destinationPixelFormat));
    }
    catch (const ImpactAcquireException& e)
    {
        cout << "Failed to set destination pixel format(" << e.getErrorCodeAsString() << "), using default" << endl;
        id.pixelFormat.write(idpfRGB888Packed);
    }
    cout << "Using " << id.pixelFormat.readS() << "." << endl;
}

void defaultAviPixelFormat(Device* pDev) {
    ImageDestination id(pDev);
    id.pixelFormat.write(idpfRGB888Packed);    
    cout << "Using " << id.pixelFormat.readS() << "." << endl;
}

//-----------------------------------------------------------------------------
int main(int argc, char* argv[])
//-----------------------------------------------------------------------------
{
    DeviceManager devMgr;
    Device* pDev = getDeviceFromUserInput( devMgr );
    if( pDev == nullptr )
    {
        cout << "Could not obtain a valid pointer to a device. Unable to continue! Press any key to end the program." << endl;
        return _getch();
    }


    // default parameters
    string fileName(".\\output.avi");
    string infoFilename(".\\output.txt");
    unsigned int frameRate = 25u;
    unsigned int exposureTimeUs = 5000u;
    unsigned int pixelClockMHz = 40u;
    unsigned int maxAviFrames = 1150u; // AVI file max size = 4GB = 4 * 1024^3 > maxAviFrames * 3 * 1280 * 960.
    unsigned int bufferFrames = maxAviFrames;
    int cullStartFrames = 7;
    unsigned int replay = 0u;
    bool boInvalidCommandLineParameterDetected = false;
    // scan command line
    if (argc > 1)
    {
        for (int i = 1; i < argc; i++)
        {
            string param(argv[i]), key, value;
            string::size_type keyEnd = param.find_first_of("=");
            if ((keyEnd == string::npos) || (keyEnd == param.length() - 1))
            {
                cout << "Invalid command line parameter: '" << param << "' (ignored)." << endl;
                boInvalidCommandLineParameterDetected = true;
            }
            else
            {
                key = param.substr(0, keyEnd);
                value = param.substr(keyEnd + 1);
                if ((key == "outputFile") || (key == "of"))
                {
                    fileName = value;
                }
                else if ((key == "frameRate") || (key == "fr"))
                {
                    frameRate = static_cast<unsigned int>(atoi(value.c_str()));
                }
                else if ((key == "exposureTime") || (key == "et"))
                {
                    exposureTimeUs = static_cast<unsigned int>(atoi(value.c_str()));
                }
                else if ((key == "pixelClock") || (key == "pc"))
                {
                    pixelClockMHz = static_cast<unsigned int>(atoi(value.c_str()));
                }
                else if ((key == "bufferFrames") || (key == "bf"))
                {
                    bufferFrames = static_cast<unsigned int>(atoi(value.c_str()));
                    if (bufferFrames > maxAviFrames) {
                        cerr << "One avi file keeps at most " << maxAviFrames << " 3-channel RGB frames!\n";
                        bufferFrames = maxAviFrames;
                    }
                }
                else if ((key == "cullStartFrames") || (key == "cs"))
                {
                    cullStartFrames = atoi(value.c_str());
                }
                else if ((key == "replay") || (key == "rp"))
                {
                    replay = static_cast<unsigned int>(atoi(value.c_str()));
                }
                else
                {
                    cout << "Invalid command line parameter: '" << param << "' (ignored)." << endl;
                    boInvalidCommandLineParameterDetected = true;
                }
            }
        }
        if (boInvalidCommandLineParameterDetected)
        {
            displayCommandLineOptions();
        }
    }
    else
    {
        cout << "No command line parameters specified." << endl;
        displayCommandLineOptions();
    }

    cout << "Initialising the device. This might take some time..." << endl
         << endl
         << "PLEASE NOTE THAT THIS EXAMPLE APPLICATION MAKES USE OF A VERY OLD, OUTDATED WINDOWS ONLY API WHICH IS NOT RECOMMENDED FOR NEW PROJECTS!" << endl
         << "There are various other, more portable ways to encode/store a video stream there day. Please consider using the FFmpeg library (see" << endl
         << "'ContinuousCaptureFFmpeg' in the C++ manual) or something similar instead!" << endl;
    try
    {
        pDev->open();
    }
    catch( const ImpactAcquireException& e )
    {
        // this e.g. might happen if the same device is already opened in another process...
        cout << "An error occurred while opening the device " << pDev->serial.read()
             << "(error code: " << e.getErrorCodeAsString() << ").";
        return 1;
    }

    if( pDev->family.read() == "mvBlueFOX" )
    {
        setupBlueFOXFrameRate( pDev, frameRate, exposureTimeUs, pixelClockMHz );
    }

    ImageQueue::size_type maxQueueSize = bufferFrames;
    cout << "The length of the sequence to buffer (please note that this might "
        << "be limited by your systems memory): " << maxQueueSize << endl;

    defaultAviPixelFormat(pDev);

    // start the execution of the 'live' thread.
    cout << "Press [ENTER] to stop the acquisition thread" << endl;
    ImageQueue imageQueue;

    helper::RequestProvider requestProvider( pDev );
    // initialise display window
    // IMPORTANT: It's NOT safe to create multiple display windows in multiple threads!!!
    // IMPORTANT: If you need to access the queue from multiple threads appropriate security
    // mechanisms (e.g. critical sections) must be used. Here we don't care about that as we
    // will NOT access the queue from multiple threads at the same time!
    deque<FrameInfo> frameInfoList;
    ThreadParameter threadParam( pDev, imageQueue, maxQueueSize, frameRate, frameInfoList, cullStartFrames );
    ImageDisplay& display = threadParam.getDisplayWindow().GetImageDisplay();
    display.SetDisplayMode(mvIMPACT::acquire::display::DM_Default);
    requestProvider.acquisitionStart( myThreadCallback, std::ref( threadParam ) );
    if( _getch() == EOF )
    {
        cout << "Calling '_getch()' did return EOF...\n";
    }
    const double fr = threadParam.getStatistics().framesPerSecond.read();
    threadParam.setFrameRate( static_cast<int>( fr ) );
    if( ( fr - static_cast<double>( static_cast<int>( fr ) ) ) >= 0.5 )
    {
        threadParam.setFrameRate( threadParam.getFrameRate() + 1 );
    }
    requestProvider.acquisitionStop();
    if( imageQueue.empty() )
    {
        cout << "No images have been captured thus no playback or storage can be performed"
             << "Press any key to end the application" << endl;
        if( _getch() == EOF )
        {
            cout << "Calling '_getch()' did return EOF...\n";
        }
        return 1;
    }

    const ImageQueue::size_type qSize = imageQueue.size();
    bool boRun = replay;
    while( boRun )
    {
        cout << "Press 'y' to replay the captured sequence of " << qSize << " images from memory or any other key to end the replay loop." << endl;
        char c = static_cast<char>( _getch() );
        if( c != 'y' )
        {
            boRun = false;
            continue;
        }
        // delay between two image during display
        const DWORD frameDelay = ( threadParam.getFrameRate() > 0 ) ? 1000 / threadParam.getFrameRate() : 40;
        // obtain pointer to display structure
        ImageDisplay& replayDisplay = threadParam.getDisplayWindow().GetImageDisplay();
        cout << "Replaying the last " << qSize << " captured images with " <<  1000 / frameDelay << " Hz..." << endl;
        for( const auto& imageBufferDescriptor : imageQueue )
        {
            replayDisplay.SetImage( imageBufferDescriptor.getBuffer() );
            replayDisplay.Update();
            this_thread::sleep_for( chrono::milliseconds( frameDelay ) );
        }
    }

    if (false) {
        // ask the user if the sequence shall be written into a AVI file
        cout << endl;
        cout << "If you want to save the captured sequence press 'y' or any other key to end the application: ";
        if (_getch() != 'y')
        {
            return 0;
        }

        cout << endl << "Please enter the file name for the resulting AVI stream(use proper file extensions like *.avi as otherwise creating the stream may fail): ";
        // string fileName;
        cin >> fileName;
    }
    // Now we have to create and configure the AVI stream
    unique_ptr<AVIWrapper> pAVIWrapper;
    boRun = true;
    while( boRun )
    {
        try
        {
            // create the AVI file builder
            pAVIWrapper = unique_ptr<AVIWrapper>( new AVIWrapper() );
            pAVIWrapper->OpenAVIFile( fileName.c_str(), OF_WRITE | OF_CREATE | OF_SHARE_DENY_WRITE );
            // To select from installed compression handlers, pass codecMax as codec to the next function, which is also
            // the default parameter if not specified. Windows will display a dialog to select the codec then.
            // Most codecs only accept RGB888 data with no alpha byte. Make sure that either the driver is
            // operated in RGB888Packed mode or you supply the correct image data converted by hand here.
            cout << "Please select a compression handler from the dialog box (which might be hidden behind this window)" << endl << endl;
            const ImageBuffer* pIB = imageQueue.front().getBuffer();
            pAVIWrapper->CreateAVIStreamFromDIBs( pIB->iWidth, pIB->iHeight, pIB->iBytesPerPixel * 8, threadParam.getFrameRate(), 8000, "myStream" );
            boRun = false;
        }
        catch( const AVIException& e )
        {
            cout << "Error while creating AVI stream(" << string( e.what() ) << ")." << endl
                 << "Please note, that not every codec will accept every pixel format, thus this error might" << endl
                 << "appear without changing the destination pixel format within the driver. However the" << endl
                 << "format selected in this sample (RGB888Packed) works for the greatest number of codecs" << endl
                 << "Unable to continue. Press 'q' to end the application or any other key to select a different" << endl
                 << "compression handler." << endl;
            if( _getch() == 'q' )
            {
                return 1;
            }
        }
    }

    infoFilename = fileName.substr(0, fileName.length() - 3) + "txt";
    cout << "Saving frame info to " << infoFilename << endl;
    ofstream infoStream(infoFilename.c_str(), ofstream::out);
    infoStream << "%infoTimeStamp_ns,infoExposeStart_us,"
        "infoExposeTime_us,infoFrameNr\n";
    cout << "Image queue size " << qSize << " frame info list size " << frameInfoList.size() << endl;
    // we should have a valid AVI stream by now thus we can start to write the images to it
    for( ImageQueue::size_type x = 0; x < qSize; x++ )
    {
        cout << "Storing image " << x << " in stream " << fileName << ".\r";
        const ImageBuffer* pIB = imageQueue[x].getBuffer();
        // Unfortunately we have to flip the images as they are stored upside down in the stream...
        inplaceHorizontalMirror( pIB );
        pAVIWrapper->SaveDataToAVIStream( reinterpret_cast<unsigned char*>( pIB->vpData ), pIB->iSize );
        const FrameInfo& fi = frameInfoList[x];
        infoStream << fi.infoTimeStamp_us
            << "000," << fi.infoExposeStart_us << "," << fi.infoExposeTime_us
            << "," << fi.infoFrameNr << "\n";
    }
    return 0;
}

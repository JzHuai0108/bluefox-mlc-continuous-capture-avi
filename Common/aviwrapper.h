//----------------------------------------------------------------------------------------
#ifndef aviwrapperH
#define aviwrapperH aviwrapperH
//----------------------------------------------------------------------------------------
#include "avihelper.h"

//----------------------------------------------------------------------------------------
class AVIWrapper
//----------------------------------------------------------------------------------------
{
    // member data
    /// Keeps track about the number of images stored in the actual stream
    int                 m_AVIStreamFrameCounter;
    /// pointer to the structure describing the AVI-file
    PAVIFILE            m_pAVIFile;
    /// pointer to an uncompressed AVI stream
    PAVISTREAM          m_pAVIStream;
    /// pointer to a compressed AVI stream (this is used when a compression handler is used)
    PAVISTREAM          m_pAVIStreamCompressed;
    /// A structure containing information about the AVI stream
    AVISTREAMINFO       m_AVIStreamInfo;
    /// A structure for storing the compression options of the AVI stream
    AVICOMPRESSOPTIONS  m_AVICompressionOptions;
    /// The used codec
    DWORD               m_codec;
    /// Usage count for this class
    static unsigned int m_usageCount;
    // functions
    void                CloseStreamsAndFiles( void );
    void                SetupStreamStructs( BITMAPINFOHEADER& BmpHeader, int w, int h, int bitcount, DWORD sampleRate, DWORD quality, const char* pName = "default", CODEC_T codec = codecMax );
    /// Do not allow copy construction
    AVIWrapper( const AVIWrapper& scr );
public:
    // construction/destruction
    explicit AVIWrapper( const char* pFilename = 0, UINT mode = OF_READ );
    ~AVIWrapper( void );

    // user interface
    void CloseAVIFile( void );
    void CreateAVIStreamFromDIBs( int w, int h, int bitcount, DWORD sampleRate, DWORD quality, const char* pName = "default", CODEC_T codec = codecMax );
    void CreateAVIStreamFromJPEGs( int w, int h, int bitcount, DWORD sampleRate, DWORD quality, const char* pName = "default" );
    const char* GetStreamName( void ) const
    {
        return m_AVIStreamInfo.szName;
    }
    bool UsesCompressionHandler( void ) const
    {
        return m_codec != CodecToFourccCode( codecNoCompression );
    }
    void OpenAVIFile( const char* pFilename, UINT mode = OF_READ );
    void SaveDataToAVIStream( unsigned char* pData, int size );
};

#endif // aviwrapperH
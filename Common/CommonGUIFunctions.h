//-----------------------------------------------------------------------------
#ifndef CommonGUIFunctionsH
#define CommonGUIFunctionsH CommonGUIFunctionsH
//-----------------------------------------------------------------------------
#include <mvIMPACT_CPP/mvIMPACT_acquire.h>

#include "wxIncludePrologue.h"
#include <wx/thread.h>
#include "wxIncludeEpilogue.h"

class wxWindow;

//=============================================================================
//================= Implementation UpdateDeviceListThread =====================
//=============================================================================
//------------------------------------------------------------------------------
class UpdateDeviceListThread : public wxThread
//------------------------------------------------------------------------------
{
    const mvIMPACT::acquire::DeviceManager& devMgr_;
protected:
    void* Entry( void )
    {
        devMgr_.updateDeviceList();
        return 0;
    }
public:
    explicit UpdateDeviceListThread( const mvIMPACT::acquire::DeviceManager& devMgr ) : wxThread( wxTHREAD_JOINABLE ),
        devMgr_( devMgr ) {}
};

void UpdateDeviceListWithProgressMessage( wxWindow* pParent, const mvIMPACT::acquire::DeviceManager& devMgr );

#endif // CommonGUIFunctionsH

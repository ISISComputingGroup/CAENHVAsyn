/**
 *-----------------------------------------------------------------------------
 * Title      : CAEN HV Asyn module
 * ----------------------------------------------------------------------------
 * File       : crate.cpp
 * Author     : Jesus Vasquez, jvasquez@slac.stanford.edu
 * Created    : 2019-08-30
 * ----------------------------------------------------------------------------
 * Description:
 * CAEN HV Power Supplies Crate Class
 * ----------------------------------------------------------------------------
 * This file is part of l2MpsAsyn. It is subject to
 * the license terms in the LICENSE.txt file found in the top-level directory
 * of this distribution and at:
    * https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
 * No part of l2MpsAsyn, including this file, may be
 * copied, modified, propagated, or distributed except according to the terms
 * contained in the LICENSE.txt file.
 * ----------------------------------------------------------------------------
**/
#include "crate.h"
#include <epicsThread.h>

void ICrate::GetPropList()
{
    std::string functionName("GetPropList");

    unsigned short NumProp;
    char *PropNameList;
    CAENHVRESULT r =  CAENHV_GetSysPropList(handle, &NumProp, &PropNameList);

    std::stringstream retMessage;
    retMessage << "CAENHV_GetSysPropList: " << CAENHV_GetError(handle) << " (num. " << r << ")";

    printMessage(functionName, retMessage.str().c_str());

    if ( r != CAENHV_OK )
        return;

    char* p = PropNameList;


    for (std::size_t i(0); i < NumProp; ++i, p += strlen(p) + 1)
    {
        // Get Property info
        unsigned PropMode;
        unsigned PropType;
        if ( CAENHV_GetSysPropInfo(handle, p, &PropMode, &PropType) == CAENHV_OK )
        {
            if (readOnly) {
                if (PropMode == SYSPROP_MODE_RDWR) {
                    PropMode = SYSPROP_MODE_RDONLY;
                } else if (PropMode == SYSPROP_MODE_WRONLY) {
                    continue;
                }
            }
            switch( PropType )
            {
                case SYSPROP_TYPE_STR:
                    systemPropertyStrings.push_back(ISystemPropertyString::create(handle, p, PropMode));
                    break;

                case SYSPROP_TYPE_REAL:
                    systemPropertyFloats.push_back(ISystemPropertyFloat::create(handle, p, PropMode));
                    break;

                case SYSPROP_TYPE_UINT2:
                    systemPropertyIntegers.push_back(ISystemPropertyIntegerTemplate<uint16_t>::create(handle, p, PropMode));
                    break;

                case SYSPROP_TYPE_UINT4:
                    systemPropertyIntegers.push_back(ISystemPropertyIntegerTemplate<uint32_t>::create(handle, p, PropMode));
                    break;

                case SYSPROP_TYPE_INT2:
                    systemPropertyIntegers.push_back(ISystemPropertyIntegerTemplate<int16_t>::create(handle, p, PropMode));
                    break;

                case SYSPROP_TYPE_INT4:
                    systemPropertyIntegers.push_back(ISystemPropertyIntegerTemplate<int32_t>::create(handle, p, PropMode));
                    break;

                case SYSPROP_TYPE_BOOLEAN:
                    systemPropertyIntegers.push_back(ISystemPropertyIntegerTemplate<uint8_t>::create(handle, p, PropMode));
                    break;
            }

        }
    }

    // Memory allocated across CRTs can cause issues on Windows
    #ifndef _WIN32
    free(PropNameList);
    #endif
}

void ICrate::GetCrateMap()
{
    // Get Crate Map
    std::string functionName("GetCrateMap");

    unsigned short NrOfSlot;
    unsigned short *NrOfChList;
    char *ModelList;
    char *DescriptionList;
    unsigned short *SerNumList;
    unsigned char *FmwRelMinList;
    unsigned char *FmwRelMaxList;

    CAENHVRESULT r = CAENHV_GetCrateMap(handle, &NrOfSlot, &NrOfChList, &ModelList, &DescriptionList, &SerNumList, &FmwRelMinList, &FmwRelMaxList);

    std::stringstream retMessage;
    retMessage << "CAENHV_GetCrateMap: " << CAENHV_GetError(handle) << " (num. " << r << ")";

    printMessage(functionName, retMessage.str().c_str());

    if ( r != CAENHV_OK )
        return;


    numSlots = NrOfSlot;
    char *m = ModelList, *d = ModelList;

    for (std::size_t i(0); i < NrOfSlot; ++i, m += strlen(m) + 1, d += strlen(d) + 1)
    {
        if ( *m != '\0' )
        {
            std::stringstream sn, fw;

            // Process the serial number
            sn.str("");
            sn << SerNumList[i];

            // Process the firmware release number
            fw.str("");
            fw << unsigned(FmwRelMaxList[i]) << "." << unsigned(FmwRelMinList[i]);

            // Create a new Slot object and add it to the vector
            boards.push_back( IBoard::create(handle, i, m, d, NrOfChList[i], sn.str(), fw.str(), readOnly) );
        }
    }

    // Memory allocated across CRTs can cause issues on Windows
    #ifndef _WIN32
    // Deallocate memory (Use RAII in the future for this)
    free(NrOfChList);
    free(ModelList);
    free(DescriptionList);
    free(SerNumList);
    free(FmwRelMinList);
    free(FmwRelMaxList);
    #endif
}

// called by driver to read something from the crate to keep the connection alive 
void ICrate::keepAlive()
{
    char swRelease[256];
    if (connected)
    {
        CAENHV_GetSysProp(handle, "SwRelease", swRelease);
    }
}

ICrate::ICrate(int systemType, const std::string& ipAddr, const std::string& userName, const std::string& password, const bool readOnly)
:
  handle(-1),
  readOnly(readOnly),
  connected(false),
  systemType(systemType),
  ipAddr(ipAddr),
  userName(userName),
  password(password)
{
    handle = InitSystem(systemType, ipAddr, userName, password);
    connected = true;
    GetPropList();
    GetCrateMap();
}

Crate ICrate::create(int systemType, const std::string& ipAddr, const std::string& userName, const std::string& password, const bool readOnly)
{
    return std::make_shared<ICrate>(systemType, ipAddr, userName, password, readOnly);
}

ICrate::~ICrate()
{
}

void ICrate::reconnect()
{
    if (connected)
    {
        connected = false;
        DeinitSystem();
    }
    epicsThreadSleep(1.0);
    try {
        handle = InitSystem(systemType, ipAddr, userName, password);
    }
    catch(const std::runtime_error& e) {
        std::cerr << "Reconnect failed: " << e.what() << std::endl;
    }
    
}
    

int ICrate::InitSystem(int systemType, const std::string& ipAddr, const std::string& userName, const std::string& password) const
{
    int h;
    std::string functionName("initSystem");
    
    CAENHVRESULT r = CAENHV_InitSystem( static_cast<CAENHV_SYSTEM_TYPE_t>(systemType),
                                        LINKTYPE_TCPIP,
                                        const_cast<void*>( static_cast<const void*>( ipAddr.c_str() ) ),
                                        userName.c_str(),
                                        password.c_str(),
                                        &h );

    std::stringstream retMessage;
    retMessage << "CAENHV_InitSystem: " << CAENHV_GetError(h) << " (num. " << r << ")";

    printMessage(functionName, retMessage.str());

    if( r != CAENHV_OK )
        throw std::runtime_error(retMessage.str().c_str());

    return h;
}

void ICrate::DeinitSystem()
{
    CAENHVRESULT r = CAENHV_DeinitSystem(handle);
    if( r != CAENHV_OK ) {
        std::cerr << "Error " << r << " from CAENHV_DenitSystem" << std::endl;
    }
    handle = -1;
}


void ICrate::printInfo(std::ostream& stream) const
{
    stream << "=========================" << std::endl;;
    stream << "Crate object information:" << std::endl;;
    stream << "=========================" << std::endl;;
    stream << "  handle = " << handle << std::endl;
    stream << "  Number of slots  : " << numSlots << std::endl;
    stream << "  Number of boards : " << boards.size() << std::endl;
    stream << "  Properties:" << std::endl;;
    stream << "  ---------------------------" << std::endl;
    printProperties( stream, "integer", systemPropertyIntegers );
    printProperties( stream, "float",   systemPropertyFloats   );
    printProperties( stream, "string",  systemPropertyStrings  );
    stream << "  Board information: " << std::endl;
    stream << "  ---------------------------" << std::endl;
    for (std::vector<Board>::const_iterator it = boards.begin(); it != boards.end(); ++it)
        (*it)->printInfo(stream);
    stream << "=========================" << std::endl;;
    stream << std::endl;
}

void ICrate::printCrateMap(std::ostream& stream) const
{
    stream << "=============================" << std::endl;;
    stream << "Crate information:" << std::endl;;
    stream << "=============================" << std::endl;;
    stream << "  Number of slots  : " << numSlots << std::endl;
    stream << "  Number of boards : " << boards.size() << std::endl;
    stream << "  Board information: " << std::endl;
    stream << "  ---------------------------" << std::endl;
    for (std::vector<Board>::const_iterator it = boards.begin(); it != boards.end(); ++it)
        (*it)->printBoardInfo(stream);
    stream << "  ---------------------------" << std::endl;
    stream << "=============================" << std::endl;;
}

template <typename T>
void ICrate::printProperties(std::ostream& stream, const std::string& type, const T& pv) const
{
    std::size_t n(pv.size());
    stream << "    Number of properties of type " << type << ": " << n << std::endl;
    if (n)
        for (auto it = pv.begin(); it != pv.end(); ++it)
            (*it)->printInfo(stream);
}

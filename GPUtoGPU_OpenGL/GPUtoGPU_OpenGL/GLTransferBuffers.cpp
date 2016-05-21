//--------------------------------------------------------------------------------------
//
//
// Copyright 2014 ADVANCED MICRO DEVICES, INC.  All Rights Reserved.
//
// AMD is granting you permission to use this software and documentation (if
// any) (collectively, the "Materials") pursuant to the terms and conditions
// of the Software License Agreement included with the Materials.  If you do
// not have a copy of the Software License Agreement, contact your AMD
// representative for a copy.
// You agree that you will not reverse engineer or decompile the Materials,
// in whole or in part, except as allowed by applicable law.
//
// WARRANTY DISCLAIMER: THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND.  AMD DISCLAIMS ALL WARRANTIES, EXPRESS, IMPLIED, OR STATUTORY,
// INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE, NON-INFRINGEMENT, THAT THE SOFTWARE
// WILL RUN UNINTERRUPTED OR ERROR-FREE OR WARRANTIES ARISING FROM CUSTOM OF
// TRADE OR COURSE OF USAGE.  THE ENTIRE RISK ASSOCIATED WITH THE USE OF THE
// SOFTWARE IS ASSUMED BY YOU.
// Some jurisdictions do not allow the exclusion of implied warranties, so
// the above exclusion may not apply to You.
//
// LIMITATION OF LIABILITY AND INDEMNIFICATION:  AMD AND ITS LICENSORS WILL
// NOT, UNDER ANY CIRCUMSTANCES BE LIABLE TO YOU FOR ANY PUNITIVE, DIRECT,
// INCIDENTAL, INDIRECT, SPECIAL OR CONSEQUENTIAL DAMAGES ARISING FROM USE OF
// THE SOFTWARE OR THIS AGREEMENT EVEN IF AMD AND ITS LICENSORS HAVE BEEN
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGES. 
// In no event shall AMD's total liability to You for all damages, losses,
// and causes of action (whether in contract, tort (including negligence) or
// otherwise) exceed the amount of $100 USD.  You agree to defend, indemnify
// and hold harmless AMD and its licensors, and any of their directors,
// officers, employees, affiliates or agents from and against any and all
// loss, damage, liability and other expenses (including reasonable attorneys'
// fees), resulting from Your use of the Software or violation of the terms and
// conditions of this Agreement. 
//
// U.S. GOVERNMENT RESTRICTED RIGHTS: The Materials are provided with "RESTRICTED
// RIGHTS." Use, duplication, or disclosure by the Government is subject to the
// restrictions as set forth in FAR 52.227-14 and DFAR252.227-7013, et seq., or
// its successor.  Use of the Materials by the Government constitutes
// acknowledgement of AMD's proprietary rights in them.
//
// EXPORT RESTRICTIONS: The Materials may be subject to export restrictions as
// stated in the Software License Agreement.
//
//--------------------------------------------------------------------------------------


#include "os_include.h"
#include <assert.h>

#include "FormatInfo.h"
#include "GLTransferBuffers.h"



GLTransferBuffers::GLTransferBuffers(void)
{
    m_bBufferReady = false;

    m_uiTarget      = 0;
    m_uiNumBuffers  = 0;
    m_uiBufferSize  = 0;
    m_pBuffer       = NULL;
    m_pBufferMemory = NULL;

    m_uiBufferIdx = 0;

    m_bUseP2P       = false;

    m_bAllocatedPinnedMem = false;
}


GLTransferBuffers::~GLTransferBuffers(void)
{
#ifdef WIN32
    if (wglGetCurrentContext() && m_pBuffer)
        glDeleteBuffers(m_uiNumBuffers, m_pBuffer);
#endif

#ifdef LINUX
    if (glXGetCurrentContext() && m_pBuffer)
        glDeleteBuffers(m_uiNumBuffers, m_pBuffer);
#endif

    if (!m_bUseP2P)
    {
        if (m_bAllocatedPinnedMem)
        {
            // Only in case of pinned memory, memory was allocated and needs
            // to be deleted
            for (unsigned int i = 0; i < m_uiNumBuffers; i++)
            {
                if (m_pBufferMemory && m_pBufferMemory[i].pBasePointer)
                    delete [] m_pBufferMemory[i].pBasePointer;
            }

            if (m_pBufferMemory)
                delete [] m_pBufferMemory;
        }
    }
    else
    {
        if (m_pBufferBusAddress)
        {
            delete [] m_pBufferBusAddress;
        }

        if (m_pMarkerBusAddress)
        {
            delete [] m_pMarkerBusAddress;
        }
    }

    if (m_pBuffer)
        delete [] m_pBuffer;

}




bool GLTransferBuffers::createBuffers(unsigned int uiNumBuffers, unsigned int uiBufferSize, unsigned int uiTarget, bool bUseP2P, AlignedMem *pPinnedMem )
{
    // Check if buffers were already allocated. If so delete them
    for (unsigned int i = 0; i < m_uiNumBuffers && m_pBufferMemory; i++)
    {
        if (m_pBufferMemory[i].pBasePointer)
            delete [] m_pBufferMemory[i].pBasePointer;

        glDeleteBuffers(1, &m_pBuffer[i]);
    }

    if (uiTarget != GL_PIXEL_PACK_BUFFER && uiTarget != GL_PIXEL_UNPACK_BUFFER)
        return false;

    m_pBufferMemory = NULL;

    m_uiNumBuffers = uiNumBuffers;
    m_uiBufferSize = uiBufferSize;  

    m_uiTarget = uiTarget;

    m_bUseP2P = bUseP2P;

    // Generate GL Buffers
    m_pBuffer = new unsigned int[m_uiNumBuffers];
    glGenBuffers(m_uiNumBuffers, m_pBuffer);

    GLenum nUsage = m_uiTarget == GL_PIXEL_PACK_BUFFER ? GL_DYNAMIC_READ : GL_DYNAMIC_DRAW;

    if (m_bUseP2P)
    {
        /////////////////////////////////////////////
        // Create bus addressable memory buffers
		
        m_pBufferBusAddress = new GLuint64[m_uiNumBuffers];
        m_pMarkerBusAddress = new GLuint64[m_uiNumBuffers];

        if (uiTarget == GL_PIXEL_UNPACK_BUFFER)
        {
            glGetError();

            // In the case of an unpack buffer, the memory is allocated on the
            // GPU visible framebuffer. 
            // In case of a pack buffer, memory is allocated on visible sdi
            // framebuffer, this needs to be done in the sdi thread and not here!
            // In this case only the buffer is generated and memory addressed will
            // be passed later.
            for (unsigned int i = 0; i < m_uiNumBuffers; i++)
            {
                glBindBuffer(GL_BUS_ADDRESSABLE_MEMORY_AMD, m_pBuffer[i]);
                glBufferData(GL_BUS_ADDRESSABLE_MEMORY_AMD, m_uiBufferSize, 0, nUsage);
            }

            // Call makeResident when all BufferData calls were submitted.
            glMakeBuffersResidentAMD(m_uiNumBuffers, m_pBuffer, m_pBufferBusAddress, m_pMarkerBusAddress);

            // Make sure that the buffer creation really succeed
            if (glGetError() != GL_NO_ERROR)
                return false;

            glBindBuffer(GL_BUS_ADDRESSABLE_MEMORY_AMD, 0);
        }
    }
    else
    {
        /////////////////////////////////////////////
        // Create pinned memory buffers
        /////////////////////////////////////////////
        if (!pPinnedMem)
        {
            m_pBufferMemory = new AlignedMem[m_uiNumBuffers];
            memset(m_pBufferMemory, 0, m_uiNumBuffers * sizeof(AlignedMem));

            // indicate that pinned mem was allocated and needs to be deleted.
            m_bAllocatedPinnedMem = true;
        }
        else
        {
            m_pBufferMemory = pPinnedMem;
        }

        for (unsigned int i = 0; i < m_uiNumBuffers; i++)
        {
            if (!m_pBufferMemory[i].pBasePointer)
            {
                m_pBufferMemory[i].pBasePointer = new char[m_uiBufferSize + 4096];
                memset(m_pBufferMemory[i].pBasePointer, 0, (m_uiBufferSize + 4096));

                // Align memory to 4K boundaries
                size_t addr = (size_t) m_pBufferMemory[i].pBasePointer;
                m_pBufferMemory[i].pAlignedPointer = (char*)((addr + 4095) & (~0xfff));
            }

            glBindBuffer(GL_EXTERNAL_VIRTUAL_MEMORY_AMD, m_pBuffer[i]);
            glBufferData(GL_EXTERNAL_VIRTUAL_MEMORY_AMD, m_uiBufferSize, m_pBufferMemory[i].pAlignedPointer, nUsage);
            glBindBuffer(GL_EXTERNAL_VIRTUAL_MEMORY_AMD, 0);
        }
    }

    m_bBufferReady = true;

    return true;
}


bool GLTransferBuffers::assignRemoteMemory(unsigned int uiNumBuffers, unsigned long long* pBufferBusAddress, unsigned long long* pMarkerBusAddress)
{
    // This function is only valid for P2P pack buffer
    if (!m_bUseP2P || m_uiTarget != GL_PIXEL_PACK_BUFFER)
        return false;

    // For each buffer that was created a buffer bus address and a marker bus address
    // is needed.
    if (uiNumBuffers != m_uiNumBuffers)
        return false;

    memcpy(m_pBufferBusAddress, pBufferBusAddress, uiNumBuffers * sizeof(unsigned long long));
    memcpy(m_pMarkerBusAddress, pMarkerBusAddress, uiNumBuffers * sizeof(unsigned long long));

    for (unsigned int i = 0; i < m_uiNumBuffers; i++)
    {
        glBindBuffer(GL_EXTERNAL_PHYSICAL_MEMORY_AMD, m_pBuffer[i]);
        
        glGetError();

        glBufferBusAddressAMD(GL_EXTERNAL_PHYSICAL_MEMORY_AMD, m_uiBufferSize, m_pBufferBusAddress[i], (m_pMarkerBusAddress[i] & ~0xfff));

        if (glGetError() != GL_NO_ERROR)
            return false;
    }

    glBindBuffer(GL_EXTERNAL_PHYSICAL_MEMORY_AMD, 0);

    return true;
}


char* GLTransferBuffers::getPinnedMemoryPtr(unsigned int uiIdx)
{
    if (m_bBufferReady && uiIdx < m_uiNumBuffers && !m_bUseP2P)
    {
        return m_pBufferMemory[uiIdx].pAlignedPointer;
    }

    return NULL;
}

GLuint64 GLTransferBuffers::getBufferBusAddress(unsigned int uiIdx)
{
    if (m_bBufferReady && uiIdx < m_uiNumBuffers && m_bUseP2P)
    {
        return m_pBufferBusAddress[uiIdx];
    }

    return 0;
}

GLuint64 GLTransferBuffers::getMarkerBusAddress(unsigned int uiIdx)
{
    if (m_bBufferReady && uiIdx < m_uiNumBuffers && m_bUseP2P)
    {
        return m_pMarkerBusAddress[uiIdx];
    }

    return 0;
}



bool GLTransferBuffers::bindBuffer(unsigned long long ulBusAddress)
{
    unsigned int i;

    for (i = 0; i < m_uiNumBuffers; i++)
    {
        if (m_pBufferBusAddress[i] == ulBusAddress)
        {
            m_uiBufferIdx = i;
            break;
        }
    }

    if (i < m_uiNumBuffers && m_uiBufferIdx < m_uiNumBuffers)
    {
        glBindBuffer(m_uiTarget, m_pBuffer[m_uiBufferIdx]);

        return true;
    }

    return false;
}


bool GLTransferBuffers::bindBuffer(unsigned int uiIdx)
{
    if (uiIdx < m_uiNumBuffers)
    {
        m_uiBufferIdx = uiIdx;
        glBindBuffer(m_uiTarget, m_pBuffer[m_uiBufferIdx]);

        return true;
    }

    return false;
}



void GLTransferBuffers::waitMarker(unsigned int uiMarkerValue)
{
    if (m_bUseP2P)
    {
        glWaitMarkerAMD(m_pBuffer[m_uiBufferIdx], uiMarkerValue);
    }
}


void GLTransferBuffers::writeMarker(unsigned long long ulBufferBusAddress, unsigned long long ulMarkerBusAddress, unsigned int uiMarkerValue)
{
    if (m_bUseP2P)
    {
        assert(m_pBufferBusAddress[m_uiBufferIdx] == ulBufferBusAddress);
        assert(m_pMarkerBusAddress[m_uiBufferIdx] == ulMarkerBusAddress);

        // The GL buffer was created with the aligned Marker Address, so we need to pass here the offset of the
        // aligned address to the real address.
        glWriteMarkerAMD(m_pBuffer[m_uiBufferIdx], uiMarkerValue, ulMarkerBusAddress & 0xfff);
        glFlush();
    }
}


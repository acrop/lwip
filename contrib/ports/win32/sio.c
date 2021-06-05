/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 */

#include <lwip/opt.h>
#include <lwip/sys.h>
#include <lwip/sio.h>

#include <stdio.h>
#include <stdarg.h>

#ifdef _MSC_VER
#pragma warning (push, 3)
#endif
#include <windows.h>
#include <setupapi.h>
#ifdef _MSC_VER
#pragma warning (pop)
#endif
#include "lwipcfg.h"

#pragma comment( lib, "setupapi" )

typedef struct sio_win32_fd {
  HANDLE handle;
  u8_t orig_devnum;
  u8_t devnum;
  u32_t baud_rate;
  u8_t reconnected;
} sio_win32_fd_t;

/** When 1, use COM ports, when 0, use named pipes (for simulation). */
#ifndef SIO_USE_COMPORT
#define SIO_USE_COMPORT 1
#endif

/** If SIO_USE_COMPORT==1, use COMx, if 0, use a pipe (default) */
#if SIO_USE_COMPORT
#define SIO_DEVICENAME "\\\\.\\COM"
#else
#define SIO_DEVICENAME "\\\\.\\pipe\\lwip"
#endif

#if SIO_USE_COMPORT
#ifndef SIO_COMPORT_BYTESIZE
#define SIO_COMPORT_BYTESIZE 8
#endif
#ifndef SIO_COMPORT_STOPBITS
#define SIO_COMPORT_STOPBITS 0 /* ONESTOPBIT */
#endif
#ifndef SIO_COMPORT_PARITY
#define SIO_COMPORT_PARITY 0 /* NOPARITY */
#endif
#endif /* SIO_USE_COMPORT */

static int sio_abort = 0;

/* \\.\pipe\lwip0 */
/* pppd /dev/ttyS0 logfile mylog debug nocrtscts local noauth noccp ms-dns 212.27.54.252 192.168.0.4:192.168.0.5
 */

/**
 * SIO_DEBUG: Enable debugging for SIO.
 */
#ifndef SIO_DEBUG
#define SIO_DEBUG    LWIP_DBG_OFF
#endif

#if SIO_USE_COMPORT
/** When using a real COM port, set up the
 * serial line settings (baudrate etc.)
 */
static BOOL
sio_setup(HANDLE fd, u32_t baud_rate)
{
  COMMTIMEOUTS cto;
  DCB dcb;

  /* set up baudrate and other communication settings */
  memset(&dcb, 0, sizeof(dcb));
  /* Obtain the DCB structure for the device */
  if (!GetCommState(fd, &dcb)) {
    return FALSE;
  }
  /* Set the new data */
  dcb.BaudRate = baud_rate;
  dcb.ByteSize = SIO_COMPORT_BYTESIZE;
  dcb.StopBits = 0; /* ONESTOPBIT */
  dcb.Parity   = 0; /* NOPARITY */
  dcb.fParity  = 0; /* parity is not used */
  /* do not use flow control */
  /*dcb.fOutxDsrFlow = dcb.fDtrControl = 0;
  dcb.fOutxCtsFlow = dcb.fRtsControl = 0;
  dcb.fErrorChar = dcb.fNull = 0;
  dcb.fInX = dcb.fOutX = 0;
  dcb.XonChar = dcb.XoffChar = 0;
  dcb.XonLim = dcb.XoffLim = 100;*/
  /* Set the new DCB structure */
  if (!SetCommState(fd, &dcb)) {
    return FALSE;
  }

  if (!SetupComm(fd, 8192, 8192)) {
    return FALSE;
  }

  memset(&cto, 0, sizeof(cto));
  if(!GetCommTimeouts(fd, &cto)) {
    return FALSE;
  }
  /* change read timeout, leave write timeout as it is */
  cto.ReadIntervalTimeout = 1;
  cto.ReadTotalTimeoutMultiplier = 0;
  cto.ReadTotalTimeoutConstant = 1; /* 1 ms */
  if(!SetCommTimeouts(fd, &cto)) {
    return FALSE;
  }
  return TRUE;
}
#endif /* SIO_USE_COMPORT */

BOOL RegQueryValueString(HKEY kKey, wchar_t* lpValueName, wchar_t** pszValueOut)
{
  wchar_t* pszValue= NULL;

  //First query for the size of the registry value
  DWORD dwType = 0;
  DWORD dwDataSize = 0;
  LONG nError = RegQueryValueExW(kKey, lpValueName, NULL, &dwType, NULL, &dwDataSize);

  //Initialize the output parameter
  *pszValueOut = NULL;
  if (nError != ERROR_SUCCESS)
  {
    SetLastError(nError);
    return FALSE;
  }

  //Ensure the value is a string
  if (dwType != REG_SZ)
  {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }

  //Allocate enough bytes for the return value
  DWORD dwAllocatedSize = dwDataSize + sizeof(wchar_t); //+sizeof(TCHAR) is to allow us to NULL terminate the data if it is not null terminated in the registry
  pszValue = (wchar_t*)(LocalAlloc(LMEM_FIXED, dwAllocatedSize));
  if (pszValue == NULL)
    return FALSE;

  //Recall RegQueryValueEx to return the data
  pszValue[0] = L'\0';
  DWORD dwReturnedSize = dwAllocatedSize;
  nError = RegQueryValueExW(kKey, lpValueName, NULL, &dwType, (LPBYTE)(pszValue), &dwReturnedSize);
  if (nError != ERROR_SUCCESS)
  {
    LocalFree(pszValue);
    pszValue = NULL;
    SetLastError(nError);
    return FALSE;
  }

  //Handle the case where the data just returned is the same size as the allocated size. This could occur where the data
  //has been updated in the registry with a non null terminator between the two calls to ReqQueryValueEx above. Rather than
  //return a potentially non-null terminated block of data, just fail the method call
  if (dwReturnedSize >= dwAllocatedSize)
  {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }

  //NULL terminate the data if it was not returned NULL terminated because it is not stored null terminated in the registry
  if (pszValue[dwReturnedSize/sizeof(wchar_t) - 1] != L'\0')
  {
    pszValue[dwReturnedSize/sizeof(wchar_t)] = L'\0';
  }

  *pszValueOut = pszValue;
  return TRUE;
}

BOOL IsNumeric(LPCWSTR pszString, BOOL bIgnoreColon)
{
  size_t nLen = wcslen(pszString);
  if (nLen == 0)
    return FALSE;

  //What will be the return value from this function (assume the best)
  BOOL bNumeric = TRUE;

  for (size_t i=0; i<nLen && bNumeric; i++)
  {
    bNumeric = (iswdigit(pszString[i]) != 0);
    if (bIgnoreColon && (pszString[i] == L':'))
      bNumeric = TRUE;
  }

  return bNumeric;
}

BOOL QueryRegistryPortName(HKEY deviceKey, int *nPort)
{
  //What will be the return value from the method (assume the worst)
  BOOL bAdded = FALSE;

  //Read in the name of the port
  wchar_t *sPortName = NULL;
  if (RegQueryValueString(deviceKey, L"PortName", &sPortName))
  {
    //If it looks like "COMX" then
    //add it to the array which will be returned
    const size_t nLen = wcslen(sPortName);
    if (nLen > 3)
    {
      if ((wcsnicmp(sPortName, L"COM", 3) == 0) && IsNumeric((sPortName + 3), FALSE))
      {
        //Work out the port number
        *nPort = _wtoi(sPortName + 3);
        bAdded = TRUE;
      }
    }
  }
  if (sPortName != NULL) {
    LocalFree(sPortName);
  }

  return bAdded;
}

BOOL QueryDeviceDescription(_In_ HDEVINFO hDevInfoSet, _In_ SP_DEVINFO_DATA *devInfo, _Inout_ wchar_t** sFriendlyName)
{
  DWORD dwType = 0;
  DWORD dwDataSize = 0;
  wchar_t *friendlyName = NULL;
  *sFriendlyName = NULL;
  //Query initially to get the buffer size required
  if (!SetupDiGetDeviceRegistryPropertyW(hDevInfoSet, devInfo, SPDRP_DEVICEDESC, &dwType, NULL, 0, &dwDataSize))
  {
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
      return FALSE;
  }

  DWORD dwAllocatedSize = dwDataSize + sizeof(wchar_t); //+sizeof(wchar_t) is to allow us to NULL terminate the data if it is not null terminated in the registry
  friendlyName = (wchar_t*)(LocalAlloc(LMEM_FIXED, dwAllocatedSize));

  if (!SetupDiGetDeviceRegistryPropertyW(hDevInfoSet, devInfo, SPDRP_DEVICEDESC, &dwType, (PBYTE)friendlyName, dwDataSize, &dwDataSize)) {
    LocalFree(friendlyName);
    return FALSE;
  }
  if (dwType != REG_SZ)
  {
    LocalFree(friendlyName);
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }
  *sFriendlyName = friendlyName;
  return TRUE;
}

// , _Inout_ CPortAndNamesArray& ports
BOOL QueryUsingSetupAPI(const GUID guid, _In_ DWORD dwFlags, wchar_t **ports, int portsCount)
{
  memset(ports, 0, portsCount * sizeof(ports[0]));
  //Create a "device information set" for the specified GUID
  HDEVINFO hDevInfoSet = SetupDiGetClassDevsW(&guid, NULL, NULL, dwFlags);
  if (hDevInfoSet == INVALID_HANDLE_VALUE)
    return FALSE;

  //Finally do the enumeration
  BOOL bMoreItems = TRUE;
  int nIndex = 0;
  SP_DEVINFO_DATA devInfo;
  while (bMoreItems)
  {
    //Enumerate the current device
    devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
    bMoreItems = SetupDiEnumDeviceInfo(hDevInfoSet, nIndex, &devInfo);
    if (bMoreItems)
    {
      //Did we find a serial port for this device
      BOOL bAdded = FALSE;
      int nPort = -1;

      //Get the registry key which stores the ports settings
      // ATL::CRegKey deviceKey;
      HKEY deviceKey = SetupDiOpenDevRegKey(hDevInfoSet, &devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
      if (deviceKey != INVALID_HANDLE_VALUE)
      {
        if (QueryRegistryPortName(deviceKey, &nPort))
        {
          bAdded = TRUE;
        }
      }
      //If the port was a serial port, then also try to get its friendly name
      if (bAdded && nPort >= 0 && nPort < portsCount)
      {
        wchar_t *friendlyName = NULL;
        if (QueryDeviceDescription(hDevInfoSet, &devInfo, &friendlyName)) {
          ports[nPort] = friendlyName;
        }
      }
    }

    ++nIndex;
  }

  //Free up the "device information set" now that we are finished with it
  SetupDiDestroyDeviceInfoList(hDevInfoSet);

  //Return the success indicator
  return TRUE;
}

static wchar_t *ports[256];
static BOOL sio_open_win32(sio_win32_fd_t *fd)
{
  CHAR fileName[256];
  if (fd == NULL) {
    goto error;
  }
  LWIP_DEBUGF(SIO_DEBUG, ("sio_open(%lu)\n", (DWORD)fd->devnum));
  fd->orig_devnum = fd->devnum;
  if ((int8_t)fd->devnum < 0) {
    int8_t special_devnum = -((int8_t)fd->devnum);
    // scan the serials by name for special devnum
    QueryUsingSetupAPI(GUID_DEVINTERFACE_COMPORT, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE, ports, sizeof(ports) / sizeof(ports[0]));
    for (size_t i = 0; i < sizeof(ports) / sizeof(ports[0]); ++i)
    {
      wchar_t *port = ports[i];
      if (port) {
        switch(special_devnum) {
          case 1: {
            // ур "Unisoc Usb Serial Port 0"
            wchar_t unisoc_usb_serial_0[] = L"Unisoc Usb Serial Port 0";
            if (wcscmp(port, unisoc_usb_serial_0) == 0) {
              fd->devnum = (uint8_t)i;
            }
            break;
          }

          case 2: {
            // ур "Unisoc Usb Serial Port 5"
            wchar_t unisoc_usb_serial_5[] = L"Unisoc Usb Serial Port 5";
            if (wcscmp(port, unisoc_usb_serial_5) == 0) {
              fd->devnum = (uint8_t)i;
            }
            break;
          }
        }
        LocalFree(ports[i]);
      }
    }
  }

  memset(fileName, 0, sizeof(fileName));
#if SIO_USE_COMPORT
  snprintf(fileName, 255, SIO_DEVICENAME"%lu", (DWORD)(fd->devnum));
#else /* SIO_USE_COMPORT */
  snprintf(fileName, 255, SIO_DEVICENAME"%lu", (DWORD)(fd->devnum & ~1));
  if ((fd->devnum & 1) == 0) {
    fd->handle = CreateNamedPipeA(fileName, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_NOWAIT,
      PIPE_UNLIMITED_INSTANCES, 102400, 102400, 100, NULL);
  } else
#endif /* SIO_USE_COMPORT */
  {
    fd->handle = CreateFileA(fileName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  }
  if (fd->handle != INVALID_HANDLE_VALUE) {
#if !SIO_USE_COMPORT
    if (fd->devnum & 1) {
      DWORD mode = PIPE_NOWAIT;
      if (!SetNamedPipeHandleState(fd->handle, &mode, NULL, NULL)) {
        goto error;
      }
    } else
#endif /* !SIO_USE_COMPORT */
    {
      FlushFileBuffers(fd->handle);
    }
#if SIO_USE_COMPORT
    if(!sio_setup(fd->handle, fd->baud_rate)) {
      CloseHandle(fd->handle);
      goto error;
    }
#endif /* SIO_USE_COMPORT */
    LWIP_DEBUGF(SIO_DEBUG, ("sio_open(%lu) successfully opened.\n", (DWORD)fd->devnum));
  }
  return TRUE;

error:
  LWIP_DEBUGF(SIO_DEBUG, ("sio_open(%lu) failed. GetLastError() returns %lu\n",
              (DWORD)fd->devnum, GetLastError()));
  return FALSE;
}

static BOOL sio_reopen_win32(sio_win32_fd_t *fd)
{
  if (fd->handle != INVALID_HANDLE_VALUE)
  {
    fd->handle = INVALID_HANDLE_VALUE;
    CloseHandle(fd->handle);
  }
  if (sio_open_win32(fd)) {
    fd->reconnected = 1;
    return TRUE;
  }
  return FALSE;
}


/**
 * Opens a serial device for communication.
 *
 * @param devnum device number
 * @return handle to serial device if successful, NULL otherwise
 */
sio_fd_t sio_open(u8_t devnum, u32_t baud_rate)
{
  sio_win32_fd_t *fd = (sio_win32_fd_t *)malloc(sizeof(sio_win32_fd_t));
  if (fd == NULL)
  {
    return (sio_fd_t)fd;
  }
  fd->handle = INVALID_HANDLE_VALUE;
  fd->devnum = devnum;
  fd->baud_rate = baud_rate;
  fd->reconnected = 0;
  if (!sio_open_win32(fd))
  {
    free(fd);
    return NULL;
  }
  return fd;
}

/**
 * Sends a single character to the serial device.
 *
 * @param c character to send
 * @param fd serial device handle
 *
 * @note This function will block until the character can be sent.
 */
void sio_send(u8_t c, sio_fd_t _fd)
{
  sio_win32_fd_t *fd = (sio_win32_fd_t *)(_fd);
  DWORD dwNbBytesWritten = 0;
  LWIP_DEBUGF(SIO_DEBUG, ("sio_send(%lu)\n", (DWORD)c));
  while ((!WriteFile(fd->handle, &c, 1, &dwNbBytesWritten, NULL)) || (dwNbBytesWritten < 1)) {
  }
}

/**
 * Reads from the serial device.
 *
 * @param fd serial device handle
 * @param data pointer to data buffer for receiving
 * @param len maximum length (in bytes) of data to receive
 * @return number of bytes actually received - may be 0 if aborted by sio_read_abort
 *
 * @note This function will block until data can be received. The blocking
 * can be cancelled by calling sio_read_abort().
 */
u32_t sio_read(sio_fd_t _fd, u8_t* data, u32_t len)
{
  sio_win32_fd_t *fd = (sio_win32_fd_t *)(_fd);
  BOOL ret;
  DWORD dwNbBytesReadden = 0;
  LWIP_DEBUGF(SIO_DEBUG, ("sio_read()...\n"));
  ret = ReadFile(fd->handle, data, len, &dwNbBytesReadden, NULL);
  LWIP_DEBUGF(SIO_DEBUG, ("sio_read()=%lu bytes -> %d\n", dwNbBytesReadden, ret));
  LWIP_UNUSED_ARG(ret);
  return dwNbBytesReadden;
}

/**
 * Tries to read from the serial device. Same as sio_read but returns
 * immediately if no data is available and never blocks.
 *
 * @param fd serial device handle
 * @param data pointer to data buffer for receiving
 * @param len maximum length (in bytes) of data to receive
 * @return number of bytes actually received
 */
u32_t sio_tryread(sio_fd_t _fd, u8_t* data, u32_t len)
{
  sio_win32_fd_t *fd = (sio_win32_fd_t *)(_fd);
  /* @todo: implement non-blocking read */
  BOOL ret;
  DWORD dwNbBytesReadden = 0;
  LWIP_DEBUGF(SIO_DEBUG, ("sio_read()...\n"));
  ret = ReadFile(fd->handle, data, len, &dwNbBytesReadden, NULL);
  LWIP_DEBUGF(SIO_DEBUG, ("sio_read()=%lu bytes -> %d\n", dwNbBytesReadden, ret));
  if (!ret) {
    while (!sio_reopen_win32(fd));
  }
  return dwNbBytesReadden;
}

/**
 * Writes to the serial device.
 *
 * @param fd serial device handle
 * @param data pointer to data to send
 * @param len length (in bytes) of data to send
 * @return number of bytes actually sent
 *
 * @note This function will block until all data can be sent.
 */
u32_t sio_write(sio_fd_t _fd, const u8_t* data, u32_t len)
{
  sio_win32_fd_t *fd = (sio_win32_fd_t *)(_fd);
  DWORD dwNbBytesRemain = len;
  LWIP_DEBUGF(SIO_DEBUG, ("sio_write()...\n"));
  while (dwNbBytesRemain > 0)
  {
    DWORD dwNbBytesWritten = 0;
    BOOL ret = WriteFile(fd->handle, data + (len - dwNbBytesRemain), dwNbBytesRemain, &dwNbBytesWritten, NULL);
    if (!ret) {
      while (!sio_reopen_win32(fd));
      break;
    }
    dwNbBytesRemain -= dwNbBytesWritten;
    LWIP_DEBUGF(SIO_DEBUG, ("sio_write()=%lu bytes -> %d\n", dwNbBytesWritten, ret));
  }
  return len - dwNbBytesRemain;
}

/**
 * Aborts a blocking sio_read() call.
 * @todo: This currently ignores fd and aborts all reads
 *
 * @param fd serial device handle
 */
void sio_read_abort(sio_fd_t _fd)
{
  LWIP_UNUSED_ARG(_fd);
  LWIP_DEBUGF(SIO_DEBUG, ("sio_read_abort() !!!!!...\n"));
  sio_abort = 1;
  return;
}

u8_t sio_reconnected(sio_fd_t _fd)
{
  sio_win32_fd_t *fd = (sio_win32_fd_t *)(_fd);
  if (fd->reconnected)
  {
    fd->reconnected = 0;
    return 1;
  }
  return 0;
}

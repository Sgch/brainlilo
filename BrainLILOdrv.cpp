/*
 * BrainLILODrv
 * U-Boot loader for electric dictionary.
 * 
 * Copyright (C) 2019 C. Shirasaka <holly_programmer@outlook.com>
 * based on
 ** ResetKitHelper
 ** Soft/hard reset the electronic dictionary.
 ** 
 ** Copyright (C) 2012 T. Kawada <tcppjp [ at ] gmail.com>
 *
 * This file is licensed in MIT license.
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the "Software"), 
 * to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or 
 * sell copies of the Software, and to permit persons to whom the Software is 
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in 
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
 * THE SOFTWARE.
 *
 */
#include <windows.h>
#include <stdint.h>

#define FSNOTIFY_POWER_OFF      1
#define FSNOTIFY_POWER_ON       0


#define BRAINLILODRV_API __declspec(dllexport)

#include "BrainLILODrv.h"

#define FILE_DEVICE_POWER   FILE_DEVICE_ACPI    

#define IOCTL_POWER_CAPABILITIES    \
CTL_CODE(FILE_DEVICE_POWER, 0x400, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_POWER_GET             \
CTL_CODE(FILE_DEVICE_POWER, 0x401, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_POWER_SET             \
CTL_CODE(FILE_DEVICE_POWER, 0x402, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_POWER_QUERY           \
CTL_CODE(FILE_DEVICE_POWER, 0x403, METHOD_BUFFERED, FILE_ANY_ACCESS)


typedef BOOL (*KernelIoControlProc)(DWORD dwIoControlCode, LPVOID lpInBuf,
						DWORD nInBufSize, LPVOID lpOutBuf, DWORD nOutBufSize,LPDWORD lpBytesReturned);
static KernelIoControlProc KernelIoControl;


typedef LARGE_INTEGER PHYSICAL_ADDRESS, *PPHYSICAL_ADDRESS;

typedef PVOID (*MmMapIoSpaceProc)(PHYSICAL_ADDRESS, ULONG, BOOL);
static MmMapIoSpaceProc MmMapIoSpace;

typedef void (*FileSystemPowerFunctionProc)(DWORD);
static FileSystemPowerFunctionProc FileSystemPowerFunction;

typedef LPVOID (*AllocPhysMemProc)(DWORD,DWORD,DWORD,DWORD,PULONG);

typedef void (*NKForceCleanBootProc)(BOOL);

static void disableInterrupts(){
    asm volatile("mrs	r0, cpsr\n"
        "orr	r0,r0,#0x80\n"
        "msr	cpsr_c,r0\n"
        "mov	r0,#1":::"r0");
}

static void EDNA2_physicalInvoker(){
	// r0-r7=params
	// r8=proc address
	asm volatile("nop\n" // who cares interrupt vectors?
				 "nop\n"
				 "nop\n"
				 "nop\n"
				 "nop\n"
				 "nop\n"
				 "nop\n"
				 "nop\n"
				 "nop\n"
				 "nop\n"
				 "msr	cpsr_c, #211\n"	// to supervisor mode
				 "mov	r9, #0\n"
				 "mcr	p15,0,r9,c13,c0,0\n" // clear fcse PID
				 "mrc	p15,0,r9,c1,c0,0\n" // read ctrl regs
				 "bic	r9, r9, #5\n" // disable MMU/DCache
				 "bic	r9, r9, #4096\n" // disable ICache
				 "orr	r9, r9, #8192\n" // and reset vectors to upper
				 "mcr	p15,0,r9,c1,c0,0\n" // write ctrl regs
				 "mov	r9, #0\n"
				 "mcr	p15,0,r9,c7,c7,0\n" // invalidate cache
				 "mcr	p15,0,r9,c8,c7,0\n" // invalidate tlb
				 "mov	pc, r8\n"
				 "nop\n"
				 "nop\n"
				 );
}


static void EDNA2_installPhysicalInvoker(){
	void *ptr=(void *)0xa8000000;
	wchar_t buf[256];
	swprintf(buf, L"ResetKit: copying to 0x%08x from 0x%08x\n",
			 (int)(ptr), (int)(&EDNA2_physicalInvoker));
	OutputDebugString(buf);
	memcpy(ptr, (const void *)&EDNA2_physicalInvoker, 64*4);
	//clearCache();
}


__attribute__((noreturn))
static void EDNA2_runPhysicalInvoker(unsigned long bootloaderphysaddr,DWORD size){
	// r0=info
	asm volatile("msr	cpsr_c, #211\n" // to supervisor mode
				 "mrc	p15,0,r0,c1,c0,0\n" // read ctrl regs
				 "bic	r0, r0, #8192\n" // reset vector to lower
				 "mcr	p15,0,r0,c1,c0,0\n" // write ctrl regs
				 
				 "mrc	p15,0,r10,c1,c0,0\n" // read ctrl regs
				 "bic	r10, r10, #5\n" // disable MMU/DCache
				 "mcr	p15,0,r10,c1,c0,0\n" // write ctrl regs
	);
	for(unsigned int i=0;i<size;i++)*((char *)(0x40002000+i))=*((char *)(bootloaderphysaddr+i));
	asm volatile("ldr	r0, =0x0000\n"
				 "ldr	r1, =0x0000\n"
				 "ldr	r2, =0x0000\n"
				 "ldr	r3, =0x0000\n"
				 "ldr	r4, =0x0000\n"
				 "ldr	r5, =0x0000\n"
				 "ldr	r6, =0x0000\n"
				 "ldr	r7, =0x0000\n"
				 "ldr	r8, =0x40002000\n"
				 "ldr	r9, =0x0000\n"
				 "swi	#0\n" // jump!
                 );
	
	// never reach here
	while(true);
}

__attribute__((noreturn))
static DWORD EDNA2_callKernelEntryPoint(unsigned long bootloaderphysaddr,DWORD size){
	OutputDebugString(L"BrainLILO: disabling interrupts");
    disableInterrupts();
	OutputDebugString(L"BrainLILO: injecting code to internal ram");
	EDNA2_installPhysicalInvoker();
	OutputDebugString(L"BrainLILO: invoking");
	Sleep(100);
	EDNA2_runPhysicalInvoker(bootloaderphysaddr,size);
}

static bool doLinux(){
	char *bootloaderdata;
	TCHAR bootloaderFileName[128]=TEXT("\\Storage Card\\loader\\u-boot.bin");
	HANDLE hFile;
	DWORD wReadSize;
	unsigned long bootloaderphysaddr;
	PULONG bootloaderptr;
	HINSTANCE dll;
	AllocPhysMemProc AllocPhysMem;

	dll=LoadLibrary(TEXT("COREDLL.DLL"));
	if (dll == NULL) {
		OutputDebugString(L"Cant load DLL");
		return false;
	}
	AllocPhysMem=(AllocPhysMemProc)GetProcAddress(dll,TEXT("AllocPhysMem"));
	if (AllocPhysMem == NULL) {
		OutputDebugString(L"Cant load AllocPhysMem function");
		return false;
	}
	
	OutputDebugString(L"BrainLILO: loading bootloader.");
	hFile = CreateFile(bootloaderFileName , GENERIC_READ , 0 , NULL ,OPEN_EXISTING , FILE_ATTRIBUTE_NORMAL , NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		OutputDebugString(L"Cant load bootloader");
		return false;
	}
	bootloaderdata  = (char *)malloc(GetFileSize(hFile , NULL));
	ReadFile(hFile , bootloaderdata , GetFileSize(hFile , NULL) , &wReadSize , NULL);
	CloseHandle(hFile);

	bootloaderptr=(PULONG)AllocPhysMem(wReadSize,PAGE_EXECUTE_READWRITE,0,0,&bootloaderphysaddr);
	wchar_t buf[256];
	swprintf(buf, L"BrainLILO: copying bootloader to 0x%08x from 0x%08x\n",(int)(bootloaderptr), (int)(bootloaderdata));
	OutputDebugString(buf);
	memcpy(bootloaderptr,bootloaderdata,wReadSize);
	OutputDebugString(L"BrainLILO: bootloader copied");
	free(bootloaderdata);
	FreeLibrary(dll);
	EDNA2_callKernelEntryPoint(bootloaderphysaddr,wReadSize);
	return true;
}

extern "C" BRAINLILODRV_API BOOL LIN_IOControl(DWORD handle, DWORD dwIoControlCode, DWORD *pInBuf, DWORD nInBufSize, DWORD * pOutBuf, DWORD nOutBufSize, 
                                           PDWORD pBytesReturned){
    SetLastError(0);
    
    switch(dwIoControlCode){
        case IOCTL_LIN_DO_LINUX:
            if(FileSystemPowerFunction)
                FileSystemPowerFunction(FSNOTIFY_POWER_OFF);
            if(!doLinux()){
                if(FileSystemPowerFunction)
                    FileSystemPowerFunction(FSNOTIFY_POWER_ON);
                return FALSE;
            }
            
            return TRUE;
    }
    return FALSE;
}

extern "C" BRAINLILODRV_API BOOL LIN_Read(DWORD handle, LPVOID pBuffer, DWORD dwNumBytes){
    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
}

extern "C" BRAINLILODRV_API BOOL LIN_Write(DWORD handle, LPVOID pBuffer, DWORD dwNumBytes){
    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
}

extern "C" BRAINLILODRV_API DWORD LIN_Seek(DWORD handle, long lDistance, DWORD dwMoveMethod){
    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
}


extern "C" BRAINLILODRV_API void LIN_PowerUp(void){
	OutputDebugString(L"BrainLILO: resuming.");
	
}


extern "C" BRAINLILODRV_API void LIN_PowerDown(void){
	
}


extern "C" BRAINLILODRV_API DWORD LIN_Init(LPCTSTR pContext,
									   DWORD dwBusContext){
    
	void *ctx;
	ctx=(void *)LocalAlloc(LPTR, sizeof(4));
	
	
	
	return (DWORD)ctx;
}


extern "C" BRAINLILODRV_API DWORD LIN_Open(DWORD dwData, DWORD dwAccess, DWORD dwShareMode){
	
	void *hnd=(void *)LocalAlloc(LPTR, 4);
	return (DWORD)hnd;
}

extern "C" BRAINLILODRV_API BOOL LIN_Close(DWORD handle){
	LocalFree((void *)handle);
	
	return TRUE;
}

extern "C" BRAINLILODRV_API BOOL LIN_Deinit(DWORD dwContext){
	
	LocalFree((void *)dwContext);
	return TRUE;
}

extern "C" BOOL APIENTRY DllMainCRTStartup( HANDLE hModule, 
                                           DWORD  ul_reason_for_call, 
                                           LPVOID lpReserved
                                           )
{
    switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
			KernelIoControl=(KernelIoControlProc)
			GetProcAddress(LoadLibrary(L"COREDLL"),
						   L"KernelIoControl");
			
			MmMapIoSpace=(MmMapIoSpaceProc)
			GetProcAddress(LoadLibrary(L"CEDDK"),
						   L"MmMapIoSpace");
            
            FileSystemPowerFunction=(FileSystemPowerFunctionProc)
			GetProcAddress(LoadLibrary(L"COREDLL"),
						   L"FileSystemPowerFunction");
            
		case DLL_THREAD_DETACH:
		case DLL_PROCESS_DETACH:
			break;
    }
    return TRUE;
}


#pragma  once	  
#include <stdio.h> 
#include <windows.h>
#include <MMReg.h>
	    
struct CWaveSaver
{
	FILE* m_fp;
	int m_nTotalDataLen;
	int m_nCurrentDataLen;
};

boolean Open(struct CWaveSaver *obj, const char* pFile, const WAVEFORMATEX* pWaveFormat, const int nDurationSeconds);
boolean WriteData(struct CWaveSaver* obj, BYTE* pData, const int nLen);
void Close(struct CWaveSaver* obj);
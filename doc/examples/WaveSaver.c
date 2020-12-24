#include "WaveSaver.h"  
#include <assert.h>

boolean Open(struct CWaveSaver* obj, const char* pFile, const WAVEFORMATEX* pWaveFormat, const int nDurationSeconds)
{
	Close(obj);

	fopen_s(&obj->m_fp, pFile, "wb+");
	assert(obj->m_fp);
	if (!obj->m_fp)
		return 0;

	assert(nDurationSeconds > 0);
	assert(pWaveFormat->wFormatTag == WAVE_FORMAT_PCM);
	assert(pWaveFormat->wBitsPerSample == 16 || pWaveFormat->wBitsPerSample == 8);

	obj->m_nCurrentDataLen = 0;
	obj->m_nTotalDataLen = nDurationSeconds * pWaveFormat->nAvgBytesPerSec;

	static DWORD s_dwWaveHeaderSize = 38;
	static DWORD s_dwWaveFormatSize = 18;	 // sizeof(WAVEFORMATEX), WAVEFORMATEX must Align with 1 BYTE
	static BYTE riff[4] = { 'R', 'I', 'F', 'F' };
	static BYTE wave[4] = { 'W', 'A', 'V', 'E' };
	static BYTE fmt[4] = { 'f', 'm', 't', 32 };
	static BYTE data[4] = { 'd', 'a', 't', 'a' };
	unsigned int uTotalLen = (obj->m_nTotalDataLen + s_dwWaveHeaderSize);

	fwrite(riff, 1, 4, obj->m_fp);
	fwrite(&uTotalLen, 1, sizeof(uTotalLen), obj->m_fp);
	fwrite(wave, 1, 4, obj->m_fp);
	fwrite(fmt, 1, 4, obj->m_fp);

	fwrite(&s_dwWaveFormatSize, 1, sizeof(s_dwWaveFormatSize), obj->m_fp);
	fwrite(&(pWaveFormat->wFormatTag), 1, sizeof(pWaveFormat->wFormatTag), obj->m_fp); // set with WAVE_FORMAT_PCM
	fwrite(&(pWaveFormat->nChannels), 1, sizeof(pWaveFormat->nChannels), obj->m_fp);
	fwrite(&(pWaveFormat->nSamplesPerSec), 1, sizeof(pWaveFormat->nSamplesPerSec), obj->m_fp);
	fwrite(&(pWaveFormat->nAvgBytesPerSec), 1, sizeof(pWaveFormat->nAvgBytesPerSec), obj->m_fp);
	fwrite(&(pWaveFormat->nBlockAlign), 1, sizeof(pWaveFormat->nBlockAlign), obj->m_fp);
	fwrite(&(pWaveFormat->wBitsPerSample), 1, sizeof(pWaveFormat->wBitsPerSample), obj->m_fp);
	fwrite(&(pWaveFormat->cbSize), 1, sizeof(pWaveFormat->cbSize), obj->m_fp);
										   
	fwrite(data, 1, 4, obj->m_fp);
	fwrite(&obj->m_nTotalDataLen, 1, sizeof(obj->m_nTotalDataLen), obj->m_fp);

	return 1;
}

boolean WriteData(struct CWaveSaver* obj, BYTE* pData, const int nLen)
{
	if (!obj->m_fp || !pData || (nLen <= 0))
	{				   
		return 0;
	}

	int nRestLen = obj->m_nTotalDataLen - obj->m_nCurrentDataLen;
	if (nRestLen <= 0)
		return 0;

	int nCopyLen = nLen;
	if (nCopyLen > nRestLen)
		nCopyLen = nRestLen;

	fwrite(pData, 1, nCopyLen, obj->m_fp);
	obj->m_nCurrentDataLen += nLen;

	if (obj->m_nCurrentDataLen >= obj->m_nTotalDataLen)
		Close(obj);

	return 1;
}

void Close(struct CWaveSaver* obj)
{
	if (!obj->m_fp)
		return;

	int nRestLen = obj->m_nTotalDataLen - obj->m_nCurrentDataLen;
	if (nRestLen > 0)
	{
		BYTE data = 0; // silence
		for (int i = 0; i < nRestLen; ++i)
		{
			fwrite(&data, 1, sizeof(BYTE), obj->m_fp);   
		}
	}				    

	fclose(obj->m_fp);

	obj->m_fp = 0;
	obj->m_nTotalDataLen = 0;
	obj->m_nCurrentDataLen = 0;
}
					  
#include <streams.h>
#include <olectl.h>
#include <initguid.h>
#include <dvdmedia.h>

#include "virtual_cam.h"

#define DECLARE_PTR(type, ptr, expr) type* ptr = (type*)(expr);

CUnknown* WINAPI CVCam::CreateInstance(LPUNKNOWN lpunk, HRESULT* phr)
{
	ASSERT(phr);
	return new CVCam(lpunk, phr, CLSID_VirtualCam);
}

CVCam::CVCam(LPUNKNOWN lpunk, HRESULT* phr, const GUID id)
	: CSource(NAME("FunectVirtualCam"), lpunk, id)
{
	ASSERT(phr);

	CAutoLock cAutoLock(&m_cStateLock);
	m_paStreams = (CSourceStream**) new CVCamStream * [1];
	m_stream = new CVCamStream(phr, this, L"Video");
	m_paStreams[0] = m_stream;
}

HRESULT CVCam::NonDelegatingQueryInterface(REFIID riid, void** ppv) 
{
	if (riid == _uuidof(IAMStreamConfig) || riid == _uuidof(IKsPropertySet)) {
		return m_paStreams[0]->QueryInterface(riid, ppv);
	}
	else {
		return CSource::NonDelegatingQueryInterface(riid, ppv);
	}
}

CVCamStream::CVCamStream(HRESULT* phr, CVCam* pParent, LPCWSTR pPinName)
	: CSourceStream(NAME("Video"), phr, pParent, pPinName), m_parent(pParent)
{}

CVCamStream::~CVCamStream()
{}

HRESULT CVCamStream::QueryInterface(REFIID riid, void** ppv)
{
	if (riid == _uuidof(IAMStreamConfig)) {
		*ppv = (IAMStreamConfig*)this;
	}
	else if (riid == _uuidof(IKsPropertySet)) {
		*ppv = (IKsPropertySet*)this;
	}
	else {
		return CSourceStream::QueryInterface(riid, ppv);
	}

	AddRef();
	return S_OK;
}

STDMETHODIMP CVCamStream::Notify(IBaseFilter*, Quality)
{
	return E_NOTIMPL;
}

HRESULT CVCamStream::SetMediaType(const CMediaType* pmt)
{
	DECLARE_PTR(VIDEOINFOHEADER, pvi, pmt->Format());
	HRESULT hr = CSourceStream::SetMediaType(pmt);
	return hr;
}

HRESULT CVCamStream::GetMediaType(int iPosition, CMediaType* pmt)
{
	if (iPosition != 0) {
		return E_INVALIDARG;
	}

	DECLARE_PTR(VIDEOINFOHEADER, pvi,
		pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER)));
	ZeroMemory(pvi, sizeof(VIDEOINFOHEADER));

	pvi->bmiHeader.biWidth = 1024;
	pvi->bmiHeader.biHeight = 768;
	pvi->AvgTimePerFrame = 333333;
	pvi->bmiHeader.biCompression = BI_RGB;
	pvi->bmiHeader.biBitCount = 32;
	pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pvi->bmiHeader.biPlanes = 1;
	pvi->bmiHeader.biSizeImage = GetBitmapSize(&pvi->bmiHeader);
	pvi->bmiHeader.biClrImportant = 0;

	SetRectEmpty(&(pvi->rcSource));
	SetRectEmpty(&(pvi->rcTarget));

	pmt->SetType(&MEDIATYPE_Video);
	pmt->SetFormatType(&FORMAT_VideoInfo);
	pmt->SetTemporalCompression(FALSE);

	const GUID subtypeGUID = GetBitmapSubtype(&pvi->bmiHeader);
	pmt->SetSubtype(&subtypeGUID);
	pmt->SetSampleSize(pvi->bmiHeader.biSizeImage);

	return NOERROR;
}

HRESULT CVCamStream::CheckMediaType(const CMediaType* pMediaType)
{
	if (!pMediaType) {
		return E_FAIL;
	}

	VIDEOINFOHEADER* pvi = (VIDEOINFOHEADER*)(pMediaType->Format());

	if (*pMediaType->Type() != MEDIATYPE_Video) {
		return E_INVALIDARG;
	}

	if (*pMediaType->FormatType() != FORMAT_VideoInfo) {
		return E_INVALIDARG;
	}

	if (*pMediaType->Subtype() != MEDIASUBTYPE_YUY2) {
		return E_INVALIDARG;
	}

	if (pvi->bmiHeader.biWidth == 1024 && pvi->bmiHeader.biHeight == 768) {
		return S_OK;
	}

	return E_INVALIDARG;
}

HRESULT CVCamStream::DecideBufferSize(IMemAllocator* pAlloc, 
	ALLOCATOR_PROPERTIES* pProperties) {
	CAutoLock cAutoLock(m_pFilter->pStateLock());

	VIDEOINFOHEADER* pvi = (VIDEOINFOHEADER*)m_mt.Format();
	pProperties->cBuffers = 1;
	pProperties->cbBuffer = pvi->bmiHeader.biSizeImage;

	ALLOCATOR_PROPERTIES actual;
	
	HRESULT hr = pAlloc->SetProperties(pProperties, &actual);
	if (FAILED(hr)) {
		return hr;
	}
	if (actual.cbBuffer < pProperties->cbBuffer) {
		return E_FAIL;
	}

	return NOERROR;
}

HRESULT CVCamStream::OnThreadCreate()
{
	return NOERROR;
}

HRESULT CVCamStream::OnThreadDestroy()
{
	return NOERROR;
}

HRESULT CVCamStream::FillBuffer(IMediaSample* pms)
{
	CheckPointer(pms, E_POINTER);
	
	BYTE* pData;
	long lDataLen;

	pms->GetPointer(&pData);
	lDataLen = pms->GetSize();

	ZeroMemory(pData, lDataLen);

	for (unsigned i = 0; i < 256; i++) {
		for (unsigned j = 0; j < 256; j++) {
			const unsigned idx = (i * 1024 + j) * 4;

			pData[idx] = i; // r
			pData[idx + 1] = j; // g
		}
	}

	{
		CAutoLock lock(&m_cSharedState);

		CRefTime start = m_rtSampleTime;
		m_rtSampleTime += 1000l;

		pms->SetTime((REFERENCE_TIME*) &start, (REFERENCE_TIME*) &m_rtSampleTime);
	}

	pms->SetSyncPoint(TRUE);
	
	return NOERROR;
}

HRESULT STDMETHODCALLTYPE CVCamStream::SetFormat(AM_MEDIA_TYPE* pmt)
{
	if (pmt == nullptr)
		return E_FAIL;

	if (m_parent->GetState() != State_Stopped)
		return E_FAIL;

	if (CheckMediaType((CMediaType*)pmt) != S_OK)
		return E_FAIL;

	VIDEOINFOHEADER* pvi = (VIDEOINFOHEADER*)(pmt->pbFormat);

	m_mt.SetFormat(m_mt.Format(), sizeof(VIDEOINFOHEADER));

	IPin* pin;
	ConnectedTo(&pin);
	if (pin) {
		IFilterGraph* pGraph = m_parent->GetGraph();
		pGraph->Reconnect(this);
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CVCamStream::GetFormat(AM_MEDIA_TYPE** ppmt)
{
	*ppmt = CreateMediaType(&m_mt);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CVCamStream::GetNumberOfCapabilities(int* piCount,
	int* piSize)
{
	*piCount = 1;
	*piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CVCamStream::GetStreamCaps(int iIndex,
	AM_MEDIA_TYPE** pmt, BYTE* pSCC)
{
	if (iIndex != 0) {
		return E_INVALIDARG;
	}

	*pmt = CreateMediaType(&m_mt);
	DECLARE_PTR(VIDEOINFOHEADER, pvi, (*pmt)->pbFormat);


	pvi->bmiHeader.biWidth = 1024;
	pvi->bmiHeader.biHeight = 768;
	pvi->AvgTimePerFrame = 333333;
	pvi->bmiHeader.biCompression = MAKEFOURCC('Y', 'U', 'Y', '2');
	pvi->bmiHeader.biBitCount = 16;
	pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pvi->bmiHeader.biPlanes = 1;
	pvi->bmiHeader.biSizeImage = pvi->bmiHeader.biWidth *
		pvi->bmiHeader.biHeight * 2;
	pvi->bmiHeader.biClrImportant = 0;

	SetRectEmpty(&(pvi->rcSource));
	SetRectEmpty(&(pvi->rcTarget));

	(*pmt)->majortype = MEDIATYPE_Video;
	(*pmt)->subtype = MEDIASUBTYPE_YUY2;
	(*pmt)->formattype = FORMAT_VideoInfo;
	(*pmt)->bTemporalCompression = FALSE;
	(*pmt)->bFixedSizeSamples = FALSE;
	(*pmt)->lSampleSize = pvi->bmiHeader.biSizeImage;
	(*pmt)->cbFormat = sizeof(VIDEOINFOHEADER);

	DECLARE_PTR(VIDEO_STREAM_CONFIG_CAPS, pvscc, pSCC);

	pvscc->guid = FORMAT_VideoInfo;
	pvscc->VideoStandard = AnalogVideo_None;
	pvscc->InputSize.cx = pvi->bmiHeader.biWidth;
	pvscc->InputSize.cy = pvi->bmiHeader.biHeight;
	pvscc->MinCroppingSize.cx = pvi->bmiHeader.biWidth;
	pvscc->MinCroppingSize.cy = pvi->bmiHeader.biHeight;
	pvscc->MaxCroppingSize.cx = pvi->bmiHeader.biWidth;
	pvscc->MaxCroppingSize.cy = pvi->bmiHeader.biHeight;
	pvscc->CropGranularityX = pvi->bmiHeader.biWidth;
	pvscc->CropGranularityY = pvi->bmiHeader.biHeight;
	pvscc->CropAlignX = 0;
	pvscc->CropAlignY = 0;

	pvscc->MinOutputSize.cx = pvi->bmiHeader.biWidth;
	pvscc->MinOutputSize.cy = pvi->bmiHeader.biHeight;
	pvscc->MaxOutputSize.cx = pvi->bmiHeader.biWidth;
	pvscc->MaxOutputSize.cy = pvi->bmiHeader.biHeight;
	pvscc->OutputGranularityX = 0;
	pvscc->OutputGranularityY = 0;
	pvscc->StretchTapsX = 0;
	pvscc->StretchTapsY = 0;
	pvscc->ShrinkTapsX = 0;
	pvscc->ShrinkTapsY = 0;
	pvscc->MinFrameInterval = pvi->AvgTimePerFrame;
	pvscc->MaxFrameInterval = pvi->AvgTimePerFrame;
	pvscc->MinBitsPerSecond = pvi->bmiHeader.biWidth * pvi->bmiHeader.biHeight
		* 2 * 8 * (10000000 / pvi->AvgTimePerFrame);
	pvscc->MaxBitsPerSecond = pvi->bmiHeader.biWidth * pvi->bmiHeader.biHeight
		* 2 * 8 * (10000000 / pvi->AvgTimePerFrame);

	return S_OK;
}

HRESULT CVCamStream::Set(REFGUID guidPropSet, DWORD dwID, void* pInstanceData,
	DWORD cbInstanceData, void* pPropData, DWORD cbPropData)
{
	return E_NOTIMPL;
}

HRESULT CVCamStream::Get(REFGUID guidPropSet, DWORD dwPropID,
	void* pInstanceData, DWORD cbInstanceData, void* pPropData,
	DWORD cbPropData, DWORD* pcbReturned)
{
	if (guidPropSet != AMPROPSETID_Pin)             return E_PROP_SET_UNSUPPORTED;
	if (dwPropID != AMPROPERTY_PIN_CATEGORY)        return E_PROP_ID_UNSUPPORTED;
	if (pPropData == NULL && pcbReturned == NULL)   return E_POINTER;

	if (pcbReturned) *pcbReturned = sizeof(GUID);
	if (pPropData == NULL)          return S_OK;
	if (cbPropData < sizeof(GUID))  return E_UNEXPECTED;

	*(GUID*)pPropData = PIN_CATEGORY_CAPTURE;
	return S_OK;
}

HRESULT CVCamStream::QuerySupported(REFGUID guidPropSet, DWORD dwPropID,
	DWORD* pTypeSupport)
{
	if (guidPropSet != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
	if (dwPropID != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
	if (pTypeSupport) *pTypeSupport = KSPROPERTY_SUPPORT_GET;
	return S_OK;
}
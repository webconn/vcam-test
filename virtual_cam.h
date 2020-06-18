#pragma once

// {f5529866-b085-11ea-b3de-0242ac130004}
DEFINE_GUID(CLSID_VirtualCam,
	0xf5529866, 0xb085, 0x11ea, 0xb3, 0xde, 0x02, 0x42, 0xac, 0x13, 0x00, 0x04);

class CVCamStream;

class CVCam : public CSource {
public:
	DECLARE_IUNKNOWN;

	static CUnknown* WINAPI CreateInstance(LPUNKNOWN lpunk, HRESULT* phr);

	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void** ppv);
	IFilterGraph* GetGraph() { return m_pGraph; }
	FILTER_STATE GetState() { return m_State; }

protected:
	CVCamStream* m_stream = nullptr;

private:
	CVCam(LPUNKNOWN lpunk, HRESULT* phr, const GUID id);
};


class CVCamStream : public CSourceStream, 
					public IAMStreamConfig,
					public IKsPropertySet {
public:
	CVCamStream(HRESULT* phr, CVCam* pParent, LPCWSTR pPinName);
	~CVCamStream();

	// IUnknown
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
	STDMETHODIMP_(ULONG) AddRef() { return GetOwner()->AddRef(); }
	STDMETHODIMP_(ULONG) Release() { return GetOwner()->Release(); }

	// IQualityControl
	STDMETHODIMP Notify(IBaseFilter* pSender, Quality q);

	// IAMStreamConfig
	HRESULT STDMETHODCALLTYPE SetFormat(AM_MEDIA_TYPE* pmt);
	HRESULT STDMETHODCALLTYPE GetFormat(AM_MEDIA_TYPE** pmt);
	HRESULT STDMETHODCALLTYPE GetNumberOfCapabilities(int* piCount, int* piSize);
	HRESULT STDMETHODCALLTYPE GetStreamCaps(int iIndex, AM_MEDIA_TYPE** pmt, BYTE* pSCC);

	// IKsPropertySet
	HRESULT STDMETHODCALLTYPE Set(REFGUID guidPropSet, DWORD dwID, void* pInstanceData,
		DWORD cbInstanceData, void* pPropData, DWORD cbPropData);
	HRESULT STDMETHODCALLTYPE Get(REFGUID guidPropSet, DWORD dwID, void* pInstanceData,
		DWORD cbInstanceData, void* pPropData, DWORD cbPropData, DWORD* pcbReturned);
	HRESULT STDMETHODCALLTYPE QuerySupported(REFGUID guidPropSet, DWORD dwPropId,
		DWORD* pTypeSupport);

	// CSourceStream
	HRESULT FillBuffer(IMediaSample* pms);
	HRESULT DecideBufferSize(IMemAllocator* pIMemAlloc, ALLOCATOR_PROPERTIES* pProperties);
	HRESULT CheckMediaType(const CMediaType* pMediaType);
	HRESULT GetMediaType(int iPosition, CMediaType* pmt);
	HRESULT SetMediaType(const CMediaType* pmt);
	HRESULT OnThreadCreate();
	HRESULT OnThreadDestroy();

private:
	CVCam* m_parent;

	CCritSec m_cSharedState;
	CRefTime m_rtSampleTime;
};


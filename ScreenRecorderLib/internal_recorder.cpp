#include <ppltasks.h>
#include <chrono>
#include <ctime>   // localtime
#include <sstream> // stringstream
#include <iomanip> // put_time
#include <string>
#include <comdef.h>
#include <queue>
#include <atlbase.h>
#include <wrl.h>
#include <ScreenGrab.h>
#include <winnt.h>
#include <concrt.h>
#include <wincodec.h>
#include "mouse_pointer.h"
#include "loopback_capture.h"
#include "internal_recorder.h"
#include "audio_prefs.h"
#include "log.h"
#include "utilities.h"
#include "cleanup.h"

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Mfuuid.lib")
#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "evr.lib")
#pragma comment(lib, "mfreadwrite.lib")

using namespace std;
using namespace std::chrono;
using namespace concurrency;
using namespace DirectX;

#define RETURN_ON_BAD_HR(expr) \
{ \
    HRESULT _hr_ = (expr); \
    if (FAILED(_hr_)) { \
	{\
		_com_error err(_hr_);\
		ERR(L"RETURN_ON_BAD_HR: error is %ls", err.ErrorMessage());\
	}\
		return _hr_; \
	} \
}

template <class T> void SafeRelease(T **ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}

// Driver types supported
D3D_DRIVER_TYPE gDriverTypes[] =
{
	D3D_DRIVER_TYPE_HARDWARE
};
UINT m_NumDriverTypes = ARRAYSIZE(gDriverTypes);

// Feature levels supported
D3D_FEATURE_LEVEL m_FeatureLevels[] =
{
	D3D_FEATURE_LEVEL_11_0,
	D3D_FEATURE_LEVEL_10_1,
	D3D_FEATURE_LEVEL_10_0,
	D3D_FEATURE_LEVEL_9_1
};
#if _DEBUG 
CComPtr<ID3D11Debug> m_Debug;
#endif
CComPtr<ID3D11DeviceContext> pImmediateContext;
CComPtr<IMFSinkWriter> m_SinkWriter;
queue<FrameWriteModel*> m_WriteQueue;
task<void> m_FrameWriteTask;
high_resolution_clock::time_point m_LastFrame;
cancellation_token_source m_RecordTaskCts;
cancellation_token_source m_RenderTaskCts;

bool m_IsDestructed;
UINT32 m_RecorderMode;
bool m_IsRecording;
bool m_IsPaused;
UINT m_NumFeatureLevels = ARRAYSIZE(m_FeatureLevels);

DWORD m_VideoStreamIndex;
DWORD m_AudioStreamIndex;
UINT m_DisplayOutput = 0; //Display output, where 0 is primary display.
RECT m_DestRect;
wstring m_OutputFolder;
wstring m_OutputFullPath;
nlohmann::fifo_map<wstring, int> m_FrameDelays;

UINT32 m_VideoFps = 30;
UINT32 m_VideoBitrate = 4000 * 1000;//Bitrate in bits per second
UINT32 m_AudioBitrate = (96 / 8) * 1000; //Bitrate in bytes per second. Only 96,128,160 and 192kbps is supported.
UINT32 m_AudioChannels = 2; //Number of audio channels. 1,2 and 6 is supported. 6 only on windows 8 and up.
bool m_IsMousePointerEnabled = true;
bool m_IsAudioEnabled = false;
bool m_IsFixedFramerate = false;

// Format constants
const GUID   VIDEO_ENCODING_FORMAT = MFVideoFormat_H264;
const GUID	 AUDIO_ENCODING_FORMAT = MFAudioFormat_AAC;
const UINT32 AUDIO_BITS_PER_SAMPLE = 16; //Audio bits per sample must be 16.
const UINT32 AUDIO_SAMPLES_PER_SECOND = 44100;//Audio samples per seconds must be 44100.
const GUID   VIDEO_INPUT_FORMAT = MFVideoFormat_ARGB32;
const GUID   IMAGE_ENCODER_FORMAT = GUID_ContainerFormatPng;

internal_recorder::internal_recorder()
{
	m_FrameWriteTask = concurrency::create_task([]() {});
	m_IsDestructed = false;
}


internal_recorder::~internal_recorder()
{
	if (m_IsRecording) {
		//m_IsRecording = false;
		if (RecordingStatusChangedCallback != NULL)
			RecordingStatusChangedCallback(STATUS_IDLE);
		m_RecordTaskCts.cancel();
	}
	m_IsDestructed = true;
	//m_RenderTaskCts.cancel();
}
void internal_recorder::SetVideoFps(UINT32 fps)
{
	m_VideoFps = fps;
}
void internal_recorder::SetVideoBitrate(UINT32 bitrate)
{
	m_VideoBitrate = bitrate;
}
void internal_recorder::SetAudioBitrate(UINT32 bitrate)
{
	m_AudioBitrate = bitrate;
}
void internal_recorder::SetAudioChannels(UINT32 channels)
{
	m_AudioChannels = channels;
}
void internal_recorder::SetAudioEnabled(bool enabled)
{
	m_IsAudioEnabled = enabled;
}
void internal_recorder::SetMousePointerEnabled(bool enabled)
{
	m_IsMousePointerEnabled = enabled;
}
void internal_recorder::SetDestRectangle(RECT rect)
{
	if (rect.left % 2 != 0)
		rect.left += 1;
	if (rect.top % 2 != 0)
		rect.top += 1;
	if (rect.right % 2 != 0)
		rect.right += 1;
	if (rect.bottom % 2 != 0)
		rect.bottom += 1;
	m_DestRect = rect;
}
void internal_recorder::SetDisplayOutput(UINT32 output)
{
	m_DisplayOutput = output;
}
void internal_recorder::SetRecorderMode(UINT32 mode)
{
	m_RecorderMode = mode;
}
void internal_recorder::SetFixedFramerate(bool enabled)
{
	m_IsFixedFramerate = enabled;
}
HRESULT internal_recorder::BeginRecording(std::wstring path) {
	if (m_IsRecording) {
		if (m_IsPaused) {
			m_IsPaused = false;
			if (RecordingStatusChangedCallback != NULL)
				RecordingStatusChangedCallback(STATUS_RECORDING);
		}
		return S_FALSE;
	}
	m_FrameDelays.clear();
	m_OutputFullPath = path;
	wstring dir = path;
	LPWSTR directory = (LPWSTR)dir.c_str();
	PathRemoveFileSpecW(directory);

	if (utilities::CreateAllDirs(directory))
	{
		LOG(L"output folder is ready");
		m_OutputFolder = directory;
	}
	else
	{
		// Failed to create directory.
		ERR(L"failed to create temp folder");
		if (RecordingFailedCallback != NULL)
			RecordingFailedCallback(L"failed to create temp folder");
		return S_FALSE;
	}
	if (m_RecorderMode == MODE_VIDEO || m_RecorderMode == MODE_SNAPSHOT) {
		wstring ext = m_RecorderMode == MODE_VIDEO ? L".mp4" : L".png";
		LPWSTR pStrExtension = PathFindExtension(path.c_str());
		if (pStrExtension == NULL || pStrExtension[0] == 0)
		{
			m_OutputFullPath = m_OutputFolder + L"\\" + utilities::s2ws(NowToString()) + ext;
		}
	}
	cancellation_token token = m_RecordTaskCts.get_token();
	concurrency::create_task([this, token]() {
		HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		hr = MFStartup(MF_VERSION);

		RETURN_ON_BAD_HR(hr);
		{
			DXGI_OUTDUPL_DESC OutputDuplDesc;
			CComPtr<ID3D11Device> pDevice;
			CComPtr<IDXGIOutputDuplication> pDeskDupl;
			std::unique_ptr<mouse_pointer> pMousePointer = make_unique<mouse_pointer>();
			std::unique_ptr<loopback_capture> pLoopbackCapture = make_unique<loopback_capture>();

			hr = InitializeDx(&pImmediateContext, &pDevice, &pDeskDupl, &OutputDuplDesc);
			RETURN_ON_BAD_HR(hr);
			RECT SourceRect;
			SourceRect.left = 0;
			SourceRect.right = OutputDuplDesc.ModeDesc.Width;
			SourceRect.top = 0;
			SourceRect.bottom = OutputDuplDesc.ModeDesc.Height;

			RECT DestRect = SourceRect;
			if (m_DestRect.right != 0
				|| m_DestRect.top != 0
				|| m_DestRect.bottom != 0
				|| m_DestRect.left != 0)
			{
				DestRect = m_DestRect;
			}


			if (m_RecorderMode == MODE_VIDEO) {
				hr = InitializeVideoSinkWriter(m_OutputFullPath, pDevice, SourceRect, DestRect, &m_SinkWriter, &m_VideoStreamIndex, &m_AudioStreamIndex);
				RETURN_ON_BAD_HR(hr);
			}
			m_IsRecording = true;
			if (RecordingStatusChangedCallback != NULL)
				RecordingStatusChangedCallback(STATUS_RECORDING);

			ULONGLONG lastFrameStartPos = 0;
			m_LastFrame = std::chrono::high_resolution_clock::now();
			hr = pMousePointer->Initialize(pImmediateContext, pDevice);
			SetViewPort(pImmediateContext, OutputDuplDesc.ModeDesc.Width, OutputDuplDesc.ModeDesc.Height);
			RETURN_ON_BAD_HR(hr);

			// create a "loopback capture has started" event
			HANDLE hStartedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
			if (NULL == hStartedEvent) {
				ERR(L"CreateEvent failed: last error is %u", GetLastError());
				return S_FALSE;
			}
			CloseHandleOnExit closeStartedEvent(hStartedEvent);

			// create a "stop capturing now" event
			HANDLE hStopEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
			if (NULL == hStopEvent) {
				ERR(L"CreateEvent failed: last error is %u", GetLastError());
				return S_FALSE;
			}
			CloseHandleOnExit closeStopEvent(hStopEvent);
			bool recordAudio = m_RecorderMode == MODE_VIDEO && m_IsAudioEnabled;
			if (recordAudio)
			{
				CPrefs prefs(1, NULL, hr);
				prefs.m_bInt16 = true;
				// create arguments for loopback capture thread
				LoopbackCaptureThreadFunctionArguments threadArgs;
				threadArgs.hr = E_UNEXPECTED; // thread will overwrite this
				threadArgs.pMMDevice = prefs.m_pMMDevice;
				threadArgs.bInt16 = prefs.m_bInt16;
				threadArgs.hFile = prefs.m_hFile;
				threadArgs.hStartedEvent = hStartedEvent;
				threadArgs.hStopEvent = hStopEvent;
				threadArgs.nFrames = 0;

				HANDLE hThread = CreateThread(
					NULL, 0,
					LoopbackCaptureThreadFunction, &threadArgs, 0, NULL
				);
				if (NULL == hThread) {
					ERR(L"CreateThread failed: last error is %u", GetLastError());
					return S_FALSE;
				}
				CloseHandleOnExit closeThread(hThread);

				WaitForSingleObjectEx(hStartedEvent, 1000, false);
			}
			UINT64 VideoFrameDurationMillis = 1000 / m_VideoFps;
			UINT64 VideoFrameDuration100Nanos = VideoFrameDurationMillis * 10 * 1000;
			UINT FrameTimeout = 99;
			int frameNr = 0;
			CComPtr<ID3D11Texture2D> pPreviousFrameCopy = nullptr;
			if (m_IsFixedFramerate) {
				FrameTimeout = 0;
			}


			while (m_IsRecording)
			{
				if (token.is_canceled()) {
					break;
				}
				IDXGIResource *pDesktopResource = nullptr;
				DXGI_OUTDUPL_FRAME_INFO FrameInfo;
				RtlZeroMemory(&FrameInfo, sizeof(FrameInfo));
				// Get new frame
				hr = pDeskDupl->AcquireNextFrame(
					FrameTimeout,
					&FrameInfo,
					&pDesktopResource);

				if (token.is_canceled()) {
					pDeskDupl->ReleaseFrame();
					SafeRelease(&pDesktopResource);
					break;
				}
				if (hr == DXGI_ERROR_ACCESS_LOST
					|| hr == DXGI_ERROR_INVALID_CALL) {
					pDeskDupl->ReleaseFrame();
					pDeskDupl.Release();
					pDeskDupl = NULL;
					SafeRelease(&pDesktopResource);
					{
						_com_error err(hr);
						ERR(L"AcquireNextFrame error hresult %s\n", err.ErrorMessage());
					}

					hr = InitializeDesktopDupl(pDevice, &pDeskDupl, &OutputDuplDesc);
					{
						_com_error err(hr);
						ERR(L"Reinitialized desktop duplication hresult %s\n", err.ErrorMessage());
					}

					RETURN_ON_BAD_HR(hr);
					//wait(1);
					continue;
				}

				if (m_RecorderMode == MODE_SLIDESHOW
					|| m_RecorderMode == MODE_SNAPSHOT) {

					if (frameNr == 0 && FrameInfo.AccumulatedFrames == 0) {
						pDeskDupl->ReleaseFrame();
						SafeRelease(&pDesktopResource);
						continue;
					}
				}

				if (!m_IsRecording)
				{
					pDeskDupl->ReleaseFrame();
					SafeRelease(&pDesktopResource);
					break;
				}
				if (m_IsPaused) {
					pDeskDupl->ReleaseFrame();
					wait(1);
					m_LastFrame = high_resolution_clock::now();
					pLoopbackCapture->ClearRecordedBytes();
					SafeRelease(&pDesktopResource);
					continue;
				}
				UINT64 durationSinceLastFrameMillis = duration_cast<milliseconds>(chrono::high_resolution_clock::now() - m_LastFrame).count();
				if (frameNr > 0 //always draw first frame 
					&& !m_IsFixedFramerate
					&& (!m_IsMousePointerEnabled || FrameInfo.PointerShapeBufferSize == 0)//always redraw when pointer changes if we draw pointer
					&& (hr == DXGI_ERROR_WAIT_TIMEOUT || durationSinceLastFrameMillis < VideoFrameDurationMillis)) //skip if frame timeouted or duration is under our chosen framerate
				{
					if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
						LOG(L"Skipped frame");
					}
					pDeskDupl->ReleaseFrame();
					SafeRelease(&pDesktopResource);
					continue;
				}
				if (m_IsFixedFramerate && hr == DXGI_ERROR_WAIT_TIMEOUT) {
					hr = S_OK;
				}
				else {
					if (pPreviousFrameCopy) {
						pPreviousFrameCopy.Release();
						pPreviousFrameCopy = nullptr;
					}
				}
				if (FAILED(hr)) {
					SafeRelease(&pDesktopResource);
					return hr;
				}

				m_LastFrame = high_resolution_clock::now();
				{
					CComPtr<ID3D11Texture2D> pFrameCopy;
					D3D11_TEXTURE2D_DESC desc;

					desc.Width = OutputDuplDesc.ModeDesc.Width;
					desc.Height = OutputDuplDesc.ModeDesc.Height;
					desc.Format = OutputDuplDesc.ModeDesc.Format;
					desc.ArraySize = 1;
					desc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_RENDER_TARGET;
					desc.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
					desc.SampleDesc.Count = 1;
					desc.SampleDesc.Quality = 0;
					desc.MipLevels = 1;
					desc.CPUAccessFlags = 0;
					desc.Usage = D3D11_USAGE_DEFAULT;

					hr = pDevice->CreateTexture2D(&desc, NULL, &pFrameCopy);
					if (FAILED(hr))
					{
						pDeskDupl->ReleaseFrame();
						SafeRelease(&pDesktopResource);
						return hr;
					}

					if (pPreviousFrameCopy) {
						pImmediateContext->CopyResource(pFrameCopy, pPreviousFrameCopy);
					}
					else {
						CComPtr<ID3D11Texture2D> pAcquiredDesktopImage = nullptr;
						hr = pDesktopResource->QueryInterface(IID_PPV_ARGS(&pAcquiredDesktopImage));
						if (FAILED(hr)) {
							pDeskDupl->ReleaseFrame();
							SafeRelease(&pDesktopResource);
							return hr;
						}
						SetDebugName(pAcquiredDesktopImage, "FrameCopy");
						pImmediateContext->CopyResource(pFrameCopy, pAcquiredDesktopImage);
					}

					SetDebugName(pFrameCopy, "FrameCopy");

					if (m_IsFixedFramerate && pPreviousFrameCopy == nullptr) {
						hr = pDevice->CreateTexture2D(&desc, NULL, &pPreviousFrameCopy);
						if (FAILED(hr))
						{
							pDeskDupl->ReleaseFrame();
							SafeRelease(&pDesktopResource);
							return hr;
						}
						pImmediateContext->CopyResource(pPreviousFrameCopy, pFrameCopy);
						SetDebugName(pPreviousFrameCopy, "PreviousFrameCopy");
					}

					if (m_IsMousePointerEnabled) {
						hr = pMousePointer->DrawMousePointer(pImmediateContext, pDevice, FrameInfo, SourceRect, desc, pDeskDupl, pFrameCopy);
						if (hr == DXGI_ERROR_ACCESS_LOST) {
							pDeskDupl->ReleaseFrame();
							hr = InitializeDesktopDupl(pDevice, &pDeskDupl, &OutputDuplDesc);
							{
								_com_error err(hr);
								LOG(L"Reinitialized desktop duplication hresult %s\n", err.ErrorMessage());
							}

							if (SUCCEEDED(hr)) {
								wait(1);
								continue;
							}
						}
						if (FAILED(hr)) {
							pDeskDupl->ReleaseFrame();
							SafeRelease(&pDesktopResource);
							return hr;
						}
					}
					ULONGLONG duration = VideoFrameDuration100Nanos;
					if (durationSinceLastFrameMillis > 0) {
						duration = (ULONGLONG)(durationSinceLastFrameMillis * 10 * 1000);
					}
					if (token.is_canceled()) {
						return S_FALSE;
					}
					if (m_IsRecording) {
						if (m_RecorderMode == MODE_VIDEO || m_RecorderMode == MODE_SLIDESHOW) {
							FrameWriteModel *model = new FrameWriteModel();
							model->Frame = pFrameCopy;
							model->Duration = duration;
							model->StartPos = lastFrameStartPos;
							if (recordAudio) {
								std::vector<BYTE> vec = pLoopbackCapture->GetRecordedBytes();
								model->Audio = vec;
							}
							model->FrameNumber = frameNr;
							EnqueueFrame(model);
							frameNr++;
						}
						else if (m_RecorderMode == MODE_SNAPSHOT) {
							hr = WriteFrameToImage(pFrameCopy, m_OutputFullPath.c_str());
							m_IsRecording = false;
						}
					}

					lastFrameStartPos += duration;
					pDeskDupl->ReleaseFrame();
					SafeRelease(&pDesktopResource);

					if (m_IsFixedFramerate)
					{
						wait(VideoFrameDurationMillis);
					}
				}
			}
			m_IsRecording = false;
			if (!m_IsDestructed) {
				if (RecordingStatusChangedCallback != NULL)
					RecordingStatusChangedCallback(STATUS_FINALIZING);
			}

			if (pPreviousFrameCopy)
				pPreviousFrameCopy.Release();
			if (pMousePointer) {
				pMousePointer->CleanupResources();
			}
		}
		return hr;
	}).then([this, token](HRESULT hr) {
		m_IsRecording = false;
		if (token.is_canceled()) {
			m_RenderTaskCts.cancel();
		}
		else {
			if (!m_FrameWriteTask.is_done())
				m_FrameWriteTask.wait();
			LOG(L"Recording completed!");
		}
		if (m_SinkWriter) {
			hr = m_SinkWriter->Finalize();
			if (m_SinkWriter)
				m_SinkWriter.Release();
		}
		if (pImmediateContext) {
			pImmediateContext.Release();
		}
		LOG(L"Finalized!");
		MFShutdown();
		CoUninitialize();
		LOG(L"MF shut down!");
#if _DEBUG
		if (m_Debug) {
			m_Debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
			m_Debug.Release();
	}
#endif
		if (!m_IsDestructed) {
			if (SUCCEEDED(hr)) {
				if (RecordingStatusChangedCallback != NULL)
					RecordingStatusChangedCallback(STATUS_IDLE);
				RecordingStatusChangedCallback = NULL;

				if (RecordingCompleteCallback != NULL)
					RecordingCompleteCallback(m_OutputFullPath, m_FrameDelays);
				RecordingCompleteCallback = NULL;
				RecordingStatusChangedCallback = NULL;
			}
			else {
				if (RecordingStatusChangedCallback != NULL)
					RecordingStatusChangedCallback(STATUS_IDLE);
				RecordingStatusChangedCallback = NULL;
				if (RecordingFailedCallback != NULL) {

					std::wstring errMsg;
					{
						_com_error err(hr);
						errMsg = err.ErrorMessage();
					}
					RecordingFailedCallback(errMsg);
					RecordingFailedCallback = NULL;
				}
			}
		}
		return hr;
});

	return S_OK;
}

void internal_recorder::EndRecording() {
	if (m_IsRecording) {
		m_IsRecording = false;
	}
}
void internal_recorder::PauseRecording() {
	if (m_IsRecording) {
		m_IsPaused = true;
		if (RecordingStatusChangedCallback != NULL)
			RecordingStatusChangedCallback(STATUS_PAUSED);
	}
}
void internal_recorder::ResumeRecording() {
	if (m_IsRecording) {
		if (m_IsPaused) {
			m_IsPaused = false;
			if (RecordingStatusChangedCallback != NULL)
				RecordingStatusChangedCallback(STATUS_RECORDING);
		}
	}
}

HRESULT internal_recorder::InitializeDx(ID3D11DeviceContext **ppContext, ID3D11Device **ppDevice, IDXGIOutputDuplication **ppDesktopDupl, DXGI_OUTDUPL_DESC *pOutputDuplDesc) {
	*ppContext = NULL;
	*ppDevice = NULL;
	*ppDesktopDupl = NULL;
	*pOutputDuplDesc;

	HRESULT hr(S_OK);
	DXGI_OUTDUPL_DESC OutputDuplDesc;
	CComPtr<ID3D11DeviceContext> pImmediateContext = NULL;
	CComPtr<ID3D11Device> pDevice = NULL;
	CComPtr<IDXGIOutputDuplication> pDeskDupl = NULL;
	int lresult(-1);
	D3D_FEATURE_LEVEL FeatureLevel;

	// Create device
	for (UINT DriverTypeIndex = 0; DriverTypeIndex < m_NumDriverTypes; ++DriverTypeIndex)
	{
		hr = D3D11CreateDevice(
			nullptr,
			gDriverTypes[DriverTypeIndex],
			nullptr,
#if _DEBUG 
			D3D11_CREATE_DEVICE_DEBUG,
#else
			0,
#endif
			m_FeatureLevels,
			m_NumFeatureLevels,
			D3D11_SDK_VERSION,
			&pDevice,
			&FeatureLevel,
			&pImmediateContext);

		if (SUCCEEDED(hr))
		{
#if _DEBUG 
			HRESULT hr = pDevice->QueryInterface(IID_PPV_ARGS(&m_Debug));
#endif
			// Device creation success, no need to loop anymore
			break;
	}
}

	RETURN_ON_BAD_HR(hr);
	CComPtr<ID3D10Multithread> pMulti = nullptr;
	hr = pImmediateContext->QueryInterface(IID_PPV_ARGS(&pMulti));
	RETURN_ON_BAD_HR(hr);
	hr = pMulti->SetMultithreadProtected(TRUE);
	RETURN_ON_BAD_HR(hr);
	pMulti.Release();
	if (pDevice == nullptr)
		return S_FALSE;
	hr = InitializeDesktopDupl(pDevice, &pDeskDupl, &OutputDuplDesc);

	// Return the pointer to the caller.
	*ppContext = pImmediateContext;
	(*ppContext)->AddRef();
	*ppDevice = pDevice;
	(*ppDevice)->AddRef();
	*ppDesktopDupl = pDeskDupl;
	(*ppDesktopDupl)->AddRef();
	*pOutputDuplDesc = OutputDuplDesc;

	return hr;
}

HRESULT internal_recorder::InitializeDesktopDupl(ID3D11Device *pDevice, IDXGIOutputDuplication **ppDesktopDupl, DXGI_OUTDUPL_DESC *pOutputDuplDesc) {
	*ppDesktopDupl = NULL;
	*pOutputDuplDesc;

	// Get DXGI device
	CComPtr<IDXGIDevice> pDxgiDevice;
	CComPtr<IDXGIOutputDuplication> pDeskDupl = NULL;
	DXGI_OUTDUPL_DESC OutputDuplDesc;

	HRESULT	hr = pDevice->QueryInterface(IID_PPV_ARGS(&pDxgiDevice));

	RETURN_ON_BAD_HR(hr);
	// Get DXGI adapter
	CComPtr<IDXGIAdapter> pDxgiAdapter;
	hr = pDxgiDevice->GetParent(
		__uuidof(IDXGIAdapter),
		reinterpret_cast<void**>(&pDxgiAdapter));
	pDxgiDevice.Release();
	RETURN_ON_BAD_HR(hr);

	// Get output
	CComPtr<IDXGIOutput> pDxgiOutput;
	hr = pDxgiAdapter->EnumOutputs(
		m_DisplayOutput,
		&pDxgiOutput);

	RETURN_ON_BAD_HR(hr);
	pDxgiAdapter.Release();

	RETURN_ON_BAD_HR(hr);
	CComPtr<IDXGIOutput1> pDxgiOutput1;

	hr = pDxgiOutput->QueryInterface(IID_PPV_ARGS(&pDxgiOutput1));
	RETURN_ON_BAD_HR(hr);
	pDxgiOutput.Release();

	// Create desktop duplication
	hr = pDxgiOutput1->DuplicateOutput(
		pDevice,
		&pDeskDupl);
	RETURN_ON_BAD_HR(hr);
	pDxgiOutput1.Release();

	// Create GUI drawing texture
	pDeskDupl->GetDesc(&OutputDuplDesc);

	pDxgiOutput1.Release();

	*ppDesktopDupl = pDeskDupl;
	(*ppDesktopDupl)->AddRef();
	*pOutputDuplDesc = OutputDuplDesc;
	return hr;
}

//
// Set new viewport
//
void internal_recorder::SetViewPort(ID3D11DeviceContext *deviceContext, UINT Width, UINT Height)
{
	D3D11_VIEWPORT VP;
	VP.Width = static_cast<FLOAT>(Width);
	VP.Height = static_cast<FLOAT>(Height);
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;
	VP.TopLeftX = 0;
	VP.TopLeftY = 0;
	deviceContext->RSSetViewports(1, &VP);
}

HRESULT internal_recorder::InitializeVideoSinkWriter(std::wstring path, ID3D11Device* pDevice, RECT sourceRect, RECT destRect, IMFSinkWriter **ppWriter, DWORD *pVideoStreamIndex, DWORD *pAudioStreamIndex)
{
	*ppWriter = NULL;
	*pVideoStreamIndex = NULL;
	*pAudioStreamIndex = NULL;

	UINT pResetToken;
	CComPtr<IMFDXGIDeviceManager> pDeviceManager = NULL;
	CComPtr<IMFSinkWriter>        pSinkWriter = NULL;
	CComPtr<IMFMediaType>         pVideoMediaTypeOut = NULL;
	CComPtr<IMFMediaType>         pAudioMediaTypeOut = NULL;
	CComPtr<IMFMediaType>         pVideoMediaTypeIn = NULL;
	CComPtr<IMFMediaType>         pAudioMediaTypeIn = NULL;

	CComPtr<IMFAttributes>        pAttributes = NULL;
	CComPtr<IMFSinkWriterEx>      pSinkWriterEx = NULL;

	DWORD audioStreamIndex;
	DWORD videoStreamIndex;
	RETURN_ON_BAD_HR(MFCreateDXGIDeviceManager(&pResetToken, &pDeviceManager));
	RETURN_ON_BAD_HR(pDeviceManager->ResetDevice(pDevice, pResetToken));
	// Passing 3 as the argument because we're adding 3 attributes immediately, saves re-allocations
	RETURN_ON_BAD_HR(MFCreateAttributes(&pAttributes, 3));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, 1));
	//RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_LOW_LATENCY, 1));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, 1));
	// Add device manager to attributes. This enables hardware encoding.
	RETURN_ON_BAD_HR(pAttributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, pDeviceManager));

	//Creates the sink writer with our custom attributes
	RETURN_ON_BAD_HR(MFCreateSinkWriterFromURL(path.c_str(), NULL, pAttributes, &pSinkWriter));

	RETURN_ON_BAD_HR(pSinkWriter->QueryInterface(&pSinkWriterEx));

	UINT sourceWidth = max(0, sourceRect.right - sourceRect.left);
	UINT sourceHeight = max(0, sourceRect.bottom - sourceRect.top);

	UINT destWidth = max(0, destRect.right - destRect.left);
	UINT destHeight = max(0, destRect.bottom - destRect.top);


	// Set the output video type.
	RETURN_ON_BAD_HR(MFCreateMediaType(&pVideoMediaTypeOut));
	RETURN_ON_BAD_HR(pVideoMediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	RETURN_ON_BAD_HR(pVideoMediaTypeOut->SetGUID(MF_MT_SUBTYPE, VIDEO_ENCODING_FORMAT));
	RETURN_ON_BAD_HR(pVideoMediaTypeOut->SetUINT32(MF_MT_AVG_BITRATE, m_VideoBitrate));
	RETURN_ON_BAD_HR(pVideoMediaTypeOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
	RETURN_ON_BAD_HR(MFSetAttributeSize(pVideoMediaTypeOut, MF_MT_FRAME_SIZE, destWidth, destHeight));
	RETURN_ON_BAD_HR(MFSetAttributeRatio(pVideoMediaTypeOut, MF_MT_FRAME_RATE, m_VideoFps, 1));
	RETURN_ON_BAD_HR(MFSetAttributeRatio(pVideoMediaTypeOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
	RETURN_ON_BAD_HR(pSinkWriter->AddStream(pVideoMediaTypeOut, &videoStreamIndex));
	pVideoMediaTypeOut.Release();

	// Set the input video type.
	RETURN_ON_BAD_HR(MFCreateMediaType(&pVideoMediaTypeIn));
	RETURN_ON_BAD_HR(pVideoMediaTypeIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	RETURN_ON_BAD_HR(pVideoMediaTypeIn->SetGUID(MF_MT_SUBTYPE, VIDEO_INPUT_FORMAT));
	RETURN_ON_BAD_HR(pVideoMediaTypeIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
	RETURN_ON_BAD_HR(MFSetAttributeSize(pVideoMediaTypeIn, MF_MT_FRAME_SIZE, sourceWidth, sourceHeight));
	RETURN_ON_BAD_HR(MFSetAttributeRatio(pVideoMediaTypeIn, MF_MT_FRAME_RATE, m_VideoFps, 1));
	RETURN_ON_BAD_HR(MFSetAttributeRatio(pVideoMediaTypeIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
	RETURN_ON_BAD_HR(pSinkWriter->SetInputMediaType(videoStreamIndex, pVideoMediaTypeIn, NULL));

	if (m_IsAudioEnabled) {
		// Set the output audio type.
		RETURN_ON_BAD_HR(MFCreateMediaType(&pAudioMediaTypeOut));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetGUID(MF_MT_SUBTYPE, AUDIO_ENCODING_FORMAT));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_AudioChannels));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, AUDIO_BITS_PER_SAMPLE));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, AUDIO_SAMPLES_PER_SECOND));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, m_AudioBitrate));
		RETURN_ON_BAD_HR(pSinkWriter->AddStream(pAudioMediaTypeOut, &audioStreamIndex));
		pAudioMediaTypeOut.Release();

		// Set the input audio type.
		RETURN_ON_BAD_HR(MFCreateMediaType(&pAudioMediaTypeIn));
		RETURN_ON_BAD_HR(pAudioMediaTypeIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
		RETURN_ON_BAD_HR(pAudioMediaTypeIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
		RETURN_ON_BAD_HR(pAudioMediaTypeIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, AUDIO_BITS_PER_SAMPLE));
		RETURN_ON_BAD_HR(pAudioMediaTypeIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, AUDIO_SAMPLES_PER_SECOND));
		RETURN_ON_BAD_HR(pAudioMediaTypeIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_AudioChannels));
		HRESULT hr = pSinkWriter->SetInputMediaType(audioStreamIndex, pAudioMediaTypeIn, NULL);
		RETURN_ON_BAD_HR(hr);

	}
	if (destWidth != sourceWidth || destHeight != sourceHeight) {
		GUID transformType;
		DWORD transformIndex = 0;
		CComPtr<IMFVideoProcessorControl> videoProcessor;
		while (true) {
			CComPtr<IMFTransform> transform;
			const HRESULT hr = pSinkWriterEx->GetTransformForStream(videoStreamIndex, transformIndex, &transformType, &transform);
			RETURN_ON_BAD_HR(hr);
			if (transformType == MFT_CATEGORY_VIDEO_PROCESSOR) {
				RETURN_ON_BAD_HR(transform->QueryInterface(&videoProcessor));
				break;
			}

			transformIndex++;
		}
		SIZE constrictionSize;
		constrictionSize.cx = sourceWidth;
		constrictionSize.cy = sourceHeight;
		videoProcessor->SetSourceRectangle(&destRect);
		videoProcessor->SetConstrictionSize(NULL);
	}
	// Tell the sink writer to start accepting data.
	RETURN_ON_BAD_HR(pSinkWriter->BeginWriting());

	// Return the pointer to the caller.
	*ppWriter = pSinkWriter;
	(*ppWriter)->AddRef();
	*pVideoStreamIndex = videoStreamIndex;
	*pAudioStreamIndex = audioStreamIndex;
	return S_OK;
}



void internal_recorder::EnqueueFrame(FrameWriteModel *model) {
	m_WriteQueue.push(model);
	if (m_FrameWriteTask.is_done() && m_IsRecording)
	{
		cancellation_token token = m_RenderTaskCts.get_token();
		m_FrameWriteTask = concurrency::create_task([this, token]() {
			while (true) {
				if (!m_IsRecording && m_WriteQueue.size() == 0) {
					break;
				}
				if (m_WriteQueue.size() == 0) {
					wait(1);
					continue;
				}
				if (token.is_canceled()) {
					while (m_WriteQueue.size() > 0) {
						FrameWriteModel *model = m_WriteQueue.front();
						delete model;
						m_WriteQueue.pop();
					}
					return;
				}
				HRESULT hr(S_OK);
				FrameWriteModel *model = m_WriteQueue.front();
				m_WriteQueue.pop();
				if (m_RecorderMode == MODE_VIDEO) {
					hr = WriteFrameToVideo(model->StartPos, model->Duration, m_VideoStreamIndex, model->Frame);
					if (FAILED(hr)) {
						ERR(L"Writing of frame with start pos %lld ms failed\n", (model->StartPos / 10 / 1000));
						m_IsRecording = false; //Stop recording if we fail
					}
				}
				else if (m_RecorderMode == MODE_SLIDESHOW) {
					wstring	path = m_OutputFolder + L"\\" + to_wstring(model->FrameNumber) + L".png";
					hr = WriteFrameToImage(model->Frame, path.c_str());
					LONGLONG startposMs = (model->StartPos / 10 / 1000);
					LONGLONG durationMs = (model->Duration / 10 / 1000);
					if (FAILED(hr)) {
						ERR(L"Writing of frame with start pos %lld ms failed\n", startposMs);
						m_IsRecording = false; //Stop recording if we fail
					}
					else {

						m_FrameDelays.insert(std::pair<wstring, int>(path, model->FrameNumber == 0 ? 0 : durationMs));
						ERR(L"Wrote frame with start pos %lld ms and with duration %lld ms\n", startposMs, durationMs);
					}
				}
				model->Frame.Release();
				BYTE *data;
				if (model->Audio.size() > 0) {
					data = new BYTE[model->Audio.size()];
					std::copy(model->Audio.begin(), model->Audio.end(), data);
					hr = WriteAudioSamplesToVideo(model->StartPos, model->Duration, m_AudioStreamIndex, data, model->Audio.size());
					if (FAILED(hr)) {
						ERR(L"Writing of audio sample with start pos %lld ms failed\n", (model->StartPos / 10 / 1000));
					}
					delete[] data;
					model->Audio.clear();
					vector<BYTE>().swap(model->Audio);
				}
				delete model;
				model = nullptr;
			}
		});
	}
}

std::string internal_recorder::NowToString()
{
	chrono::system_clock::time_point p = chrono::system_clock::now();
	time_t t = chrono::system_clock::to_time_t(p);

	std::stringstream ss;
	ss << std::put_time(std::localtime(&t), "%Y-%m-%d %X");
	string time = ss.str();
	std::replace(time.begin(), time.end(), ':', '-');
	return time;
}
HRESULT internal_recorder::WriteFrameToImage(ID3D11Texture2D* pAcquiredDesktopImage, LPCWSTR filePath)
{

	HRESULT hr = SaveWICTextureToFile(pImmediateContext, pAcquiredDesktopImage,
		IMAGE_ENCODER_FORMAT, filePath);
	return hr;
}

HRESULT internal_recorder::WriteFrameToVideo(ULONGLONG frameStartPos, ULONGLONG frameDuration, DWORD streamIndex, ID3D11Texture2D* pAcquiredDesktopImage)
{
	CComPtr<IMFMediaBuffer> pMediaBuffer;
	HRESULT hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), pAcquiredDesktopImage, 0, FALSE, &pMediaBuffer);

	CComPtr<IMF2DBuffer> p2DBuffer;
	DWORD length;

	hr = pMediaBuffer->QueryInterface(__uuidof(IMF2DBuffer), reinterpret_cast<void **>(&p2DBuffer));
	if (SUCCEEDED(hr))
	{
		hr = p2DBuffer->GetContiguousLength(&length);
	}
	if (SUCCEEDED(hr))
	{
		hr = pMediaBuffer->SetCurrentLength(length);
	}
	CComPtr<IMFSample> pSample;
	if (SUCCEEDED(hr))
	{
		hr = MFCreateVideoSampleFromSurface(NULL, &pSample);
	}
	if (SUCCEEDED(hr))
	{
		hr = pSample->AddBuffer(pMediaBuffer);
	}
	if (SUCCEEDED(hr))
	{
		hr = pSample->SetSampleTime(frameStartPos);
	}
	if (SUCCEEDED(hr))
	{
		hr = pSample->SetSampleDuration(frameDuration);
	}
	if (SUCCEEDED(hr))
	{
		// Send the sample to the Sink Writer.
		hr = m_SinkWriter->WriteSample(streamIndex, pSample);
		if (SUCCEEDED(hr)) {
			LOG(L"Wrote frame with start pos %lld ms and with duration %lld ms", (frameStartPos / 10 / 1000), (frameDuration / 10 / 1000));
		}
	}

	return hr;
}
HRESULT internal_recorder::WriteAudioSamplesToVideo(ULONGLONG frameStartPos, ULONGLONG frameDuration, DWORD streamIndex, BYTE *pSrc, DWORD cbData)
{
	CComPtr<IMFMediaBuffer> pBuffer = NULL;
	BYTE *pData = NULL;
	// Create the media buffer.
	HRESULT hr = MFCreateMemoryBuffer(
		cbData,   // Amount of memory to allocate, in bytes.
		&pBuffer
	);

	// Lock the buffer to get a pointer to the memory.
	if (SUCCEEDED(hr))
	{
		hr = pBuffer->Lock(&pData, NULL, NULL);
	}

	if (SUCCEEDED(hr))
	{
		memcpy_s(pData, cbData, pSrc, cbData);
	}

	// Update the current length.
	if (SUCCEEDED(hr))
	{
		hr = pBuffer->SetCurrentLength(cbData);
	}

	// Unlock the buffer.
	if (pData)
	{
		hr = pBuffer->Unlock();
	}

	CComPtr<IMFSample> pSample;
	if (SUCCEEDED(hr))
	{
		hr = MFCreateVideoSampleFromSurface(NULL, &pSample);
	}
	if (SUCCEEDED(hr))
	{
		hr = pSample->AddBuffer(pBuffer);
	}
	if (SUCCEEDED(hr))
	{
		LONGLONG start = frameStartPos;
		hr = pSample->SetSampleTime(start);
	}
	if (SUCCEEDED(hr))
	{
		LONGLONG duration = frameDuration;
		hr = pSample->SetSampleDuration(duration);
	}
	if (SUCCEEDED(hr))
	{
		// Send the sample to the Sink Writer.
		hr = m_SinkWriter->WriteSample(streamIndex, pSample);
		if (SUCCEEDED(hr)) {
			LOG(L"Wrote audio sample with start pos %lld ms and with duration %lld ms", frameStartPos / 10 / 1000, (frameDuration / 10 / 1000));
		}
	}
	if (pBuffer)
		pBuffer.Release();
	if (pSample)
		pSample.Release();
	return hr;
}


void internal_recorder::SetDebugName(ID3D11DeviceChild* child, const std::string& name)
{
#if _DEBUG
	if (child != nullptr)
		child->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());
#endif
}
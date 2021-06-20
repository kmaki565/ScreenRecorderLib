#include "Recorder.h"
#include <memory>
#include <msclr\marshal.h>
#include <msclr\marshal_cppstd.h>
#include "ManagedIStream.h"
using namespace ScreenRecorderLib;
using namespace nlohmann;

Recorder::Recorder(RecorderOptions^ options)
{
	m_Rec = new RecordingManager();
	if (!options) {
		options = gcnew RecorderOptions();
	}
	SetOptions(options);
}

void Recorder::SetOptions(RecorderOptions^ options) {
	if (options && m_Rec) {
		if (options->VideoEncoderOptions) {
			if (!options->VideoEncoderOptions->Encoder) {
				options->VideoEncoderOptions->Encoder = gcnew H264VideoEncoder();
			}
			ENCODER_OPTIONS* encoderOptions = nullptr;

			switch (options->VideoEncoderOptions->Encoder->EncodingFormat)
			{
				default:
				case VideoEncoderFormat::H264: {
					encoderOptions = new H264_ENCODER_OPTIONS();
					break;
				}
				case VideoEncoderFormat::H265: {
					encoderOptions = new H265_ENCODER_OPTIONS();
					break;
				}
			}
			encoderOptions->SetVideoBitrateMode((UINT32)options->VideoEncoderOptions->Encoder->GetBitrateMode());
			encoderOptions->SetEncoderProfile((UINT32)options->VideoEncoderOptions->Encoder->GetEncoderProfile());

			encoderOptions->SetVideoBitrate(options->VideoEncoderOptions->Bitrate);
			encoderOptions->SetVideoQuality(options->VideoEncoderOptions->Quality);
			encoderOptions->SetVideoFps(options->VideoEncoderOptions->Framerate);
			encoderOptions->SetIsFixedFramerate(options->VideoEncoderOptions->IsFixedFramerate);
			encoderOptions->SetIsThrottlingDisabled(options->VideoEncoderOptions->IsThrottlingDisabled);
			encoderOptions->SetIsLowLatencyModeEnabled(options->VideoEncoderOptions->IsLowLatencyEnabled);
			encoderOptions->SetIsFastStartEnabled(options->VideoEncoderOptions->IsMp4FastStartEnabled);
			encoderOptions->SetIsHardwareEncodingEnabled(options->VideoEncoderOptions->IsHardwareEncodingEnabled);
			encoderOptions->SetIsFragmentedMp4Enabled(options->VideoEncoderOptions->IsFragmentedMp4Enabled);
			m_Rec->SetEncoderOptions(encoderOptions);
		}
		if (options->SnapshotOptions) {
			m_Rec->SetTakeSnapshotsWithVideo(options->SnapshotOptions->SnapshotsWithVideo);
			m_Rec->SetSnapshotsWithVideoInterval(options->SnapshotOptions->SnapshotsIntervalMillis);
			if (options->SnapshotOptions->SnapshotsDirectory != nullptr) {
				m_Rec->SetSnapshotDirectory(msclr::interop::marshal_as<std::wstring>(options->SnapshotOptions->SnapshotsDirectory));
			}
			switch (options->SnapshotOptions->SnapshotFormat)
			{
			case ImageFormat::BMP:
				m_Rec->SetSnapshotSaveFormat(GUID_ContainerFormatBmp);
				break;
			case ImageFormat::JPEG:
				m_Rec->SetSnapshotSaveFormat(GUID_ContainerFormatJpeg);
				break;
			case ImageFormat::TIFF:
				m_Rec->SetSnapshotSaveFormat(GUID_ContainerFormatTiff);
				break;
			default:
			case ImageFormat::PNG:
				m_Rec->SetSnapshotSaveFormat(GUID_ContainerFormatPng);
				break;
			}
		}
		if (options->SourceOptions) {
			if (options->SourceOptions->SourceRect) {
				RECT rect;
				rect.left = options->SourceOptions->SourceRect->Left;
				rect.top = options->SourceOptions->SourceRect->Top;
				rect.right = options->SourceOptions->SourceRect->Right;
				rect.bottom = options->SourceOptions->SourceRect->Bottom;
				m_Rec->SetDestRectangle(rect);
			}
		}
		m_Rec->SetRecordingSources(CreateRecordingSourceList(options));
		if (options->AudioOptions) {
			AUDIO_OPTIONS* audioOptions = new AUDIO_OPTIONS();

			audioOptions->SetAudioEnabled(options->AudioOptions->IsAudioEnabled);
			audioOptions->SetOutputDeviceEnabled(options->AudioOptions->IsOutputDeviceEnabled);
			audioOptions->SetInputDeviceEnabled(options->AudioOptions->IsInputDeviceEnabled);
			audioOptions->SetAudioBitrate((UINT32)options->AudioOptions->Bitrate);
			audioOptions->SetAudioChannels((UINT32)options->AudioOptions->Channels);
			if (options->AudioOptions->AudioOutputDevice != nullptr) {
				audioOptions->SetOutputDevice(msclr::interop::marshal_as<std::wstring>(options->AudioOptions->AudioOutputDevice));
			}
			if (options->AudioOptions->AudioInputDevice != nullptr) {
				audioOptions->SetInputDevice(msclr::interop::marshal_as<std::wstring>(options->AudioOptions->AudioInputDevice));
			}
			audioOptions->SetInputVolume(options->AudioOptions->InputVolume);
			audioOptions->SetOutputVolume(options->AudioOptions->OutputVolume);
			m_Rec->SetAudioOptions(audioOptions);
		}
		if (options->MouseOptions) {
			MOUSE_OPTIONS* mouseOptions = new MOUSE_OPTIONS();

			mouseOptions->SetMousePointerEnabled(options->MouseOptions->IsMousePointerEnabled);
			mouseOptions->SetDetectMouseClicks(options->MouseOptions->IsMouseClicksDetected);
			mouseOptions->SetMouseClickDetectionLMBColor(msclr::interop::marshal_as<std::string>(options->MouseOptions->MouseClickDetectionColor));
			mouseOptions->SetMouseClickDetectionRMBColor(msclr::interop::marshal_as<std::string>(options->MouseOptions->MouseRightClickDetectionColor));
			mouseOptions->SetMouseClickDetectionRadius(options->MouseOptions->MouseClickDetectionRadius);
			mouseOptions->SetMouseClickDetectionDuration(options->MouseOptions->MouseClickDetectionDuration);
			mouseOptions->SetMouseClickDetectionMode((UINT32)options->MouseOptions->MouseClickDetectionMode);
			m_Rec->SetMouseOptions(mouseOptions);
		}
		if (options->OverlayOptions) {
			m_Rec->SetOverlays(CreateOverlayList(options->OverlayOptions->Overlays));
		}

		m_Rec->SetRecorderMode((UINT32)options->RecorderMode);
		m_Rec->SetRecorderApi((UINT32)options->RecorderApi);

		m_Rec->SetIsLogEnabled(options->IsLogEnabled);
		if (options->LogFilePath != nullptr) {
			m_Rec->SetLogFilePath(msclr::interop::marshal_as<std::wstring>(options->LogFilePath));
		}
		m_Rec->SetLogSeverityLevel((UINT32)options->LogSeverityLevel);
	}
}

void Recorder::SetInputVolume(float volume)
{
	if (m_Rec)
	{
		m_Rec->GetAudioOptions()->SetInputVolume(volume);
	}
}

void Recorder::SetOutputVolume(float volume)
{
	if (m_Rec)
	{
		m_Rec->GetAudioOptions()->SetOutputVolume(volume);
	}
}

bool Recorder::SetExcludeFromCapture(System::IntPtr hwnd, bool isExcluded)
{
	return RecordingManager::SetExcludeFromCapture((HWND)hwnd.ToPointer(), isExcluded);
}

Dictionary<String^, String^>^ Recorder::GetSystemAudioDevices(AudioDeviceSource source)
{
	std::map<std::wstring, std::wstring> map;
	EDataFlow dFlow;

	switch (source)
	{
	default:
	case  AudioDeviceSource::OutputDevices:
		dFlow = eRender;
		break;
	case AudioDeviceSource::InputDevices:
		dFlow = eCapture;
		break;
	case AudioDeviceSource::All:
		dFlow = eAll;
		break;
	}

	Dictionary<String^, String^>^ devices = gcnew Dictionary<String^, String^>();

	HRESULT hr = AudioPrefs::list_devices(dFlow, &map);

	if (SUCCEEDED(hr))
	{
		if (map.size() != 0)
		{
			for (auto const& element : map) {
				devices->Add(gcnew String(element.first.c_str()), gcnew String(element.second.c_str()));
			}
		}
	}
	return devices;
}

Dictionary<String^, String^>^ ScreenRecorderLib::Recorder::GetSystemVideoCaptureDevices()
{
	std::map<std::wstring, std::wstring> map;
	HRESULT hr = EnumVideoCaptureDevices(&map);
	Dictionary<String^, String^>^ devices = gcnew Dictionary<String^, String^>();
	if (SUCCEEDED(hr))
	{
		if (map.size() != 0)
		{
			for (auto const& element : map) {
				devices->Add(gcnew String(element.first.c_str()), gcnew String(element.second.c_str()));
			}
		}
	}
	return devices;
}

List<RecordableDisplay^>^ ScreenRecorderLib::Recorder::GetDisplays()
{
	List<RecordableDisplay^>^ displays = gcnew List<RecordableDisplay^>();
	std::vector<IDXGIOutput*> outputs{};
	EnumOutputs(&outputs);

	for each (IDXGIOutput * output in outputs)
	{
		DXGI_OUTPUT_DESC desc;
		if (SUCCEEDED(output->GetDesc(&desc))) {
			if (desc.AttachedToDesktop) {
				auto display = gcnew RecordableDisplay();
				display->DeviceName = gcnew String(desc.DeviceName);
				display->MonitorName = gcnew String(GetMonitorName(desc.Monitor).c_str());
				displays->Add(display);
			}
		}
		output->Release();
	}
	return displays;
}

List<RecordableWindow^>^ ScreenRecorderLib::Recorder::GetWindows()
{
	List<RecordableWindow^>^ windows = gcnew List<RecordableWindow^>();
	for each (const Window & win in EnumerateWindows())
	{
		RECT rect;
		GetWindowRect(win.Hwnd(), &rect);
		RecordableWindow^ recordableWin = gcnew RecordableWindow(gcnew String(win.Title().c_str()), IntPtr(win.Hwnd()));
		windows->Add(recordableWin);
	}
	return windows;
}

ScreenSize^ ScreenRecorderLib::Recorder::GetCombinedOutputSizeForRecordingSources(List<RecordingSource^>^ recordingSources)
{
	std::vector<RECORDING_SOURCE> sources{};
	sources.reserve(recordingSources->Count);
	for each (RecordingSource ^ recordingSource in recordingSources)
	{
		switch (recordingSource->SourceType)
		{
		case RecordingSourceType::Window: {
			WindowRecordingSource^ windowRecordingSource = (WindowRecordingSource^)recordingSource;
			if (windowRecordingSource->Handle != IntPtr::Zero) {
				HWND hwnd = (HWND)windowRecordingSource->Handle.ToPointer();
				if (hwnd) {
					RECORDING_SOURCE source{};
					source.Type = RecordingSourceType::Window;
					source.WindowHandle = hwnd;
					sources.push_back(source);
				}
			}
			break;
		}
		case RecordingSourceType::Display: {
			DisplayRecordingSource^ displayRecordingSource = (DisplayRecordingSource^)recordingSource;
			if (!String::IsNullOrEmpty(displayRecordingSource->DeviceName)) {
				RECORDING_SOURCE source{};
				source.Type = RecordingSourceType::Display;
				source.CaptureDevice = msclr::interop::marshal_as<std::wstring>(displayRecordingSource->DeviceName);
				sources.push_back(source);
			}
			break;
		}
		default:
			break;
		}
	}

	std::vector<RECT> outputRects{};
	std::vector<std::pair< RECORDING_SOURCE, RECT>> validOutputs{};
	HRESULT hr = GetOutputRectsForRecordingSources(sources, &validOutputs);
	for each (auto const& pair in validOutputs) {
		outputRects.push_back(pair.second);
	}
	RECT deskBounds;
	GetCombinedRects(outputRects, &deskBounds, nullptr);
	return gcnew ScreenSize(RectWidth(deskBounds), RectHeight(deskBounds));
}

Recorder::~Recorder()
{
	this->!Recorder();
	GC::SuppressFinalize(this);
}

Recorder::!Recorder() {
	if (m_Rec) {
		delete m_Rec;
		m_Rec = nullptr;
	}
	if (m_ManagedStream) {
		delete m_ManagedStream;
		m_ManagedStream = nullptr;
	}
	ClearCallbacks();
}

Recorder^ Recorder::CreateRecorder() {
	return gcnew Recorder(nullptr);
}

Recorder^ Recorder::CreateRecorder(RecorderOptions^ options)
{
	Recorder^ rec = gcnew Recorder(options);
	return rec;
}
void Recorder::Record(System::Runtime::InteropServices::ComTypes::IStream^ stream) {
	SetupCallbacks();
	IStream* pNativeStream = (IStream*)Marshal::GetComInterfaceForObject(stream, System::Runtime::InteropServices::ComTypes::IStream::typeid).ToPointer();
	m_Rec->BeginRecording(pNativeStream);
}
void Recorder::Record(System::IO::Stream^ stream) {
	SetupCallbacks();
	m_ManagedStream = new ManagedIStream(stream);
	m_Rec->BeginRecording(m_ManagedStream);
}
void Recorder::Record(System::String^ path) {
	SetupCallbacks();
	std::wstring stdPathString = msclr::interop::marshal_as<std::wstring>(path);
	m_Rec->BeginRecording(stdPathString);
}
void Recorder::Pause() {
	m_Rec->PauseRecording();
}
void Recorder::Resume() {
	m_Rec->ResumeRecording();
}
void Recorder::Stop() {
	m_Rec->EndRecording();
}

void Recorder::SetupCallbacks() {
	CreateErrorCallback();
	CreateCompletionCallback();
	CreateStatusCallback();
	CreateSnapshotCallback();
}

void Recorder::ClearCallbacks() {
	if (_statusChangedDelegateGcHandler.IsAllocated)
		_statusChangedDelegateGcHandler.Free();
	if (_errorDelegateGcHandler.IsAllocated)
		_errorDelegateGcHandler.Free();
	if (_completedDelegateGcHandler.IsAllocated)
		_completedDelegateGcHandler.Free();
	if (_snapshotDelegateGcHandler.IsAllocated)
		_snapshotDelegateGcHandler.Free();
}

std::vector<RECORDING_SOURCE> Recorder::CreateRecordingSourceList(RecorderOptions^ options) {
	std::vector<RECORDING_SOURCE> sources{};
	if (!options->SourceOptions) {
		options->SourceOptions = SourceOptions::MainMonitor;
	}
	if (options->SourceOptions && options->SourceOptions->RecordingSources) {
		for each (RecordingSource ^ source in options->SourceOptions->RecordingSources)
		{
			switch (source->SourceType)
			{
			case RecordingSourceType::Display:
			{
				DisplayRecordingSource^ displaySource = (DisplayRecordingSource^)source;
				if (displaySource->DeviceName == DisplayRecordingSource::AllMonitors->DeviceName) {
					std::vector<IDXGIOutput*> outputs;
					EnumOutputs(&outputs);
					for each (IDXGIOutput * output in outputs)
					{
						DXGI_OUTPUT_DESC desc;
						output->GetDesc(&desc);
						RECORDING_SOURCE source{};
						source.Type = displaySource->SourceType;
						source.CaptureDevice = desc.DeviceName;
						if (options->MouseOptions && options->MouseOptions->IsMousePointerEnabled) {
							source.IsCursorCaptureEnabled = true;
						}
						if (std::find(sources.begin(), sources.end(), source) == sources.end()) {
							sources.insert(sources.end(), source);
						}
						output->Release();
					}
				}
				else {
					if (!String::IsNullOrEmpty(displaySource->DeviceName)) {
						std::wstring deviceName = msclr::interop::marshal_as<std::wstring>(displaySource->DeviceName);
						CComPtr<IDXGIOutput> output;
						HRESULT hr = GetOutputForDeviceName(deviceName, &output);
						if (SUCCEEDED(hr)) {
							DXGI_OUTPUT_DESC desc;
							hr = output->GetDesc(&desc);
							if (SUCCEEDED(hr)) {
								RECORDING_SOURCE source{};
								source.Type = displaySource->SourceType;
								source.CaptureDevice = desc.DeviceName;
								if (options->MouseOptions && options->MouseOptions->IsMousePointerEnabled) {
									source.IsCursorCaptureEnabled = true;
								}
								if (std::find(sources.begin(), sources.end(), source) == sources.end()) {
									sources.insert(sources.end(), source);
								}
							}
						}
					}
				}
				break;
			}
			case RecordingSourceType::Window: {

				WindowRecordingSource^ windowSource = (WindowRecordingSource^)source;
				if (windowSource->Handle != IntPtr::Zero) {
					HWND window = (HWND)(windowSource->Handle.ToPointer());
					RECORDING_SOURCE source{};
					source.Type = windowSource->SourceType;
					source.WindowHandle = window;
					if (options->MouseOptions && options->MouseOptions->IsMousePointerEnabled) {
						source.IsCursorCaptureEnabled = true;
					}
					if (std::find(sources.begin(), sources.end(), source) == sources.end()) {
						sources.insert(sources.end(), source);
					}
				}
				break;
			}
			default:
				break;
			}
		}
		std::vector<RECORDING_SOURCE> sourceVector{};
		for each (auto obj in sources)
		{
			sourceVector.push_back(obj);
		}
		return sourceVector;
	}
}

std::vector<RECORDING_OVERLAY> Recorder::CreateOverlayList(List<RecordingOverlay^>^ managedOverlays) {
	std::vector<RECORDING_OVERLAY> overlays{};
	if (managedOverlays != nullptr) {
		for each (RecordingOverlay ^ managedOverlay in managedOverlays)
		{
			RECORDING_OVERLAY overlay{};
			overlay.Offset = POINT{ managedOverlay->OffsetX, managedOverlay->OffsetY };
			overlay.Size = SIZE{ managedOverlay->Width, managedOverlay->Height };
			switch (managedOverlay->AnchorPosition)
			{
			case Anchor::BottomLeft:
				overlay.Anchor = OverlayAnchor::BottomLeft;
				break;
			case Anchor::BottomRight:
				overlay.Anchor = OverlayAnchor::BottomRight;
				break;
			case Anchor::TopLeft:
				overlay.Anchor = OverlayAnchor::TopLeft;
				break;
			case Anchor::TopRight:
				overlay.Anchor = OverlayAnchor::TopRight;
				break;
			default:
				break;
			}

			if (managedOverlay->GetType() == CameraCaptureOverlay::typeid) {
				CameraCaptureOverlay^ videoCaptureOverlay = (CameraCaptureOverlay^)managedOverlay;
				overlay.Type = OverlayType::CameraCapture;
				if (videoCaptureOverlay->CaptureDeviceName != nullptr) {
					overlay.Source = msclr::interop::marshal_as<std::wstring>(videoCaptureOverlay->CaptureDeviceName);
				}
				overlays.push_back(overlay);
			}
			else if (managedOverlay->GetType() == PictureOverlay::typeid) {
				PictureOverlay^ pictureOverlay = (PictureOverlay^)managedOverlay;
				overlay.Type = OverlayType::Picture;
				if (pictureOverlay->FilePath != nullptr) {
					overlay.Source = msclr::interop::marshal_as<std::wstring>(pictureOverlay->FilePath);
				}
				overlays.push_back(overlay);
			}
			else if (managedOverlay->GetType() == VideoOverlay::typeid) {
				VideoOverlay^ videoOverlay = (VideoOverlay^)managedOverlay;
				overlay.Type = OverlayType::Video;
				if (videoOverlay->FilePath != nullptr) {
					overlay.Source = msclr::interop::marshal_as<std::wstring>(videoOverlay->FilePath);
				}
				overlays.push_back(overlay);
			}
		}
	}
	return overlays;
}

void Recorder::CreateErrorCallback() {
	InternalErrorCallbackDelegate^ fp = gcnew InternalErrorCallbackDelegate(this, &Recorder::EventFailed);
	_errorDelegateGcHandler = GCHandle::Alloc(fp);
	IntPtr ip = Marshal::GetFunctionPointerForDelegate(fp);
	CallbackErrorFunction cb = static_cast<CallbackErrorFunction>(ip.ToPointer());
	m_Rec->RecordingFailedCallback = cb;

}
void Recorder::CreateCompletionCallback() {
	InternalCompletionCallbackDelegate^ fp = gcnew InternalCompletionCallbackDelegate(this, &Recorder::EventComplete);
	_completedDelegateGcHandler = GCHandle::Alloc(fp);
	IntPtr ip = Marshal::GetFunctionPointerForDelegate(fp);
	CallbackCompleteFunction cb = static_cast<CallbackCompleteFunction>(ip.ToPointer());
	m_Rec->RecordingCompleteCallback = cb;
}
void Recorder::CreateStatusCallback() {
	InternalStatusCallbackDelegate^ fp = gcnew InternalStatusCallbackDelegate(this, &Recorder::EventStatusChanged);
	_statusChangedDelegateGcHandler = GCHandle::Alloc(fp);
	IntPtr ip = Marshal::GetFunctionPointerForDelegate(fp);
	CallbackStatusChangedFunction cb = static_cast<CallbackStatusChangedFunction>(ip.ToPointer());
	m_Rec->RecordingStatusChangedCallback = cb;
}
void Recorder::CreateSnapshotCallback() {
	InternalSnapshotCallbackDelegate^ fp = gcnew InternalSnapshotCallbackDelegate(this, &Recorder::EventSnapshotCreated);
	_snapshotDelegateGcHandler = GCHandle::Alloc(fp);
	IntPtr ip = Marshal::GetFunctionPointerForDelegate(fp);
	CallbackSnapshotFunction cb = static_cast<CallbackSnapshotFunction>(ip.ToPointer());
	m_Rec->RecordingSnapshotCreatedCallback = cb;
}
void Recorder::EventComplete(std::wstring str, fifo_map<std::wstring, int> delays)
{
	ClearCallbacks();

	List<FrameData^>^ frameInfos = gcnew List<FrameData^>();

	for (auto x : delays) {
		frameInfos->Add(gcnew FrameData(gcnew String(x.first.c_str()), x.second));
	}
	if (m_ManagedStream) {
		delete m_ManagedStream;
		m_ManagedStream = nullptr;
	}
	RecordingCompleteEventArgs^ args = gcnew RecordingCompleteEventArgs(gcnew String(str.c_str()), frameInfos);
	OnRecordingComplete(this, args);
}
void Recorder::EventFailed(std::wstring str)
{
	ClearCallbacks();
	if (m_ManagedStream) {
		delete m_ManagedStream;
		m_ManagedStream = nullptr;
	}
	OnRecordingFailed(this, gcnew RecordingFailedEventArgs(gcnew String(str.c_str())));
}
void Recorder::EventStatusChanged(int status)
{
	RecorderStatus recorderStatus = (RecorderStatus)status;
	Status = recorderStatus;
	OnStatusChanged(this, gcnew RecordingStatusEventArgs(recorderStatus));
}

void ScreenRecorderLib::Recorder::EventSnapshotCreated(std::wstring str)
{
	OnSnapshotSaved(this, gcnew SnapshotSavedEventArgs(gcnew String(str.c_str())));
}

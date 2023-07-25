#pragma once

using namespace System;
using namespace System::Collections::Generic;

namespace ScreenRecorderLib {
	public enum class RecorderStatus {
		Idle,
		Recording,
		Paused,
		Finishing
	};
	public ref class FrameData {
	public:
		property String^ Path;
		property int Delay;
		FrameData() {}
		FrameData(String^ path, int delay) {
			Path = path;
			Delay = delay;
		}
	};

	public ref class RecordingStatusEventArgs :System::EventArgs {
	public:
		property RecorderStatus Status;
		RecordingStatusEventArgs(RecorderStatus status) {
			Status = status;
		}
	};
	public ref class RecordingCompleteEventArgs :System::EventArgs {
	public:
		property String^ FilePath;
		property  List<FrameData^>^ FrameInfos;
		RecordingCompleteEventArgs(String^ path, List<FrameData^>^ frameInfos) {
			FilePath = path;
			FrameInfos = frameInfos;
		}
	};
	public ref class RecordingFailedEventArgs :System::EventArgs {
	public:
		property String^ Error;
		property String^ FilePath;
		RecordingFailedEventArgs(String^ error, String^ path) {
			Error = error;
			FilePath = path;
		}
	};

	public ref class SnapshotSavedEventArgs :System::EventArgs {
	public:
		property String^ SnapshotPath;
		SnapshotSavedEventArgs(String^ path) {
			SnapshotPath = path;
		}
	};
	public ref class FrameRecordedEventArgs :System::EventArgs {
	public:
		property int FrameNumber;
		property INT64 Timestamp;
		FrameRecordedEventArgs(int frameNumber, INT64 timestamp) {
			FrameNumber = frameNumber;
			Timestamp = timestamp;
		}
	};

	public ref class FrameDataRecordedEventArgs :System::EventArgs {
	public:
		property int Stride;
		property int Width;
		property int Height;
		property IntPtr Data;
		property int Length;
		FrameDataRecordedEventArgs(int stride, byte* data, int length,int width, int height) {
			Stride = stride;
			Data = IntPtr(data);
			Length = length;
			Width = width;
			Height = height;
		}
	};
}
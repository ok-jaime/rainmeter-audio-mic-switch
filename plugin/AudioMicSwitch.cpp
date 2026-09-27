/*
  AudioMicSwitch - Rainmeter plugin for the Audio + Mic Switch skin.
  By AdviceWithSalt & ok-jaime (https://github.com/ok-jaime)

  Switches the default playback and recording devices by name, and controls
  the playback volume.

  Bangs (use with !CommandMeasure):
    SetOutput <device name>   Make this the default playback device. With no
                              name (skin not set up yet), shows PickDevice.
    SetInput <device name>    Make this the default recording device (mic).
                              With no name, does nothing.
    PickDevice                Show a device menu at the cursor, with a submenu
                              for each PickN option
    PickDevice <Output|Input> <variable>
                              Show a menu of just those devices
    ChangeVolume <+n|-n>      Change playback volume by n percent
    ToggleMute                Mute/unmute playback

  The chosen device's name is put in the skin variable and saved to SaveTo.

  Options:
    SaveTo=<file>             Where PickDevice saves choices, e.g.
                              #@#Variables.inc. Default: the skin's .ini.
    Pick1=<Output|Input>,<variable>,<label>
    Pick2=...                 Submenus shown by PickDevice with no arguments
    MatchOutput=<device name> Makes the measure's number 1 while this is the
                              default playback device, 0 otherwise

  Device names are the ones shown in Windows Sound settings, for example
  "Speakers (BlackShark V3 Pro - Game)". Matching ignores case; an exact match
  wins, otherwise the first active device containing the text is used.
  Devices are set as default for all roles (including "communications").

  Measure number: see MatchOutput; without it, the playback volume (0-100).
  Measure string: name of the current default playback device.

  Build: see build.ps1.
*/

#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <string>
#include <vector>
#include <cwctype>

#define PLUGIN_EXPORT extern "C" __declspec(dllexport)

// Rainmeter API, looked up at runtime so building doesn't need Rainmeter.lib.
enum { LOG_ERROR = 1, LOG_WARNING = 2, LOG_NOTICE = 3, LOG_DEBUG = 4 };
enum { RMG_MEASURENAME = 0, RMG_SKIN = 1, RMG_SKINWINDOWHANDLE = 4 };
typedef void (__stdcall *RmLogFunc)(void*, int, LPCWSTR);
typedef LPCWSTR (__stdcall *RmReadStringFunc)(void*, LPCWSTR, LPCWSTR, BOOL);
typedef LPCWSTR (__stdcall *RmReplaceVariablesFunc)(void*, LPCWSTR);
typedef void* (__stdcall *RmGetFunc)(void*, int);
typedef void (__stdcall *RmExecuteFunc)(void*, LPCWSTR);

// 32-bit Rainmeter may export these __stdcall functions with decorated names
// (_Name@<argument bytes>), so that form is tried too.
template <class F>
static F RmApi(const char* name, int argumentBytes)
{
	HMODULE rainmeter = GetModuleHandleW(L"Rainmeter.dll");
	FARPROC function = GetProcAddress(rainmeter, name);
#ifndef _WIN64
	if (!function)
	{
		char decorated[64];
		wsprintfA(decorated, "_%s@%d", name, argumentBytes);
		function = GetProcAddress(rainmeter, decorated);
	}
#endif
	return (F)(void*)function;
}

static const CLSID CLSID_MMDeviceEnumerator_ = {0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
static const IID IID_IMMDeviceEnumerator_ = {0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};
static const IID IID_IAudioEndpointVolume_ = {0x5cdf2c82, 0x841e, 0x4546, {0x97, 0x22, 0x0c, 0xf7, 0x40, 0x78, 0x22, 0x9a}};
static const PROPERTYKEY PKEY_FriendlyName_ = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

// Windows has no public API for changing the default audio device. This is the
// undocumented interface that the Sound control panel (and every audio
// switcher, including Rainmeter's Win7AudioPlugin) uses. Only the vtable order
// matters; SetDefaultEndpoint is the one we call.
struct IPolicyConfig : public IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, void**) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, void**) = 0;
	virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, void*, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, void*, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR deviceId, ERole role) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
static const CLSID CLSID_PolicyConfigClient = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
static const IID IID_IPolicyConfig = {0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8}};

// Releases a COM pointer when it goes out of scope.
template <class T>
struct Com
{
	T* p = nullptr;
	~Com() { if (p) p->Release(); }
	T* operator->() const { return p; }
	T** operator&() { return &p; }
};

// One device list in the PickDevice menu: which devices, and the variable it sets.
struct Pick
{
	bool output;
	std::wstring variable;
	std::wstring label;
};

struct Measure
{
	void* rm = nullptr;
	bool comInitialized = false;
	std::wstring deviceName;
	std::wstring saveTo;
	std::wstring matchOutput;
	std::vector<Pick> picks;
};

struct Device
{
	std::wstring id;
	std::wstring name;
};

static void Log(Measure* m, int level, const std::wstring& message)
{
	static RmLogFunc rmLog = RmApi<RmLogFunc>("RmLog", 12);
	if (rmLog) rmLog(m->rm, level, (L"AudioMicSwitch: " + message).c_str());
}

static std::wstring Lower(std::wstring s)
{
	for (wchar_t& c : s) c = towlower(c);
	return s;
}

static std::wstring Trim(const std::wstring& s)
{
	size_t start = s.find_first_not_of(L" \t\"");
	if (start == std::wstring::npos) return L"";
	size_t end = s.find_last_not_of(L" \t\"");
	return s.substr(start, end - start + 1);
}

// Splits "word rest of line" into "word" and "rest of line".
static void SplitFirstWord(const std::wstring& line, std::wstring& first, std::wstring& rest)
{
	const size_t space = line.find(L' ');
	first = line.substr(0, space);
	rest = space == std::wstring::npos ? L"" : Trim(line.substr(space + 1));
}

// Wraps a bang parameter in triple quotes so it may contain quotes itself.
static std::wstring Quote(const std::wstring& s)
{
	return L"\"\"\"" + s + L"\"\"\"";
}

static bool CreateEnumerator(IMMDeviceEnumerator** enumerator)
{
	return SUCCEEDED(CoCreateInstance(CLSID_MMDeviceEnumerator_, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator_, (void**)enumerator));
}

static std::wstring FriendlyName(IMMDevice* device)
{
	std::wstring name;
	Com<IPropertyStore> store;
	if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)))
	{
		PROPVARIANT value;
		PropVariantInit(&value);
		if (SUCCEEDED(store->GetValue(PKEY_FriendlyName_, &value)) && value.vt == VT_LPWSTR) name = value.pwszVal;
		PropVariantClear(&value);
	}
	return name;
}

static std::vector<Device> ActiveDevices(EDataFlow flow)
{
	std::vector<Device> result;
	Com<IMMDeviceEnumerator> enumerator;
	Com<IMMDeviceCollection> devices;
	if (!CreateEnumerator(&enumerator) || FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &devices))) return result;

	UINT count = 0;
	devices->GetCount(&count);
	for (UINT i = 0; i < count; ++i)
	{
		Com<IMMDevice> device;
		LPWSTR id = nullptr;
		if (FAILED(devices->Item(i, &device)) || FAILED(device->GetId(&id))) continue;
		result.push_back({id, FriendlyName(device.p)});
		CoTaskMemFree(id);
	}
	return result;
}

// Index of the device matching `query`: an exact name match, otherwise the first
// name containing it. -1 if none.
static int BestMatch(const std::vector<Device>& devices, const std::wstring& query)
{
	if (query.empty()) return -1;
	const std::wstring wanted = Lower(query);
	int partial = -1;
	for (size_t i = 0; i < devices.size(); ++i)
	{
		const std::wstring name = Lower(devices[i].name);
		if (name == wanted) return (int)i;
		if (partial < 0 && name.find(wanted) != std::wstring::npos) partial = (int)i;
	}
	return partial;
}

static void SetDefaultDevice(Measure* m, EDataFlow flow, const std::wstring& query)
{
	const std::wstring kind = flow == eRender ? L"playback" : L"recording";
	if (query.empty())
	{
		Log(m, LOG_ERROR, L"no " + kind + L" device name given");
		return;
	}

	const std::vector<Device> devices = ActiveDevices(flow);
	const int match = BestMatch(devices, query);
	if (match < 0)
	{
		Log(m, LOG_ERROR, L"no active " + kind + L" device matches \"" + query + L"\"");
		return;
	}

	Com<IPolicyConfig> policy;
	if (FAILED(CoCreateInstance(CLSID_PolicyConfigClient, nullptr, CLSCTX_ALL, IID_IPolicyConfig, (void**)&policy)))
	{
		Log(m, LOG_ERROR, L"could not access Windows audio policy");
		return;
	}
	for (ERole role : {eConsole, eMultimedia, eCommunications})
	{
		policy->SetDefaultEndpoint(devices[match].id.c_str(), role);
	}
	Log(m, LOG_DEBUG, L"default " + kind + L" device is now \"" + devices[match].name + L"\"");
}

static bool ParseFlow(const std::wstring& s, bool& output)
{
	output = _wcsicmp(s.c_str(), L"Output") == 0;
	return output || _wcsicmp(s.c_str(), L"Input") == 0;
}

// Reads Pick1, Pick2, ... ("Output,SpeakerOutput,Speakers output") until one is missing.
static std::vector<Pick> ReadPicks(void* rm)
{
	std::vector<Pick> picks;
	RmReadStringFunc rmReadString = RmApi<RmReadStringFunc>("RmReadString", 16);
	if (!rmReadString) return picks;
	for (int i = 1;; ++i)
	{
		const std::wstring option = L"Pick" + std::to_wstring(i);
		const std::wstring value = rmReadString(rm, option.c_str(), L"", TRUE);
		if (value.empty()) break;

		const size_t first = value.find(L','), second = value.find(L',', first + 1);
		Pick pick;
		if (first == std::wstring::npos || second == std::wstring::npos || !ParseFlow(Trim(value.substr(0, first)), pick.output)) continue;
		pick.variable = Trim(value.substr(first + 1, second - first - 1));
		pick.label = Trim(value.substr(second + 1));
		picks.push_back(pick);
	}
	return picks;
}

// Adds the device list for `pick` to `menu`, using command IDs base+1, base+2, ...
static void AddDeviceItems(Measure* m, HMENU menu, const Pick& pick, const std::vector<Device>& devices, UINT base, std::wstring& current)
{
	static RmReplaceVariablesFunc rmReplaceVariables = RmApi<RmReplaceVariablesFunc>("RmReplaceVariables", 8);
	const std::wstring unset = L"#" + pick.variable + L"#";
	current = rmReplaceVariables ? rmReplaceVariables(m->rm, unset.c_str()) : L"";
	if (current == unset) current.clear();
	const int selected = BestMatch(devices, current);

	for (size_t i = 0; i < devices.size(); ++i)
	{
		AppendMenuW(menu, MF_STRING | ((int)i == selected ? MF_CHECKED : 0), base + i + 1, devices[i].name.c_str());
	}
	if (devices.empty())
	{
		AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"No devices found");
	}
	if (selected < 0 && !current.empty())
	{
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, (L"Current: " + current + L" (not connected)").c_str());
	}
}

static void PickDevice(Measure* m, const std::wstring& args)
{
	std::vector<Pick> picks;
	if (args.empty())
	{
		picks = m->picks;
		if (picks.empty())
		{
			Log(m, LOG_ERROR, L"PickDevice needs Pick1=... options on the measure, or arguments");
			return;
		}
	}
	else
	{
		std::wstring flow, variable;
		SplitFirstWord(args, flow, variable);
		bool output;
		if (!ParseFlow(flow, output) || variable.empty())
		{
			Log(m, LOG_ERROR, L"usage: PickDevice <Output|Input> <variable>");
			return;
		}
		picks.push_back({output, variable, L""});
	}

	RmGetFunc rmGet = RmApi<RmGetFunc>("RmGet", 8);
	RmExecuteFunc rmExecute = RmApi<RmExecuteFunc>("RmExecute", 8);
	if (!rmGet || !rmExecute) return;

	const std::vector<Device> outputs = ActiveDevices(eRender), inputs = ActiveDevices(eCapture);
	const UINT idsPerPick = 1000;

	// One pick: a flat device list. Several: a submenu each, labeled with the current device.
	HMENU menu = CreatePopupMenu();
	std::wstring current;
	if (picks.size() == 1)
	{
		AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, picks[0].output ? L"Playback devices" : L"Recording devices");
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		AddDeviceItems(m, menu, picks[0], picks[0].output ? outputs : inputs, 0, current);
	}
	else
	{
		for (size_t p = 0; p < picks.size(); ++p)
		{
			HMENU submenu = CreatePopupMenu();
			AddDeviceItems(m, submenu, picks[p], picks[p].output ? outputs : inputs, (UINT)p * idsPerPick, current);
			const std::wstring label = picks[p].label + L"\t" + (current.empty() ? L"(not set)" : current);
			AppendMenuW(menu, MF_POPUP, (UINT_PTR)submenu, label.c_str());
		}
	}

	// The owner window must be in the foreground, or the menu won't close when
	// clicking elsewhere. The WM_NULL afterwards is the documented workaround
	// for the menu otherwise needing a second click to dismiss.
	HWND window = (HWND)rmGet(m->rm, RMG_SKINWINDOWHANDLE);
	POINT cursor;
	GetCursorPos(&cursor);
	SetForegroundWindow(window);
	const UINT choice = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, window, nullptr);
	PostMessageW(window, WM_NULL, 0, 0);
	DestroyMenu(menu);
	if (choice == 0) return;

	const Pick& pick = picks[choice / idsPerPick];
	const std::vector<Device>& devices = pick.output ? outputs : inputs;
	const std::wstring name = Quote(devices[choice % idsPerPick - 1].name);
	const std::wstring measureName = (LPCWSTR)rmGet(m->rm, RMG_MEASURENAME);
	std::wstring bangs = L"[!SetVariable " + pick.variable + L" " + name + L"]";
	bangs += L"[!WriteKeyValue Variables " + pick.variable + L" " + name + (m->saveTo.empty() ? L"" : L" " + Quote(m->saveTo)) + L"]";
	bangs += L"[!UpdateMeasure " + Quote(measureName) + L"][!UpdateMeter *][!Redraw]";
	rmExecute(rmGet(m->rm, RMG_SKIN), bangs.c_str());
}

// Gets the volume control (and optionally the name and ID) of the default playback device.
static bool GetPlaybackVolume(IAudioEndpointVolume** volume, std::wstring* name, std::wstring* id = nullptr)
{
	Com<IMMDeviceEnumerator> enumerator;
	Com<IMMDevice> device;
	if (!CreateEnumerator(&enumerator) || FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) return false;
	if (name) *name = FriendlyName(device.p);
	LPWSTR deviceId = nullptr;
	if (id && SUCCEEDED(device->GetId(&deviceId)))
	{
		*id = deviceId;
		CoTaskMemFree(deviceId);
	}
	return SUCCEEDED(device->Activate(IID_IAudioEndpointVolume_, CLSCTX_ALL, nullptr, (void**)volume));
}

PLUGIN_EXPORT void Initialize(void** data, void* rm)
{
	Measure* m = new Measure;
	m->rm = rm;
	m->comInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
	*data = m;
}

PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
{
	Measure* m = (Measure*)data;
	RmReadStringFunc rmReadString = RmApi<RmReadStringFunc>("RmReadString", 16);
	m->saveTo = rmReadString ? rmReadString(rm, L"SaveTo", L"", TRUE) : L"";
	m->picks = ReadPicks(rm);
	m->matchOutput = rmReadString ? rmReadString(rm, L"MatchOutput", L"", TRUE) : L"";
	*maxValue = 100.0;
}

PLUGIN_EXPORT double Update(void* data)
{
	Measure* m = (Measure*)data;
	m->deviceName.clear();
	std::wstring defaultId;
	Com<IAudioEndpointVolume> volume;
	float level = 0.0f;
	if (GetPlaybackVolume(&volume, &m->deviceName, &defaultId)) volume->GetMasterVolumeLevelScalar(&level);

	if (!m->matchOutput.empty())
	{
		// Same matching as SetOutput, so "is this device active" agrees with what a switch would pick.
		const std::vector<Device> devices = ActiveDevices(eRender);
		const int match = BestMatch(devices, m->matchOutput);
		return match >= 0 && devices[match].id == defaultId ? 1.0 : 0.0;
	}
	return (double)(int)(level * 100.0f + 0.5f);
}

PLUGIN_EXPORT LPCWSTR GetString(void* data)
{
	return ((Measure*)data)->deviceName.c_str();
}

PLUGIN_EXPORT void ExecuteBang(void* data, LPCWSTR args)
{
	Measure* m = (Measure*)data;
	std::wstring command, arg;
	SplitFirstWord(Trim(args), command, arg);

	if (_wcsicmp(command.c_str(), L"SetOutput") == 0)
	{
		// No device yet means the skin hasn't been set up, so let the user choose.
		if (arg.empty()) PickDevice(m, L"");
		else SetDefaultDevice(m, eRender, arg);
	}
	else if (_wcsicmp(command.c_str(), L"SetInput") == 0)
	{
		// No mic means "leave the mic alone".
		if (!arg.empty()) SetDefaultDevice(m, eCapture, arg);
	}
	else if (_wcsicmp(command.c_str(), L"PickDevice") == 0)
	{
		PickDevice(m, arg);
	}
	else if (_wcsicmp(command.c_str(), L"ChangeVolume") == 0)
	{
		Com<IAudioEndpointVolume> volume;
		float level = 0.0f;
		if (!GetPlaybackVolume(&volume, nullptr) || FAILED(volume->GetMasterVolumeLevelScalar(&level))) return;
		level += (float)_wtof(arg.c_str()) / 100.0f;
		volume->SetMasterVolumeLevelScalar(level < 0.0f ? 0.0f : level > 1.0f ? 1.0f : level, nullptr);
	}
	else if (_wcsicmp(command.c_str(), L"ToggleMute") == 0)
	{
		Com<IAudioEndpointVolume> volume;
		BOOL muted = FALSE;
		if (!GetPlaybackVolume(&volume, nullptr) || FAILED(volume->GetMute(&muted))) return;
		volume->SetMute(!muted, nullptr);
	}
	else
	{
		Log(m, LOG_WARNING, L"unknown command \"" + std::wstring(args) + L"\"");
	}
}

PLUGIN_EXPORT void Finalize(void* data)
{
	Measure* m = (Measure*)data;
	if (m->comInitialized) CoUninitialize();
	delete m;
}

// dx10HW.cpp: implementation of the DX10 specialisation of CHW.
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop
#include <VersionHelpers.h>
#pragma warning(disable:4995)
#include <d3dx9.h>
#include <dxgi1_4.h>
#pragma warning(default:4995)
#include "../xrRender/HW.h"
#include "../../xrEngine/XR_IOConsole.h"
#include "../../xrCore/xrAPI.h"

#include "StateManager\dx10SamplerStateCache.h"
#include "StateManager\dx10StateCache.h"

struct DM1024
{
	DEVMODE		sys_mode;
	string1024	sm_buffer;
} g_dm;

ENGINE_API BOOL isGraphicDebugging;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CHW HW;

CHW::CHW() : m_pAdapter(nullptr), pDevice(nullptr), m_move_window(true)	// NULL cuz it's struct
{
	Device.seqAppActivate.Add(this);
	Device.seqAppDeactivate.Add(this);
}

CHW::~CHW()
{
	Device.seqAppActivate.Remove(this);
	Device.seqAppDeactivate.Remove(this);
}

//////////////////////////////////////////////////////////////////////
// Functions
//////////////////////////////////////////////////////////////////////
void CHW::CreateD3D()
{
	// Init g_dm
	std::memset(&g_dm, 0, sizeof(g_dm));
	g_dm.sys_mode.dmSize = sizeof(g_dm.sys_mode);
	g_dm.sys_mode.dmDriverExtra = sizeof(g_dm.sm_buffer);
	EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &g_dm.sys_mode);

	m_bUsePerfhud = false;

	// Init pAdapter
	if (IsWindows10OrGreater())
	{
		IDXGIFactory4 * pFactory;
		CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)(&pFactory));
		pFactory->EnumAdapters1(0, &m_pAdapter);
		_RELEASE(pFactory);
	}
	else if (IsWindows8Point1OrGreater())
	{
		IDXGIFactory3 * pFactory;
		CreateDXGIFactory1(__uuidof(IDXGIFactory3), (void**)(&pFactory));
		pFactory->EnumAdapters1(0, &m_pAdapter);
		_RELEASE(pFactory);
	}
	else if (IsWindows8OrGreater())
	{
		IDXGIFactory2 * pFactory;
		CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)(&pFactory));
		pFactory->EnumAdapters1(0, &m_pAdapter);
		_RELEASE(pFactory);
	}
	else
	{
		IDXGIFactory1 * pFactory;
		CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&pFactory));
		pFactory->EnumAdapters1(0, &m_pAdapter);
		_RELEASE(pFactory);
	}
}

void CHW::DestroyD3D()
{
	_SHOW_REF("refCount:m_pAdapter", m_pAdapter);
	_RELEASE(m_pAdapter);
}

void CHW::CreateDevice(HWND m_hWnd, bool move_window)
{
	m_move_window = move_window;
	CreateD3D();

	// General - select adapter and device
	BOOL bWindowed = !psDeviceFlags.is(rsFullscreen) || strstr(Core.Params, "-editor");

	m_DriverType = Caps.bForceGPU_REF ? D3D_DRIVER_TYPE_REFERENCE : D3D_DRIVER_TYPE_HARDWARE;

	if (m_bUsePerfhud)
		m_DriverType = D3D_DRIVER_TYPE_REFERENCE;

	// Display the name of video board
	DXGI_ADAPTER_DESC1 Desc;
	R_CHK(m_pAdapter->GetDesc1(&Desc));
	//	Warning: Desc.Description is wide string
	Msg("* GPU [vendor:%X]-[device:%X]: %S", Desc.VendorId, Desc.DeviceId, Desc.Description);
	Caps.id_vendor = Desc.VendorId;
	Caps.id_device = Desc.DeviceId;

	// MatthewKush to all: Please change to DXGI_SWAP_CHAIN_DESC1 (for lots of reasons)
	DXGI_SWAP_CHAIN_DESC &sd = m_ChainDesc;
	memset(&sd, 0, sizeof(sd));

	SelectResolution(sd.BufferDesc.Width, sd.BufferDesc.Height, bWindowed);

	//	TODO: DX10: implement dynamic format selection
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // Prep for HDR10; breaks nothing
	sd.BufferCount = psDeviceFlags.test(rsTripleBuffering) ? 2 : 1;

	// Multisample
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;

	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	sd.OutputWindow = m_hWnd;
	sd.Windowed = bWindowed;
	sd.BufferDesc.RefreshRate = SelectRefresh(sd.BufferDesc.Width, sd.BufferDesc.Height, sd.BufferDesc.Format);

	//	Additional set up
	UINT createDeviceFlags = 0;
	if (isGraphicDebugging)
	{
#ifdef USE_DX11
		createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#else
		createDeviceFlags |= D3D10_CREATE_DEVICE_DEBUG;
#endif
	}
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	HRESULT R;
#ifdef USE_DX11
	D3D_FEATURE_LEVEL pFeatureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0
	};

	R = D3D11CreateDeviceAndSwapChain(
		NULL,
		m_DriverType,
		NULL,
		createDeviceFlags,
		pFeatureLevels,
		sizeof(pFeatureLevels) / sizeof(pFeatureLevels[0]),
		D3D11_SDK_VERSION,
		&sd,
		&m_pSwapChain,
		&pDevice,
		&FeatureLevel,
		&pContext
	);

	if (FAILED(R))
	{

		R_CHK(D3D11CreateDeviceAndSwapChain(
			NULL,
			m_DriverType,
			NULL,
			createDeviceFlags,
			&pFeatureLevels[1],
			sizeof(pFeatureLevels) / sizeof(pFeatureLevels[0] - 1),
			D3D11_SDK_VERSION,
			&sd,
			&m_pSwapChain,
			&pDevice,
			&FeatureLevel,
			&pContext
			)
		);
	}

	D3D11_FEATURE_DATA_THREADING threadingFeature;
	R_CHK(pDevice->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threadingFeature, sizeof(threadingFeature)));
        //MatthewKush to all: If we keep data threading, make use of async resources. Otherwise we
        //should make it a single-threaded device.

	if (IsWindows10OrGreater())
	{
	        D3D11_FEATURE_DATA_ARCHITECTURE_INFO arch;
	        R_CHK(pDevice->CheckFeatureSupport(D3D11_FEATURE_ARCHITECTURE_INFO, &arch, sizeof(arch)));
                arch.TileBasedDeferredRenderer == TRUE;

		IDXGIDevice3 * pDXGIDevice;
		R_CHK(pDevice->QueryInterface(__uuidof(IDXGIDevice3), (void **)&pDXGIDevice));

		IDXGIAdapter3 * pDXGIAdapter;
		R_CHK(pDXGIDevice->GetParent(__uuidof(IDXGIAdapter3), (void **)&pDXGIAdapter));

		R = pDXGIDevice->SetMaximumFrameLatency(1);
	}
	else if (IsWindows8OrGreater())
	{
	        D3D11_FEATURE_DATA_ARCHITECTURE_INFO arch;
	        R_CHK(pDevice->CheckFeatureSupport(D3D11_FEATURE_ARCHITECTURE_INFO, &arch, sizeof(arch)));
                arch.TileBasedDeferredRenderer == TRUE;

		IDXGIDevice2 * pDXGIDevice;
		R_CHK(pDevice->QueryInterface(__uuidof(IDXGIDevice2), (void **)&pDXGIDevice));

		IDXGIAdapter2 * pDXGIAdapter;
		R_CHK(pDXGIDevice->GetParent(__uuidof(IDXGIAdapter2), (void **)&pDXGIAdapter));

		R = pDXGIDevice->SetMaximumFrameLatency(1);
	}
	else
	{
		IDXGIDevice1 * pDXGIDevice;
		R_CHK(pDevice->QueryInterface(__uuidof(IDXGIDevice1), (void **)&pDXGIDevice));

		IDXGIAdapter1 * pDXGIAdapter;
		R_CHK(pDXGIDevice->GetParent(__uuidof(IDXGIAdapter1), (void **)&pDXGIAdapter));

		R = pDXGIDevice->SetMaximumFrameLatency(1);
	}
#else
	R = D3DX10CreateDeviceAndSwapChain(m_pAdapter, m_DriverType, NULL, createDeviceFlags, &sd, &m_pSwapChain, &pDevice);

	pContext = pDevice;
	FeatureLevel = D3D_FEATURE_LEVEL_10_0;
	if (SUCCEEDED(R))
	{
		if (SUCCEEDED(D3DX10GetFeatureLevel1(pDevice, &pDevice1)))
		{
			FeatureLevel = D3D_FEATURE_LEVEL_10_1;
			pContext1 = pDevice1;
		}
	}
#endif

	if (FAILED(R))
	{
		// Fatal error! Cannot create rendering device AT STARTUP !!!
		Msg("Failed to initialize graphics hardware.\n"
			"Please try to restart the game.\n"
			"CreateDevice returned 0x%08x", R);
		FlushLog();
		FATAL("Failed to initialize graphics hardware.\nPlease try to restart the game.");
	};
	R_CHK(R);

	_SHOW_REF("* CREATE: DeviceREF:", HW.pDevice);
	//	Create render target and depth-stencil views here
	UpdateViews();

	size_t	memory = Desc.DedicatedVideoMemory;
	Msg("* Texture memory: %d M", memory / (1024 * 1024));
	FillVidModeList();
}

void CHW::DestroyDevice()
{
	//	Destroy state managers
	StateManager.Reset();
	RSManager.ClearStateArray();
	DSSManager.ClearStateArray();
	BSManager.ClearStateArray();
	SSManager.ClearStateArray();

	_SHOW_REF("refCount:pBaseZB", pBaseZB);
	_RELEASE(pBaseZB);

	_SHOW_REF("refCount:pBaseRT", pBaseRT);
	_RELEASE(pBaseRT);
	//	Must switch to windowed mode to release swap chain
	if (!m_ChainDesc.Windowed) m_pSwapChain->SetFullscreenState(FALSE, NULL);
	_SHOW_REF("refCount:m_pSwapChain", m_pSwapChain);
	_RELEASE(m_pSwapChain);

#ifdef USE_DX11
	_RELEASE(pContext);
#endif

#ifndef USE_DX11
	_RELEASE(HW.pDevice1);
#endif
	_SHOW_REF("refCount:DeviceREF:", HW.pDevice);
	_RELEASE(HW.pDevice);


	DestroyD3D();

#ifndef _EDITOR
	FreeVidModeList();
#endif
}

//////////////////////////////////////////////////////////////////////
// Resetting device
//////////////////////////////////////////////////////////////////////
void CHW::Reset(HWND hwnd)
{
	DXGI_SWAP_CHAIN_DESC &sd = m_ChainDesc;

	bool bWindowed = !psDeviceFlags.is(rsFullscreen) || strstr(Core.Params, "-editor");

	sd.Windowed		= bWindowed;
	sd.BufferCount	= psDeviceFlags.test(rsTripleBuffering) ? 2 : 1;

	m_pSwapChain->SetFullscreenState(!bWindowed, NULL);

	DXGI_MODE_DESC	&desc = m_ChainDesc.BufferDesc;

	SelectResolution(desc.Width, desc.Height, bWindowed);
	desc.RefreshRate = SelectRefresh(desc.Width, desc.Height, desc.Format);

	CHK_DX(m_pSwapChain->ResizeTarget(&desc));

	_SHOW_REF("refCount:pBaseZB", pBaseZB);
	_SHOW_REF("refCount:pBaseRT", pBaseRT);

	_RELEASE(pBaseZB);
	_RELEASE(pBaseRT);

	CHK_DX(m_pSwapChain->ResizeBuffers(
		sd.BufferCount,
		desc.Width,
		desc.Height,
		desc.Format,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
		)
	);

	UpdateViews();
}

void CHW::ResizeWindowProc(WORD h, WORD w)
{
	Reset(NULL);
}


void CHW::SelectResolution(u32 &dwWidth, u32 &dwHeight, BOOL bWindowed)
{
	FillVidModeList();

	if (bWindowed)
	{
		dwWidth = psCurrentVidMode[0];
		dwHeight = psCurrentVidMode[1];
	}
	else
	{
		string64 buff;
		xr_sprintf(buff, sizeof(buff), "%dx%d", psCurrentVidMode[0], psCurrentVidMode[1]);

		if (_ParseItem(buff, vid_mode_token) == u32(-1)) //not found
		{ //select safe
			xr_sprintf(buff, sizeof(buff), "vid_mode %s", vid_mode_token[0].name);
			Console->Execute(buff);
		}

		dwWidth = psCurrentVidMode[0];
		dwHeight = psCurrentVidMode[1];
	}
}

//	TODO: DX10: check if we need these
DXGI_RATIONAL CHW::SelectRefresh(u32 dwWidth, u32 dwHeight, DXGI_FORMAT fmt)
{
	DXGI_RATIONAL	res;

	res.Numerator = 60;
	res.Denominator = 1;

	float	CurrentFreq = 60.0f;

	if (psDeviceFlags.is(rsRefresh60hz))
	{
		return res;
	}
	else if (psDeviceFlags.is(rsRefresh120hz))
	{
		res.Numerator = 120;
		return res;
	}
	else
	{
		xr_vector<DXGI_MODE_DESC>	modes;

		IDXGIOutput *pOutput;
		m_pAdapter->EnumOutputs(0, &pOutput);
		VERIFY(pOutput);

		UINT num = 0;
		DXGI_FORMAT format = fmt;
		UINT flags = 0;

		// Get the number of display modes available
		pOutput->GetDisplayModeList(format, flags, &num, 0);

		// Get the list of display modes
		modes.resize(num);
		pOutput->GetDisplayModeList(format, flags, &num, &modes.front());

		_RELEASE(pOutput);

		for (u32 i = 0; i<num; ++i)
		{
			DXGI_MODE_DESC &desc = modes[i];

			if ((desc.Width == dwWidth) && (desc.Height == dwHeight))
			{
				VERIFY(desc.RefreshRate.Denominator);
				float TempFreq = float(desc.RefreshRate.Numerator) / float(desc.RefreshRate.Denominator);
				if (TempFreq > CurrentFreq)
				{
					CurrentFreq = TempFreq;
					res = desc.RefreshRate;
				}

				// Select desktop frequency
				if (TempFreq == g_dm.sys_mode.dmDisplayFrequency)
				{
					res = desc.RefreshRate;
					break;
				}
			}
		}

		return res;
	}
}

void CHW::OnAppActivate()
{
	if (m_pSwapChain && !m_ChainDesc.Windowed)
	{
		ShowWindow(m_ChainDesc.OutputWindow, SW_RESTORE);
		m_pSwapChain->SetFullscreenState(TRUE, NULL);
	}
}

void CHW::OnAppDeactivate()
{
	if (m_pSwapChain && !m_ChainDesc.Windowed)
	{
		m_pSwapChain->SetFullscreenState(FALSE, NULL);
		ShowWindow(m_ChainDesc.OutputWindow, SW_MINIMIZE);
	}
}


bool CHW::IsFormatSupported(D3DFORMAT fmt, DWORD type, DWORD usage)
{
	VERIFY(!"Implement CHW::support");
	return true;
}

struct _uniq_mode
{
	_uniq_mode(LPCSTR v) :_val(v) {}
	LPCSTR _val;
	bool operator() (LPCSTR _other) { return !stricmp(_val, _other); }
};

#ifndef _EDITOR
void CHW::FreeVidModeList()
{
	for (int i = 0; vid_mode_token[i].name; i++)
	{
		xr_free(vid_mode_token[i].name);
	}
	xr_free(vid_mode_token);
	vid_mode_token = NULL;
}

void CHW::FillVidModeList()
{
	if (vid_mode_token != NULL)
		return;

	xr_vector<LPCSTR>	_tmp;
	xr_vector<DXGI_MODE_DESC>	modes;

	IDXGIOutput *pOutput;
	m_pAdapter->EnumOutputs(0, &pOutput);
	VERIFY(pOutput);

	UINT num = 0;
	DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;//Don't touch
	UINT flags = 0;

	// Get the number of display modes available
	pOutput->GetDisplayModeList(format, flags, &num, 0);

	// Get the list of display modes
	modes.resize(num);
	pOutput->GetDisplayModeList(format, flags, &num, &modes.front());

	_RELEASE(pOutput);

	for (u32 i = 0; i<num; ++i)
	{
		DXGI_MODE_DESC &desc = modes[i];
		string32		str;

		xr_sprintf(str, sizeof(str), "%dx%d", desc.Width, desc.Height);

		if (_tmp.end() != std::find_if(_tmp.begin(), _tmp.end(), _uniq_mode(str)))
			continue;

		_tmp.push_back(NULL);
		_tmp.back() = xr_strdup(str);
	}

	u32 _cnt = _tmp.size() + 1;

	vid_mode_token = xr_alloc<xr_token>(_cnt);

	vid_mode_token[_cnt - 1].id = -1;
	vid_mode_token[_cnt - 1].name = NULL;

#ifdef DEBUG
	Msg("Available video modes[%d]:", _tmp.size());
#endif // DEBUG
	for (u32 i = 0; i<_tmp.size(); ++i)
	{
		vid_mode_token[i].id = i;
		vid_mode_token[i].name = _tmp[i];
#ifdef DEBUG
		Msg("[%s]", _tmp[i]);
#endif // DEBUG
	}
}

void CHW::UpdateViews()
{
	DXGI_SWAP_CHAIN_DESC &sd = m_ChainDesc;
	HRESULT R;

	ID3DTexture2D *pBuffer;
	R = m_pSwapChain->GetBuffer(0, __uuidof(ID3DTexture2D), (LPVOID*)&pBuffer);
	R_CHK(R);

	R = pDevice->CreateRenderTargetView(pBuffer, NULL, &pBaseRT);
	pBuffer->Release();
	R_CHK(R);

	//	Create Depth/stencil buffer
	//	HACK: DX10: hard depth buffer format
	ID3DTexture2D* pDepthStencil = NULL;
	D3D_TEXTURE2D_DESC descDepth;
	descDepth.Width = sd.BufferDesc.Width;
	descDepth.Height = sd.BufferDesc.Height;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	descDepth.SampleDesc.Count = 1;
	descDepth.SampleDesc.Quality = 0;
	descDepth.Usage = D3D_USAGE_DEFAULT;
	descDepth.BindFlags = D3D_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	R = pDevice->CreateTexture2D(&descDepth /* Texture desc */, NULL /* Initial data */, &pDepthStencil); // [out] Texture
	R_CHK(R);

	//	Create Depth/stencil view
	R = pDevice->CreateDepthStencilView(pDepthStencil, NULL, &pBaseZB);
	R_CHK(R);

	_RELEASE(pDepthStencil);
}
#endif

#define SDL_MAIN_HANDLED
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "playback_controller.h"
#include "playback_runtime.h"
#include "video_frame.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "comdlg32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
struct UiEventQueue {
	void push(std::function<void()> event)
	{
		std::lock_guard<std::mutex> lock(mutex);
		events.push(std::move(event));
	}

	void drain()
	{
		std::queue<std::function<void()>> pending;
		{
			std::lock_guard<std::mutex> lock(mutex);
			std::swap(pending, events);
		}
		while (!pending.empty()) {
			pending.front()();
			pending.pop();
		}
	}

	std::mutex mutex;
	std::queue<std::function<void()>> events;
};

struct D3DObjects {
	ID3D11Device* device = nullptr;
	ID3D11DeviceContext* context = nullptr;
	IDXGISwapChain* swapChain = nullptr;
	ID3D11RenderTargetView* renderTarget = nullptr;
};

struct VideoTexture {
	ID3D11Texture2D* texture = nullptr;
	ID3D11ShaderResourceView* view = nullptr;
	int width = 0;
	int height = 0;

	void release()
	{
		if (view) {
			view->Release();
			view = nullptr;
		}
		if (texture) {
			texture->Release();
			texture = nullptr;
		}
		width = 0;
		height = 0;
	}
};

struct AppState {
	HWND hwnd = nullptr;
	D3DObjects d3d;
	VideoTexture videoTexture;
	std::shared_ptr<VideoFrame> frame;
	std::string currentFile;
	int totalSeconds = 0;
	int playSeconds = 0;
	double volume = 1.0;
	bool paused = false;
	bool running = true;
	bool fullscreenVideo = false;
};

D3DObjects* g_d3d = nullptr;

std::string toUtf8(const wchar_t* text)
{
	if (!text || !*text)
		return {};
	const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (size <= 1)
		return {};
	std::string out(static_cast<size_t>(size - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
	return out;
}

std::string openMediaDialog(HWND owner)
{
	wchar_t fileName[MAX_PATH] = {};
	OPENFILENAMEW ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = owner;
	ofn.lpstrFile = fileName;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter = L"Media Files\0*.mp4;*.mkv;*.avi;*.flv;*.wmv;*.rmvb;*.3gp;*.mov;*.mp3;*.aac;*.wav\0All Files\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	return GetOpenFileNameW(&ofn) ? toUtf8(fileName) : std::string{};
}

void createRenderTarget(D3DObjects& d3d)
{
	ID3D11Texture2D* backBuffer = nullptr;
	d3d.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
	if (backBuffer) {
		d3d.device->CreateRenderTargetView(backBuffer, nullptr, &d3d.renderTarget);
		backBuffer->Release();
	}
}

void cleanupRenderTarget(D3DObjects& d3d)
{
	if (d3d.renderTarget) {
		d3d.renderTarget->Release();
		d3d.renderTarget = nullptr;
	}
}

bool createDeviceD3D(HWND hwnd, D3DObjects& d3d)
{
	DXGI_SWAP_CHAIN_DESC desc{};
	desc.BufferCount = 2;
	desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.OutputWindow = hwnd;
	desc.SampleDesc.Count = 1;
	desc.Windowed = TRUE;
	desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevelArray[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
	const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
		featureLevelArray, 2, D3D11_SDK_VERSION, &desc, &d3d.swapChain, &d3d.device, &featureLevel, &d3d.context);
	if (FAILED(hr))
		return false;

	createRenderTarget(d3d);
	return true;
}

void cleanupDeviceD3D(D3DObjects& d3d)
{
	cleanupRenderTarget(d3d);
	if (d3d.swapChain) {
		d3d.swapChain->Release();
		d3d.swapChain = nullptr;
	}
	if (d3d.context) {
		d3d.context->Release();
		d3d.context = nullptr;
	}
	if (d3d.device) {
		d3d.device->Release();
		d3d.device = nullptr;
	}
}

bool updateVideoTexture(AppState& app)
{
	const auto& frame = app.frame;
	if (!frame || frame->bgra.empty() || frame->width <= 0 || frame->height <= 0)
		return false;

	if (app.videoTexture.width != frame->width || app.videoTexture.height != frame->height) {
		app.videoTexture.release();

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = static_cast<UINT>(frame->width);
		desc.Height = static_cast<UINT>(frame->height);
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		if (FAILED(app.d3d.device->CreateTexture2D(&desc, nullptr, &app.videoTexture.texture)))
			return false;
		if (FAILED(app.d3d.device->CreateShaderResourceView(app.videoTexture.texture, nullptr, &app.videoTexture.view))) {
			app.videoTexture.release();
			return false;
		}
		app.videoTexture.width = frame->width;
		app.videoTexture.height = frame->height;
	}

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(app.d3d.context->Map(app.videoTexture.texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		return false;

	const auto* src = frame->bgra.data();
	auto* dst = static_cast<unsigned char*>(mapped.pData);
	const int copyBytes = std::min<int>(frame->bytesPerLine, static_cast<int>(mapped.RowPitch));
	for (int y = 0; y < frame->height; ++y) {
		std::memcpy(dst + y * mapped.RowPitch, src + y * frame->bytesPerLine, static_cast<size_t>(copyBytes));
	}
	app.d3d.context->Unmap(app.videoTexture.texture, 0);
	return true;
}

std::string formatTime(int seconds)
{
	char buffer[16] = {};
	std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", seconds / 3600, (seconds / 60) % 60, seconds % 60);
	return buffer;
}

void startFile(AppState& app, PlaybackController& controller, const std::string& file)
{
	if (file.empty())
		return;
	app.currentFile = file;
	app.playSeconds = 0;
	app.totalSeconds = 0;
	controller.play(file);
}

void renderUi(AppState& app, PlaybackController& controller)
{
	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
	ImGui::Begin("Player", nullptr, flags);

	const float footerHeight = app.fullscreenVideo ? 0.0f : 118.0f;
	const ImVec2 available = ImGui::GetContentRegionAvail();
	const ImVec2 videoArea(available.x, std::max(1.0f, available.y - footerHeight));

	ImGui::BeginChild("Video", videoArea, false, ImGuiWindowFlags_NoScrollbar);
	if (updateVideoTexture(app) && app.videoTexture.view) {
		float drawW = videoArea.x;
		float drawH = drawW * app.videoTexture.height / static_cast<float>(app.videoTexture.width);
		if (drawH > videoArea.y) {
			drawH = videoArea.y;
			drawW = drawH * app.videoTexture.width / static_cast<float>(app.videoTexture.height);
		}
		ImGui::SetCursorPos(ImVec2((videoArea.x - drawW) * 0.5f, (videoArea.y - drawH) * 0.5f));
		ImGui::Image(reinterpret_cast<ImTextureID>(app.videoTexture.view), ImVec2(drawW, drawH));
	} else {
		ImGui::SetCursorPos(ImVec2(24.0f, 24.0f));
		ImGui::TextUnformatted("Open a media file to start playback.");
	}
	ImGui::EndChild();

	if (!app.fullscreenVideo) {
		float progress = app.totalSeconds > 0 ? app.playSeconds / static_cast<float>(app.totalSeconds) : 0.0f;
		ImGui::SetNextItemWidth(-1);
		if (ImGui::SliderFloat("##progress", &progress, 0.0f, 1.0f, "")) {
			controller.seek(progress);
		}

		if (ImGui::Button("Open", ImVec2(76, 32)))
			startFile(app, controller, openMediaDialog(app.hwnd));
		ImGui::SameLine();
		if (ImGui::Button("-5s", ImVec2(54, 32)))
			controller.seekBack();
		ImGui::SameLine();
		if (ImGui::Button(app.paused ? "Play" : "Pause", ImVec2(76, 32)))
			controller.pause();
		ImGui::SameLine();
		if (ImGui::Button("Stop", ImVec2(60, 32)))
			controller.stop();
		ImGui::SameLine();
		if (ImGui::Button("+5s", ImVec2(54, 32)))
			controller.seekForward();
		ImGui::SameLine();
		if (ImGui::Button("Vol -", ImVec2(60, 32)))
			controller.subVolume();
		ImGui::SameLine();
		if (ImGui::Button("Vol +", ImVec2(60, 32)))
			controller.addVolume();
		ImGui::SameLine();
		ImGui::Checkbox("Clean view", &app.fullscreenVideo);

		ImGui::Text("%s / %s   Volume %.0f%%   %s",
			formatTime(app.playSeconds).c_str(),
			formatTime(app.totalSeconds).c_str(),
			app.volume * 100.0,
			app.currentFile.empty() ? "" : app.currentFile.c_str());
	} else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		app.fullscreenVideo = false;
	}

	ImGui::End();
}

LRESULT WINAPI wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
		return true;

	switch (msg) {
	case WM_SIZE:
		if (g_d3d && g_d3d->device && wParam != SIZE_MINIMIZED) {
			cleanupRenderTarget(*g_d3d);
			g_d3d->swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
			createRenderTarget(*g_d3d);
		}
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU)
			return 0;
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}
}

int main()
{
	WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, wndProc, 0L, 0L, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr,
		L"myplayer_imgui", nullptr};
	RegisterClassExW(&wc);

	AppState app;
	app.hwnd = CreateWindowW(wc.lpszClassName, L"MyPlayer ImGui", WS_OVERLAPPEDWINDOW,
		100, 100, 1280, 760, nullptr, nullptr, wc.hInstance, nullptr);
	if (!app.hwnd)
		return 1;

	g_d3d = &app.d3d;
	if (!createDeviceD3D(app.hwnd, app.d3d)) {
		DestroyWindow(app.hwnd);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	ShowWindow(app.hwnd, SW_SHOWDEFAULT);
	UpdateWindow(app.hwnd);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(app.hwnd);
	ImGui_ImplDX11_Init(app.d3d.device, app.d3d.context);

	UiEventQueue uiEvents;
	std::vector<sigslot::scoped_connection> connections;
	auto runtime = PlaybackRuntime::Create();
	if (!runtime) {
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		cleanupDeviceD3D(app.d3d);
		DestroyWindow(app.hwnd);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return 1;
	}
	PlaybackController& controller = runtime->controller();

	connections.emplace_back(runtime->SigVideoFrame.connect([&](std::shared_ptr<VideoFrame> frame) {
		uiEvents.push([&app, frame]() { app.frame = frame; });
	}));
	connections.emplace_back(runtime->SigVideoTotalSeconds.connect([&](int seconds) {
		uiEvents.push([&app, seconds]() { app.totalSeconds = seconds; });
	}));
	connections.emplace_back(runtime->SigVideoPlaySeconds.connect([&](int seconds) {
		uiEvents.push([&app, seconds]() { app.playSeconds = seconds; });
	}));
	connections.emplace_back(runtime->SigVideoVolume.connect([&](double volume) {
		uiEvents.push([&app, volume]() { app.volume = volume; });
	}));
	connections.emplace_back(runtime->SigPauseStat.connect([&](bool paused) {
		uiEvents.push([&app, paused]() { app.paused = paused; });
	}));
	connections.emplace_back(runtime->SigStopFinished.connect([&]() {
		uiEvents.push([&app]() {
			app.frame.reset();
			app.videoTexture.release();
			app.playSeconds = 0;
			app.totalSeconds = 0;
		});
	}));

	while (app.running) {
		MSG msg;
		while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
			if (msg.message == WM_QUIT)
				app.running = false;
		}
		if (!app.running)
			break;

		uiEvents.drain();
		if (GetAsyncKeyState(VK_SPACE) & 1)
			controller.pause();
		if (GetAsyncKeyState(VK_LEFT) & 1)
			controller.seekBack();
		if (GetAsyncKeyState(VK_RIGHT) & 1)
			controller.seekForward();
		if (GetAsyncKeyState('O') & 1)
			startFile(app, controller, openMediaDialog(app.hwnd));

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		renderUi(app, controller);
		ImGui::Render();

		const float clearColor[4] = {0.02f, 0.02f, 0.025f, 1.0f};
		app.d3d.context->OMSetRenderTargets(1, &app.d3d.renderTarget, nullptr);
		app.d3d.context->ClearRenderTargetView(app.d3d.renderTarget, clearColor);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		app.d3d.swapChain->Present(1, 0);
	}

	connections.clear();
	controller.stopAndWait();
	app.videoTexture.release();
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	cleanupDeviceD3D(app.d3d);
	DestroyWindow(app.hwnd);
	UnregisterClassW(wc.lpszClassName, wc.hInstance);
	return 0;
}

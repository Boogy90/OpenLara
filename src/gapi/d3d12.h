#ifndef H_GAPI_D3D12
#define H_GAPI_D3D12

#include "core.h"
#include <d3d12.h>
#include <dxgi1_6.h>

#define USE_PIX
#include <pix.h>

struct Marker
{
	Marker(const char* title, ID3D12GraphicsCommandList* cl)
		:commandlist(cl){
		if(cl)
			PIXBeginEvent(cl, 0, title);
	}

	~Marker() {
		if(commandlist)
			PIXEndEvent(commandlist);
	}
	ID3D12GraphicsCommandList* commandlist = nullptr;
};

#define PROFILE_MARKER(title) Marker marker(title, GAPI::inFrame ? GAPI::commandList : nullptr)
#define PROFILE_LABEL(id, child, label)
#define PROFILE_TIMING(time)

extern ID3D12Device* osDevice;
extern IDXGISwapChain3* osSwapChain;

namespace GAPI {
	using namespace Core;

	typedef ::Vertex Vertex;

	ID3D12Resource* depthBuffer = nullptr;

	ID3D12CommandAllocator* commandAllocator[2] = { nullptr, nullptr };
	ID3D12GraphicsCommandList* commandList = nullptr;
	ID3D12CommandQueue* commandQueue = nullptr;
	ID3D12Fence* fence = nullptr;
	ID3D12DescriptorHeap* rtvHeap = nullptr;
	ID3D12DescriptorHeap* dsvHeap = nullptr;
	bool inFrame = false;
	UINT64 fenceValue = 2;
	UINT frameIndex = 0;

	// Shader
#include "shaders/d3d11/shaders.h"

	struct Texture {
		int       width, height, depth, origWidth, origHeight, origDepth;
		TexFormat fmt;
		uint32    opt;

		Texture(int width, int height, int depth, uint32 opt) : width(width), height(height), depth(depth), origWidth(width), origHeight(height), origDepth(depth), fmt(FMT_RGBA), opt(opt) {

		}

		void init(void* data) {
		}

		void deinit() {
		}

		void generateMipMap() {
		}

		void update(void* data) {
		}

		void bind(int sampler) {
			if (Core::active.textures[sampler] != this) {
				Core::active.textures[sampler] = this;
			}
		}

		void unbind(int sampler) {
			if (Core::active.textures[sampler]) {
				Core::active.textures[sampler] = NULL;
			}
		}

		void setFilterQuality(int value) {}
	};

	struct Shader {

		void init(Core::Pass pass, int type, int* def, int defCount) {
		}

		void deinit() {
		}

		void bind() {
			if (Core::active.shader != this) {
				Core::active.shader = this;
				
			}
		}

		void validate() {
		}

		void setParam(UniformType uType, float* value, int count) {
		}

		void setParam(UniformType uType, const vec4& value, int count = 1) {
			setParam(uType, (float*)&value, count);
		}

		void setParam(UniformType uType, const mat4& value, int count = 1) {
			setParam(uType, (float*)&value, count * 4);
		}
	};

	struct Mesh {

		bool dynamic;

		Mesh(bool dynamic) : dynamic(dynamic) {
			
		}

		void init(Index* indices, int iCount, ::Vertex* vertices, int vCount, int aCount) {
		}

		void deinit() {
		}

		void update(Index* indices, int iCount, ::Vertex* vertices, int vCount) {
		}

		void bind(const MeshRange& range) const {
		}

		void initNextRange(MeshRange& range, int& aIndex) const {
			range.aIndex = -1;
		}
	};

	// Functions
	void init() {
		support.maxAniso = 8;
		support.maxVectors = 16;
		support.shaderBinary = true;
		support.VAO = false; // SHADOW_COLOR
		support.depthTexture = true;
		support.shadowSampler = true;
		support.discardFrame = false;
		support.texNPOT = true;
		support.texRG = true;
		support.texBorder = true;
		support.colorFloat = true;
		support.colorHalf = true;
		support.texFloatLinear = true;
		support.texFloat = true;
		support.texHalfLinear = true;
		support.texHalf = true;
		support.tex3D = true;

		frameIndex = osSwapChain->GetCurrentBackBufferIndex();

		osDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator[0]));
		osDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator[1]));
		osDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator[0], nullptr, IID_PPV_ARGS(&commandList));
		commandList->Close();

		osDevice->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
		fenceValue++;

		D3D12_DESCRIPTOR_HEAP_DESC descHeap = {};
		descHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		descHeap.NumDescriptors = 2;
		descHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		osDevice->CreateDescriptorHeap(&descHeap, IID_PPV_ARGS(&rtvHeap));

		descHeap.NumDescriptors = 1;
		descHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		osDevice->CreateDescriptorHeap(&descHeap, IID_PPV_ARGS(&dsvHeap));

		D3D12_HEAP_PROPERTIES heap = {};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC desc = { };
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = Core::width;
		desc.Height = Core::height;
		desc.MipLevels = 1;
		desc.DepthOrArraySize = 1;
		desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		D3D12_CLEAR_VALUE clearValue = { };
		clearValue.Format = desc.Format;
		clearValue.DepthStencil.Depth = 1.f;
		osDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(&depthBuffer));

		osDevice->CreateDepthStencilView(depthBuffer, nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());
	}

	void deinit() {
		if (depthBuffer)
			depthBuffer->Release();
	}

	bool beginFrame() {
		UINT64 gpuWait = fenceValue - 2;
		if (fence->GetCompletedValue() < gpuWait)
		{
			HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			fence->SetEventOnCompletion(gpuWait, eventHandle);
			WaitForSingleObject(eventHandle, INFINITE);
			CloseHandle(eventHandle);
		}

		commandAllocator[frameIndex]->Reset();
		commandList->Reset(commandAllocator[frameIndex], nullptr);
		inFrame = true;

		return true;
	}

	void endFrame() {

		inFrame = false;
		commandList->Close();

		ID3D12CommandList* commandLists[] = { commandList };
		commandQueue->ExecuteCommandLists(1, commandLists);
	}

	void signalFrameComplete()
	{
		frameIndex = osSwapChain->GetCurrentBackBufferIndex();

		commandQueue->Signal(fence, fenceValue);
		fenceValue++;
	}

	void setVSync(bool enable) {}
	void waitVBlank() {}

	void resetState() {
	}

	void discardTarget(bool color, bool depth) {}

	void bindTarget(Texture* target, int face) {
	}

	void setClearColor(const vec4& color) {
	}

	void clear(bool color, bool depth) {
		if (depth)
		{
			commandList->ClearDepthStencilView(dsvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
		}
	}

	void copyTarget(Texture* dst, int xOffset, int yOffset, int x, int y, int width, int height) {
	}

	vec4 copyPixel(int x, int y) {
		return vec4(0, 0, 0, 0);
	}

	void setViewport(const short4& v) {
	}

	void setScissor(const short4& s) {
	}

	void setDepthTest(bool enable) {
	}

	void setDepthWrite(bool enable) {
	}

	void setColorWrite(bool r, bool g, bool b, bool a) {
	}

	void setCullMode(int rsMask) {
	}

	void setBlendMode(int rsMask) {
	}

	void setAlphaTest(bool enable) {
	}

	void setViewProj(const mat4& mView, const mat4& mProj) {}

	void DIP(Mesh* mesh, const MeshRange& range) {
		if (Core::active.shader) {
			Core::active.shader->validate();
		}
#if 0
		if (dirtyDepthState) {
			osContext->OMSetDepthStencilState(DS[depthTest][depthWrite], 0);
			dirtyDepthState = false;
		}

		if (dirtyBlendState) {
			osContext->OMSetBlendState(BS[colorWrite][blendMode], NULL, 0xFFFFFFFF);
			dirtyBlendState = false;
		}

		osContext->DrawIndexed(range.iCount, range.iStart, range.vStart);
#endif
	}

	void updateLights(vec4* lightPos, vec4* lightColor, int count) {
		if (active.shader) {
			active.shader->setParam(uLightColor, lightColor[0], count);
			active.shader->setParam(uLightPos, lightPos[0], count);
		}
	}

	inline mat4::ProjRange getProjRange() {
		return mat4::PROJ_ZERO_POS;
	}

	mat4 ortho(float l, float r, float b, float t, float znear, float zfar) {
		mat4 m;
		m.ortho(getProjRange(), l, r, b, t, znear, zfar);

#ifdef _OS_WP8
		m.rot90();
#endif

		return m;
	}

	mat4 perspective(float fov, float aspect, float znear, float zfar, float eye) {
		mat4 m;

#ifdef _OS_WP8
		aspect = 1.0f / aspect;
#endif

		m.perspective(getProjRange(), fov, aspect, znear, zfar, eye);

#ifdef _OS_WP8
		m.rot90();
#endif

		return m;
	}
}

#endif
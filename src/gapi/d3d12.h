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

	vec4 clearColor;

	ID3D12Resource* depthBuffer = nullptr;
	ID3D12Resource* backbuffers[2] = { nullptr, nullptr };

	ID3D12CommandAllocator* commandAllocator[2] = { nullptr, nullptr };
	ID3D12GraphicsCommandList* commandList = nullptr;
	ID3D12CommandQueue* commandQueue = nullptr;
	ID3D12Fence* fence = nullptr;
	ID3D12DescriptorHeap* rtvHeap = nullptr;
	ID3D12DescriptorHeap* dsvHeap = nullptr;
	ID3D12RootSignature* rootSignature = nullptr;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline = {};
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle[2] = {};
	bool inFrame = false;
	UINT64 fenceValue = 2;
	UINT frameIndex = 0;

	// Shader
#include "shaders/d3d11/shaders.h"

	struct Texture {
		int       width, height, depth, origWidth, origHeight, origDepth;
		TexFormat fmt;
		uint32    opt;
		bool dirty = false;

		ID3D12Resource* resource = nullptr;
		ID3D12Resource* stagingResource = nullptr;
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_GENERIC_READ;

		const struct FormatDesc {
			int         bpp;
			DXGI_FORMAT format;
		} formats[FMT_MAX] = {
			{   8, DXGI_FORMAT_R8_UNORM       },
			{  32, DXGI_FORMAT_R8G8B8A8_UNORM },
			{  16, DXGI_FORMAT_B5G6R5_UNORM   },
			{  16, DXGI_FORMAT_B5G5R5A1_UNORM },
			{  64, DXGI_FORMAT_R32G32_FLOAT   },
			{  32, DXGI_FORMAT_R16G16_FLOAT   },
			{  16, DXGI_FORMAT_R16_TYPELESS   },
			{  16, DXGI_FORMAT_R16_TYPELESS   },
		};

		Texture(int width, int height, int depth, uint32 opt) : width(width), height(height), depth(depth), origWidth(width), origHeight(height), origDepth(depth), fmt(FMT_RGBA), opt(opt) {

		}

		void init(void* data) {
			bool mipmaps = (opt & OPT_MIPMAPS) != 0;
			bool isDepth = fmt == FMT_DEPTH || fmt == FMT_SHADOW;
			bool isCube = (opt & OPT_CUBEMAP) != 0;
			bool isVolume = (opt & OPT_VOLUME) != 0;
			bool isTarget = (opt & OPT_TARGET) != 0;
			bool isDynamic = (opt & OPT_DYNAMIC) != 0;

			D3D12_RESOURCE_DESC desc = {};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = mipmaps ? 0 : 1;
			desc.Format = formats[fmt].format;
			desc.Alignment = 0;
			desc.Flags = D3D12_RESOURCE_FLAG_NONE;
			desc.SampleDesc.Count = 1;
			desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

			if (isVolume)
			{
				desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
				desc.DepthOrArraySize = origDepth;
			}
			else if (isCube)
				desc.DepthOrArraySize = 6;
			else
				desc.DepthOrArraySize = 1;

			if (isTarget && isDepth)
				desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
			else if (isTarget)
				desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

			D3D12_HEAP_PROPERTIES heap = {};
			heap.Type = D3D12_HEAP_TYPE_DEFAULT;
			osDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource));

			if (isDynamic)
			{
				UINT64 size = 0;
				osDevice->GetCopyableFootprints(&desc, 0, 1, 0, nullptr, nullptr, nullptr, &size);

				D3D12_RESOURCE_DESC stagingDesc = { };
				stagingDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
				stagingDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
				stagingDesc.Width = size;
				stagingDesc.Height = 1;
				stagingDesc.DepthOrArraySize = 1;
				stagingDesc.MipLevels = 1;
				stagingDesc.SampleDesc.Count = 1;
				
				heap.Type = D3D12_HEAP_TYPE_UPLOAD;
				osDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &stagingDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&stagingResource));
			}
		}

		void deinit() {
			if (resource)
				resource->Release();
			resource = nullptr;
		}

		void generateMipMap() {
		}

		void update(void* data) {
			ASSERT(stagingResource != nullptr);

			D3D12_RANGE range = { 0, 0 };
			void* gpuData = nullptr;
			stagingResource->Map(0, &range, &gpuData);
			memcpy(gpuData, data, width * height * (formats[fmt].bpp / 8));
			range.End = width * height * (formats[fmt].bpp / 8);
			stagingResource->Unmap(0, &range);
			dirty = true;
		}

		void bind(int sampler) {
			if (Core::active.textures[sampler] != this) {
				Core::active.textures[sampler] = this;
			}

			if (dirty)
			{
				D3D12_TEXTURE_COPY_LOCATION dst = { };
				dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				dst.SubresourceIndex = 0;
				dst.pResource = resource;

				D3D12_TEXTURE_COPY_LOCATION src = { };
				src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				src.pResource = stagingResource;
				src.PlacedFootprint.Offset = 0;
				osDevice->GetCopyableFootprints(&resource->GetDesc(), 0, 1, 0, &src.PlacedFootprint, nullptr, nullptr, nullptr);

				D3D12_RESOURCE_BARRIER barrier = { };
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				barrier.Transition.pResource = resource;
				barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
				barrier.Transition.StateBefore = state;
				barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

				commandList->ResourceBarrier(1, &barrier);
				commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
				
				barrier.Transition.StateAfter = state;
				barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
				commandList->ResourceBarrier(1, &barrier);

				dirty = false;
			}
		}

		void unbind(int sampler) {
			if (Core::active.textures[sampler]) {
				Core::active.textures[sampler] = NULL;
			}
		}

		void setFilterQuality(int value) {}
	};

	static const struct Binding {
		int reg;
	} bindings[uMAX] = {
		{   0 }, // uParam
		{   1 }, // uTexParam
		{   2 }, // uViewProj
		{   6 }, // uBasis
		{  70 }, // uLightProj
		{  74 }, // uMaterial
		{  75 }, // uAmbient
		{  81 }, // uFogParams
		{  82 }, // uViewPos
		{  83 }, // uLightPos
		{  87 }, // uLightColor
		{  91 }, // uRoomSize
		{  92 }, // uPosScale
		{  98 }, // uContacts
	};

	struct Shader {

		D3D12_SHADER_BYTECODE vs = { };
		D3D12_SHADER_BYTECODE ps = { };

		vec4* cbMem = nullptr;
		ID3D12Resource* constantBuffer = nullptr;

		void init(Core::Pass pass, int type, int* def, int defCount) {

			bool underwater = false;
			bool alphatest = false;

			for (int i = 0; i < defCount; i++) {
				switch (def[i]) {
				case SD_UNDERWATER: underwater = true; break;
				case SD_ALPHA_TEST: alphatest = true; break;
				}
			}

#define SHADER(S,P)    (P##Src = S##_##P, P##Size = sizeof(S##_##P))
#define SHADER_A(S,P)  (alphatest  ? SHADER(S##_a,P) : SHADER(S,P))
#define SHADER_U(S,P)  (underwater ? SHADER(S##_u,P) : SHADER(S,P))
#define SHADER_AU(S,P) ((underwater && alphatest) ? SHADER(S##_au,P) : (alphatest ? SHADER(S##_a,P) : SHADER_U(S,P)))

			int vSize = 0, fSize = 0;
			const uint8 *vSrc = nullptr, *fSrc = nullptr;
			switch (pass) {
			case passCompose:
				switch (type) {
				case 0: SHADER_U(compose_sprite, v);  SHADER_AU(compose_sprite, f); break;
				case 1: SHADER(compose_flash, v);  SHADER(compose_flash, f); break;
				case 2: SHADER_U(compose_room, v);  SHADER_AU(compose_room, f); break;
				case 3: SHADER_U(compose_entity, v);  SHADER_AU(compose_entity, f); break;
				case 4: SHADER(compose_mirror, v);  SHADER(compose_mirror, f); break;
				default: ASSERT(false);
				}
				break;
			case passShadow:
				switch (type) {
				case 3:
				case 4: SHADER(shadow_entity, v);  SHADER(shadow_entity, f); break;
				default: ASSERT(false);
				}
				break;
			case passAmbient:
				switch (type) {
				case 0: SHADER(ambient_sprite, v);  SHADER_A(ambient_sprite, f); break;
				case 1: SHADER(ambient_room, v);  SHADER(ambient_room, f); break; // TYPE_FLASH (sky)
				case 2: SHADER(ambient_room, v);  SHADER_A(ambient_room, f); break;
				default: ASSERT(false);
				}
				break;
			case passSky:
				switch (type) {
				case 0: SHADER(sky, v);     SHADER(sky, f);    break;
				case 1: SHADER(sky_clouds, v);  SHADER(sky_clouds, f); break;
				case 2: SHADER(sky_azure, v);   SHADER(sky_azure, f);  break;
				default: ASSERT(false);
				}
				break;
			case passWater:
				switch (type) {
				case 0: SHADER(water_drop, v);  SHADER(water_drop, f); break;
				case 1: SHADER(water_simulate, v);  SHADER(water_simulate, f); break;
				case 2: SHADER(water_caustics, v);  SHADER(water_caustics, f); break;
				case 3: SHADER(water_rays, v);  SHADER(water_rays, f); break;
				case 4: SHADER(water_mask, v);  SHADER(water_mask, f); break;
				case 5: SHADER(water_compose, v);  SHADER(water_compose, f); break;
				default: ASSERT(false);
				}
				break;
			case passFilter:
				switch (type) {
				case 0: SHADER(filter_upscale, v);  SHADER(filter_upscale, f); break;
				case 1: SHADER(filter_downsample, v);  SHADER(filter_downsample, f); break;
				case 3: SHADER(filter_grayscale, v);  SHADER(filter_grayscale, f); break;
				case 4: SHADER(filter_blur, v);  SHADER(filter_blur, f); break;
				case 5: SHADER(filter_anaglyph, v);  SHADER(filter_anaglyph, f); break;
				default: ASSERT(false);
				}
				break;
			case passGUI: SHADER(gui, v);  SHADER(gui, f); break;
			default: ASSERT(false); LOG("! wrong pass id\n"); return;
			}

#undef SHADER_A
#undef SHADER_U
#undef SHADER_AU

			vs.pShaderBytecode = vSrc;
			vs.BytecodeLength = vSize;
			ps.pShaderBytecode = vSrc;
			ps.BytecodeLength = vSize;

			D3D12_HEAP_PROPERTIES heap = {};
			heap.Type = D3D12_HEAP_TYPE_UPLOAD;

			D3D12_RESOURCE_DESC desc = { };
			desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width = sizeof(vec4) * (98 + MAX_CONTACTS);
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			desc.Height = 1;
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.SampleDesc.Count = 1;
			osDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&constantBuffer));
			constantBuffer->Map(0, nullptr, (void**)&cbMem);
		}

		void deinit() {
			if (constantBuffer)
			{
				constantBuffer->Unmap(0, nullptr);
				constantBuffer->Release();
				constantBuffer = nullptr;
				cbMem = nullptr;
			}
		}

		void bind() {
			if (Core::active.shader != this) {
				Core::active.shader = this;
				
			}
		}

		void validate() {
		}

		void setParam(UniformType uType, float* value, int count) {
			memcpy(cbMem + bindings[uType].reg, value, count * sizeof(vec4));
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

		osSwapChain->GetBuffer(0, IID_PPV_ARGS(&backbuffers[0]));
		osSwapChain->GetBuffer(1, IID_PPV_ARGS(&backbuffers[1]));
		rtvHandle[0] = rtvHeap->GetCPUDescriptorHandleForHeapStart();
		rtvHandle[1] = { rtvHandle[0].ptr + osDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) };

		osDevice->CreateRenderTargetView(backbuffers[0], nullptr, rtvHandle[0]);
		osDevice->CreateRenderTargetView(backbuffers[1], nullptr, rtvHandle[1]);

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

		{
			D3D12_ROOT_SIGNATURE_DESC rsDesc = { };
			D3D12_ROOT_PARAMETER params[2] = {};
			params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
			params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			params[0].Descriptor.RegisterSpace = 0;
			params[0].Descriptor.ShaderRegister = 0;
			
			rsDesc.NumParameters = _countof(params);
			rsDesc.pParameters = params;
		}
	}

	void deinit() {
		if (depthBuffer)
			depthBuffer->Release();
		depthBuffer = nullptr;
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

		D3D12_RESOURCE_BARRIER barrier = { };
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = backbuffers[frameIndex];
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1, &barrier);

		commandList->OMSetRenderTargets(1, rtvHandle + frameIndex, TRUE, &(dsvHeap->GetCPUDescriptorHandleForHeapStart()));

		return true;
	}

	void endFrame() {

		D3D12_RESOURCE_BARRIER barrier = { };
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = backbuffers[frameIndex];
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1, &barrier);

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
		clearColor = color;
	}

	void clear(bool color, bool depth) {
		if (color)
		{
			commandList->ClearRenderTargetView(rtvHandle[frameIndex], (FLOAT*)&clearColor, 0, nullptr);
		}
		
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
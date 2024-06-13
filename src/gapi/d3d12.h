#ifndef H_GAPI_D3D12
#define H_GAPI_D3D12

#include "core.h"
#include <d3d12.h>
#include <dxgi1_6.h>

#define PROFILE_MARKER(title)
#define PROFILE_LABEL(id, child, label)
#define PROFILE_TIMING(time)

namespace GAPI {
	using namespace Core;

	typedef ::Vertex Vertex;

	ID3D12CommandQueue* commandQueue = nullptr;
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
	}

	void deinit() {
	}

	bool beginFrame() {
		return true;
	}

	void endFrame() {
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
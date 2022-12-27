#pragma once

#include "D3DClass.h"
#include "MathHelper.h"
#include "UploadBuffer.h"
#include "FrameResource.h"
#include "GeometryGenerator.h"
#include "Camera.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXColors.h>
#include <exception>

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

namespace Graphics
{
	class ENGINE_API GraphicsClass : public D3DClass
	{
	public:
		GraphicsClass();
		GraphicsClass(const GraphicsClass& rhs) = delete;
		GraphicsClass& operator=(const GraphicsClass& rhs) = delete;
		~GraphicsClass();

		virtual void Initialize(HWND mainWnd, int width, int height) override;

	protected:
		virtual void OnResize() override;
		virtual void Update(const Timer& gameTimer) override;
		virtual void Draw(const Timer& gameTimer) override;

		void OnRightMouseDown(WPARAM buttonState, int x, int y) override;
		void OnRightMouseUp(WPARAM buttonState, int x, int y) override;
		void OnMouseMove(WPARAM buttonState, int x, int y, int z) override;
		void OnMouseWheel(WPARAM buttonState, int x, int y, int z) override;

		void OnKeyboardInput(const Timer& gameTimer);
		void FrustumCulling(const Timer& gameTimer);
		void UpdateReflections(const Timer& gameTimer);
		void UpdateShadows(const Timer& gameTimer);
		void UpdateObjectConstantBuffers(const Timer& gameTimer);
		void UpdateMaterialConstantBuffers(const Timer& gameTimer);
		void UpdateMainPassConstantBuffer(const Timer& gameTimer);
		void UpdateReflectedPassConstantBuffer(const Timer& gameTimer);

		void LoadTextures();
		void BuildDescriptorHeaps();
		void BuildRootSignature();
		void BuildShadersAndInputLayout();
		void BuildRoomGeometry();
		void BuildShapeGeometry();
		void BuildCarGeometry();
		void BuildPipelineStateObjects();
		void BuildFrameResources();
		void BuildMaterials();
		void BuildRenderItems();
		void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
		void UpdateImGuiData();
		void UpdateLights();
		void UpdateSceneData();
		void AddShape();
		void AddMaterial();

		void Pick(int sx, int sy);
		void MoveRenderItem(int sx, int sy, int sz);

		std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	protected:
		Camera m_Camera;

		std::vector<std::unique_ptr<FrameResource>> m_FrameResources;
		FrameResource* m_CurrentFrameResource = nullptr;
		int m_CurrentFrameResourceIndex = 0;

		UINT m_CbvSrvDescriptorSize = 0;

		ComPtr<ID3D12RootSignature> m_RootSignature = nullptr;

		ComPtr<ID3D12DescriptorHeap> m_ShaderResourceViewDescriptorHeap = nullptr;

		std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> m_Geometries;
		std::unordered_map<std::string, std::unique_ptr<Material>> m_Materials;
		std::unordered_map<std::string, std::unique_ptr<Texture>> m_Textures;
		std::unordered_map<std::string, ComPtr<ID3DBlob>> m_Shaders;
		std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> m_PipelineStateObjects;

		std::vector<D3D12_INPUT_ELEMENT_DESC> m_InputLayout;

		// List of all the render items.
		std::vector<std::unique_ptr<RenderItem>> m_AllRenderItems;
		// Render items divided by PSO.
		std::vector<RenderItem*> m_RenderItemLayer[(int)RenderLayer::Count];

		RenderItem* m_PickedRenderItem = nullptr;

		bool m_FrustumCullingIsEnabled = true;
		BoundingFrustum m_CameraFrustum;

		PassConstants m_MainPassConstantBuffer;
		PassConstants m_ReflectedPassCB;

		POINT m_LastMousePos = { 0, 0 };

		u_int m_ObjConstantBufferIndex = 0;
		u_int m_MatConstantBufferIndex = 0;
		u_int m_AddedShapesCount = 0;
	};
}



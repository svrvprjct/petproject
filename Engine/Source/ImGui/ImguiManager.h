#pragma once

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

#include "Graphics/D3DUtils.h"

#include <DirectXMath.h>

class ENGINE_API ImguiManager
{
public:
	ImguiManager();
	~ImguiManager();

	void Initialize(ID3D12Device* d3dDevice, DXGI_FORMAT backBufferFormat, ID3D12DescriptorHeap* srvHeap);
	void NewFrame();
	void DrawRenderData(ID3D12GraphicsCommandList* commandList);
	void ShowSetItemsWindow();
	void ShowSetCameraWindow();
	void ShowSetLightningWindow();
	void ShowSetSceneWindow();
	void ShowGeometryShapesHeader();
	void ShowMaterialsHeader();
	void ShowModelsHeader();
	void Render();
	void UpdateItems(
		bool isPicked, 
		std::string itemName = "\0",
		std::string itemMaterial = "\0",
		DirectX::XMFLOAT3 scaling = { 1.0f, 1.0f, 1.0f },
		DirectX::XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f },
		DirectX::XMFLOAT3 translation = { 0.0f, 0.0f, 0.0f });
	void UpdateLights();

	bool AddShapeFlag();
	void AddShapeFlag(bool flag);
	AddShapeData GetAddShape();
	bool CreateMaterialFlag();
	void CreateMaterialFlag(bool flag);
	AddMaterialData GetAddMaterial();
	DirectX::XMFLOAT3 CameraPosition();
	void CameraPosition(DirectX::XMFLOAT3 cameraPosition);
	DirectX::XMFLOAT4 GetAmbientLight();
	Light& GetLights(int element);
	DirectX::XMFLOAT3 GetItemScaling();
	DirectX::XMFLOAT3 GetItemRotation();
	DirectX::XMFLOAT3 GetItemTranslation();
	std::string GetItemMaterial();
	std::unordered_map<std::string, bool> GetGeometryShapes();
	void SetGeometryShapes(std::string name, bool isVisible);
	std::string GetShapeEraseName();
	void SetShapeEraseName(std::string name);
	void EraseShape(std::string name);
	int GetMaterialsCount();
	void SetMaterialsCount(int count);
	std::vector<std::string> GetMaterialsName();
	void SetMaterialsName(std::string name);
	std::vector<DirectX::XMFLOAT4> GetMaterialsAlbedo();
	void SetMaterialsAlbedo(DirectX::XMFLOAT4 albedo);
	std::vector<DirectX::XMFLOAT3> GetMaterialsFresnelR0();
	void SetMaterialsFresnelR0(DirectX::XMFLOAT3 fresnelR0);
	std::vector<float> GetMaterialsRoughness();
	void SetMaterialsRoughness(float roughness);
	std::unordered_map<std::string, bool> GetModels();
	void SetModels(std::string name, bool isVisible);

	const int maxDirLightsCount = 1;
	const int maxPointLightsCount = 10;
	const int maxSpotLightsCount = 10;

private:
	bool ImGuiImplDX12Initialized = false;

	bool m_ItemIsPicked = false;
	DirectX::XMFLOAT3 m_ItemScaling = { 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 m_ItemRotation = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 m_ItemTranslation = { 0.0f, 0.0f, 0.0f };
	std::string m_ItemName;
	std::string m_ItemMaterial;

	DirectX::XMFLOAT3 m_CameraPosition = { 0.0f, 8.0f, -40.0f };

	std::unordered_map<std::string, bool> m_GeometryShapes;
	std::string m_GeometryShapeEraseName;

	int m_MaterialsCount = 0;
	std::vector<std::string> m_MaterialsName;
	std::vector<DirectX::XMFLOAT4> m_MaterialsAlbedo;
	std::vector<DirectX::XMFLOAT3> m_MaterialsFresnelR0;
	std::vector<float> m_MaterialsRoughness;

	std::unordered_map<std::string, bool> m_Models;

	AddShapeData m_AddShape;
	bool m_GeometryAddShapeFlag = false;

	AddMaterialData m_AddMaterial;
	bool m_MaterialCreateFlag = false;
	bool m_NameAlreadyExists = false;

	DirectX::XMFLOAT4 m_AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };

	Light m_Lights[MaxLights];

	DirectX::XMFLOAT3 m_DirectLightDirection = { DirectX::XMFLOAT3(0.57735f, -0.57735f, 0.57735f) };
	DirectX::XMFLOAT3 m_DirectLightStrength = { DirectX::XMFLOAT3(0.6f, 0.6f, 0.6f) };
	bool m_DirectLightIsEnable = true;

	int m_PointLightsCount = 1;
	DirectX::XMFLOAT3 m_PointLightsStrength[10] = { DirectX::XMFLOAT3(0.6f, 0.6f, 0.6f) };
	float m_PointLightsFalloffStart[10] = { 1.0f };
	float m_PointLightsFalloffEnd[10] = { 10.0f };
	DirectX::XMFLOAT3 m_PointLightsPosition[10] = { DirectX::XMFLOAT3(-4.0f, 5.0f, -17.0f) };
	bool m_PointLightsIsEnable[10] = { true };

	int m_SpotLightsCount = 1;
	DirectX::XMFLOAT3 m_SpotLightsDirection[10] = { DirectX::XMFLOAT3(0.97735f, -0.97735f, 0.57735f) };
	DirectX::XMFLOAT3 m_SpotLightsStrength[10] = { DirectX::XMFLOAT3(0.6f, 0.6f, 0.6f) };
	float m_SpotLightsFalloffStart[10] = { 1.0f };
	float m_SpotLightsFalloffEnd[10] = { 15.0f };
	DirectX::XMFLOAT3 m_SpotLightsPosition[10] = { DirectX::XMFLOAT3(4.0f, 8.0f, -22.0f) };
	float m_SpotLightsSpotPower[10] = { 5.0f };
	bool m_SpotLightsIsEnable[10] = { true };
};
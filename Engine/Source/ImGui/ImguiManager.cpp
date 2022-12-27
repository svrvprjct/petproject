#include "Engine.h"
#include "ImguiManager.h"

ImguiManager::ImguiManager()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
}

ImguiManager::~ImguiManager()
{
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void ImguiManager::Initialize(ID3D12Device* d3dDevice, DXGI_FORMAT backBufferFormat, ID3D12DescriptorHeap* srvHeap)
{
	if (!ImGuiImplDX12Initialized)
	{
		ImGuiImplDX12Initialized = ImGui_ImplDX12_Init(d3dDevice, 3,
			backBufferFormat, srvHeap,
			srvHeap->GetCPUDescriptorHandleForHeapStart(),
			srvHeap->GetGPUDescriptorHandleForHeapStart());
	}
}

void ImguiManager::NewFrame()
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void ImguiManager::DrawRenderData(ID3D12GraphicsCommandList* commandList)
{
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

void ImguiManager::Render()
{
	ImGui::Render();
}

void ImguiManager::UpdateItems(
	bool isPicked,
	std::string itemName,
	std::string itemMaterial,
	DirectX::XMFLOAT3 scaling,
	DirectX::XMFLOAT3 rotation,
	DirectX::XMFLOAT3 translation)
{
	if (isPicked)
	{
		m_ItemIsPicked = isPicked;
		m_ItemScaling = scaling;
		m_ItemRotation.x = XMConvertToDegrees(rotation.x);
		m_ItemRotation.y = XMConvertToDegrees(rotation.y);
		m_ItemRotation.z = XMConvertToDegrees(rotation.z);
		m_ItemTranslation = translation;
		m_ItemName = itemName;
		m_ItemMaterial = itemMaterial;
	}
	else
	{
		m_ItemIsPicked = false;
	}
}

void ImguiManager::UpdateLights()
{
	// Direct Lights
	if (m_DirectLightIsEnable)
	{
		m_Lights[0].Direction = m_DirectLightDirection;
		m_Lights[0].Strength = m_DirectLightStrength;
	}
	else
	{
		m_Lights[0].Direction = { 0.0f, 0.0f, 0.0f };
		m_Lights[0].Strength = { 0.0f, 0.0f, 0.0f };
	}

	// Point Lights
	for (int i = maxDirLightsCount; i < maxDirLightsCount + maxPointLightsCount; i++)
	{
		if (m_PointLightsIsEnable[i - maxDirLightsCount])
		{
			m_Lights[i].Strength = m_PointLightsStrength[i - maxDirLightsCount];
			m_Lights[i].FalloffStart = m_PointLightsFalloffStart[i - maxDirLightsCount];
			m_Lights[i].FalloffEnd = m_PointLightsFalloffEnd[i - maxDirLightsCount];
			m_Lights[i].Position = m_PointLightsPosition[i - maxDirLightsCount];
		}
		else
		{
			m_Lights[i].Strength = { 0.0f, 0.0f, 0.0f };
			m_Lights[i].FalloffStart = 0.0f;
			m_Lights[i].FalloffEnd = 0.0f;
			m_Lights[i].Position = { 0.0f, 0.0f, 0.0f };
		}
	}

	// Spot Lights
	for (int n = maxDirLightsCount + maxPointLightsCount, i = n; i < MaxLights; i++)
	{
		if (m_SpotLightsIsEnable[i - n])
		{
			m_Lights[i].Direction = m_SpotLightsDirection[i - n];
			m_Lights[i].Strength = m_SpotLightsStrength[i - n];
			m_Lights[i].FalloffStart = m_SpotLightsFalloffStart[i - n];
			m_Lights[i].FalloffEnd = m_SpotLightsFalloffEnd[i - n];
			m_Lights[i].Position = m_SpotLightsPosition[i - n];
			m_Lights[i].SpotPower = m_SpotLightsSpotPower[i - n];
		}
		else
		{
			m_Lights[i].Direction = { 0.0f, 0.0f, 0.0f };
			m_Lights[i].Strength = { 0.0f, 0.0f, 0.0f };
			m_Lights[i].FalloffStart = 0.0f;
			m_Lights[i].FalloffEnd = 0.0f;
			m_Lights[i].Position = { 0.0f, 0.0f, 0.0f };
			m_Lights[i].SpotPower = 0.0f;
		}
	}
}

bool ImguiManager::AddShapeFlag()
{
	return m_GeometryAddShapeFlag;
}

void ImguiManager::AddShapeFlag(bool flag)
{
	m_GeometryAddShapeFlag = flag;
}

bool ImguiManager::CreateMaterialFlag()
{
	return m_MaterialCreateFlag;
}

void ImguiManager::CreateMaterialFlag(bool flag)
{
	m_MaterialCreateFlag = flag;
}

AddMaterialData ImguiManager::GetAddMaterial()
{
	return m_AddMaterial;
}

DirectX::XMFLOAT3 ImguiManager::CameraPosition()
{
	return m_CameraPosition;
}

void ImguiManager::CameraPosition(DirectX::XMFLOAT3 cameraPosition)
{
	m_CameraPosition = cameraPosition;
}

DirectX::XMFLOAT4 ImguiManager::GetAmbientLight()
{
	return m_AmbientLight;
}

Light& ImguiManager::GetLights(int element)
{
	return m_Lights[element];
}

DirectX::XMFLOAT3 ImguiManager::GetItemScaling()
{
	return m_ItemScaling;
}

DirectX::XMFLOAT3 ImguiManager::GetItemRotation()
{
	return m_ItemRotation;
}

DirectX::XMFLOAT3 ImguiManager::GetItemTranslation()
{
	return m_ItemTranslation;
}

std::string ImguiManager::GetItemMaterial()
{
	return m_ItemMaterial;
}

std::unordered_map<std::string, bool> ImguiManager::GetGeometryShapes()
{
	return m_GeometryShapes;
}

void ImguiManager::SetGeometryShapes(std::string name, bool isVisible)
{
	if (m_GeometryShapes.contains(name))
		m_GeometryShapes[name] = isVisible;
	else
		m_GeometryShapes[name] = std::move(isVisible);
}

std::string ImguiManager::GetShapeEraseName()
{
	return m_GeometryShapeEraseName;
}

void ImguiManager::SetShapeEraseName(std::string name)
{
	m_GeometryShapeEraseName = name;
}

void ImguiManager::EraseShape(std::string name)
{
	m_GeometryShapes.erase(name);
}

int ImguiManager::GetMaterialsCount()
{
	return m_MaterialsCount;
}

void ImguiManager::SetMaterialsCount(int count)
{
	m_MaterialsCount = count;
}

std::vector<std::string> ImguiManager::GetMaterialsName()
{
	return m_MaterialsName;
}

void ImguiManager::SetMaterialsName(std::string name)
{
	m_MaterialsName.push_back(name);
}

std::vector<DirectX::XMFLOAT4> ImguiManager::GetMaterialsAlbedo()
{
	return m_MaterialsAlbedo;
}

void ImguiManager::SetMaterialsAlbedo(DirectX::XMFLOAT4 albedo)
{
	m_MaterialsAlbedo.push_back(albedo);
}

std::vector<DirectX::XMFLOAT3> ImguiManager::GetMaterialsFresnelR0()
{
	return m_MaterialsFresnelR0;
}

void ImguiManager::SetMaterialsFresnelR0(DirectX::XMFLOAT3 fresnelR0)
{
	m_MaterialsFresnelR0.push_back(fresnelR0);
}

std::vector<float> ImguiManager::GetMaterialsRoughness()
{
	return m_MaterialsRoughness;
}

void ImguiManager::SetMaterialsRoughness(float roughness)
{
	m_MaterialsRoughness.push_back(roughness);
}

std::unordered_map<std::string, bool> ImguiManager::GetModels()
{
	return m_Models;
}

void ImguiManager::SetModels(std::string name, bool isVisible)
{
	if (m_Models.contains(name))
		m_Models[name] = isVisible;
	else
		m_Models[name] = std::move(isVisible);
}

AddShapeData ImguiManager::GetAddShape()
{
	return m_AddShape;
}

void ImguiManager::ShowSetItemsWindow()
{
	ImGui::Begin("Render Item");                      

	if (m_ItemIsPicked)
	{
		ImGui::Text("Item Name:");
		ImGui::Text(m_ItemName.c_str());

		ImGui::Text("Position (Use Mouse Move or Mouse Scroll)");
		ImGui::InputFloat("x", &m_ItemTranslation.x, 1.0f, 1.0f);
		ImGui::InputFloat("y", &m_ItemTranslation.y, 1.0f, 1.0f);
		ImGui::InputFloat("z", &m_ItemTranslation.z, 1.0f, 1.0f);

		ImGui::Text("Rotation");
		ImGui::SliderFloat("x axis", &m_ItemRotation.x, -180.0f, 180.0f, "%.1f degree");
		ImGui::SliderFloat("y axis", &m_ItemRotation.y, -180.0f, 180.0f, "%.1f degree");
		ImGui::SliderFloat("z axis", &m_ItemRotation.z, -180.0f, 180.0f, "%.1f degree");

		ImGui::Text("Scaling");
		ImGui::InputFloat("Scale x", &m_ItemScaling.x, 1.0f, 1.0f);
		if (m_ItemScaling.x < 1)
			m_ItemScaling.x = 1;
		ImGui::InputFloat("Scale y", &m_ItemScaling.y, 1.0f, 1.0f);
		if (m_ItemScaling.y < 1)
			m_ItemScaling.y = 1;
		ImGui::InputFloat("Scale z", &m_ItemScaling.z, 1.0f, 1.0f);
		if (m_ItemScaling.z < 1)
			m_ItemScaling.z = 1;

		ImGui::Text("Material");
		if (ImGui::Button("Select.."))
			ImGui::OpenPopup("my_select_popup");
		ImGui::SameLine();
		ImGui::TextUnformatted(m_ItemMaterial.c_str());
		if (ImGui::BeginPopup("my_select_popup"))
		{
			for (auto e : m_MaterialsName)
				if (ImGui::Selectable(e.c_str()))
					m_ItemMaterial = e.c_str();
			ImGui::EndPopup();
		}
	}
	else
	{
		ImGui::Text("No picked items");
		ImGui::Text("(Aim on item and click Right Mouse)");
	}
	ImGui::End();
}

void ImguiManager::ShowSetCameraWindow()
{
	ImGui::Begin("Set camera");

	ImGui::Text("Position (Use WASD or Mouse Scroll)");
	ImGui::InputFloat("x", &m_CameraPosition.x, 1.0f, 1.0f);
	ImGui::InputFloat("y", &m_CameraPosition.y, 1.0f, 1.0f);
	ImGui::InputFloat("z", &m_CameraPosition.z, 1.0f, 1.0f);
	ImGui::Text("Rotation (Use Left Ctrl + Mouse Move)");

	ImGui::End();
}

void ImguiManager::ShowSetLightningWindow()
{
	ImGui::Begin("Set lightning");

	ImGui::Text("Ambient Light");
	ImGui::ColorEdit3("Color", (float*)&m_AmbientLight);

	ImGui::Separator();

	if (ImGui::CollapsingHeader("Direction Light"))
	{
		std::string name = "Dir light source ";
		std::string num = std::to_string(1);
		name += num;
		std::string enable = name + " enable";

		if (ImGui::TreeNode(name.c_str()))
		{
			ImGui::Text("Dir Light Direction");
			ImGui::SliderFloat("x axis", &m_DirectLightDirection.x, -0.999f, 0.999f);
			ImGui::SliderFloat("y axis", &m_DirectLightDirection.y, -0.999f, 0.999f);
			ImGui::SliderFloat("z axis", &m_DirectLightDirection.z, -0.999f, 0.999f);

			ImGui::Text("Dir Light Strength");
			ImGui::SliderFloat("r", &m_DirectLightStrength.x, 0.0f, 1.0f);
			ImGui::SliderFloat("g", &m_DirectLightStrength.y, 0.0f, 1.0f);
			ImGui::SliderFloat("b", &m_DirectLightStrength.z, 0.0f, 1.0f);

			ImGui::Checkbox(enable.c_str(), &m_DirectLightIsEnable);

			ImGui::TreePop();
		}
	}

	ImGui::Separator();

	if (ImGui::CollapsingHeader("Point Light"))
	{
		for (int i = 0; i < m_PointLightsCount; i++)
		{
			std::string name = "Point light source ";
			std::string num = std::to_string(i + 1);
			name += num;
			std::string enable = name + " enable";

			if (ImGui::TreeNode(name.c_str()))
			{
				ImGui::Text("Point Strength");
				ImGui::SliderFloat("Point Strength r", &m_PointLightsStrength[i].x, 0.0f, 1.0f);
				ImGui::SliderFloat("Point Strength g", &m_PointLightsStrength[i].y, 0.0f, 1.0f);
				ImGui::SliderFloat("Point Strength b", &m_PointLightsStrength[i].z, 0.0f, 1.0f);

				ImGui::Text("Point Fall");
				ImGui::InputFloat("Point Fall of Start", &m_PointLightsFalloffStart[i], 1.0f, 1.0f);
				ImGui::InputFloat("Point Fall of End", &m_PointLightsFalloffEnd[i], 1.0f, 1.0f);

				ImGui::Text("Point Position");
				ImGui::InputFloat("Point Pos x", &m_PointLightsPosition[i].x, 1.0f, 1.0f);
				ImGui::InputFloat("Point Pos y", &m_PointLightsPosition[i].y, 1.0f, 1.0f);
				ImGui::InputFloat("Point Pos z", &m_PointLightsPosition[i].z, 1.0f, 1.0f);

				ImGui::Checkbox(enable.c_str(), &m_PointLightsIsEnable[i]);

				ImGui::TreePop();
			}
		}

		if (ImGui::Button("Create Point Light Source"))
		{
			if (m_PointLightsCount < 10)
			{
				m_PointLightsStrength[m_PointLightsCount] = DirectX::XMFLOAT3(0.6f, 0.6f, 0.6f);
				m_PointLightsFalloffStart[m_PointLightsCount] = 1.0f;
				m_PointLightsFalloffEnd[m_PointLightsCount] = 10.0f;
				m_PointLightsPosition[m_PointLightsCount] = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
				m_PointLightsIsEnable[m_PointLightsCount] = true;
				++m_PointLightsCount;
			}
		}

		ImGui::SameLine();

		if (ImGui::Button("Delete Point Light Source"))
		{
			if (m_PointLightsCount > 0)
			{
				m_PointLightsIsEnable[m_PointLightsCount - 1] = false;
				--m_PointLightsCount;
			}
		}
	}

	ImGui::Separator();

	if (ImGui::CollapsingHeader("Spot Light"))
	{
		for (int i = 0; i < m_SpotLightsCount; i++)
		{
			std::string name = "Spot light source ";
			std::string num = std::to_string(i + 1);
			name += num;
			std::string enable = name + " enable";

			if (ImGui::TreeNode(name.c_str()))
			{
				ImGui::Text("Spot Direction");
				ImGui::SliderFloat("Spot Dir x axis", &m_SpotLightsDirection[i].x, -0.999f, 0.999f);
				ImGui::SliderFloat("Spot Dir y axis", &m_SpotLightsDirection[i].y, -0.999f, 0.999f);
				ImGui::SliderFloat("Spot Dir z axis", &m_SpotLightsDirection[i].z, -0.999f, 0.999f);

				ImGui::Text("Spot Strength");
				ImGui::SliderFloat("Spot Strength r", &m_SpotLightsStrength[i].x, 0.0f, 1.0f);
				ImGui::SliderFloat("Spot Strength g", &m_SpotLightsStrength[i].y, 0.0f, 1.0f);
				ImGui::SliderFloat("Spot Strength b", &m_SpotLightsStrength[i].z, 0.0f, 1.0f);

				ImGui::Text("Spot Fall");
				ImGui::InputFloat("Spot Fall of Start", &m_SpotLightsFalloffStart[i], 1.0f, 1.0f);
				ImGui::InputFloat("Spot Fall of End", &m_SpotLightsFalloffEnd[i], 1.0f, 1.0f);

				ImGui::Text("Spot Position");
				ImGui::InputFloat("Spot pos x", &m_SpotLightsPosition[i].x, 1.0f, 1.0f);
				ImGui::InputFloat("Spot pos y", &m_SpotLightsPosition[i].y, 1.0f, 1.0f);
				ImGui::InputFloat("Spot pos z", &m_SpotLightsPosition[i].z, 1.0f, 1.0f);

				ImGui::Text("Spot power");
				ImGui::SliderFloat("Power", &m_SpotLightsSpotPower[i], 1.0f, 100.0f);

				ImGui::Checkbox(enable.c_str(), &m_SpotLightsIsEnable[i]);

				ImGui::TreePop();
			}
		}

		if (ImGui::Button("Create Spot Light Source"))
		{
			if (m_SpotLightsCount < 10)
			{
				m_SpotLightsDirection[m_SpotLightsCount] = DirectX::XMFLOAT3(0.57735f, -0.57735f, 0.57735f);
				m_SpotLightsStrength[m_SpotLightsCount] = DirectX::XMFLOAT3(0.6f, 0.6f, 0.6f);
				m_SpotLightsFalloffStart[m_SpotLightsCount] = 1.0f;
				m_SpotLightsFalloffEnd[m_SpotLightsCount] = 10.0f;
				m_SpotLightsPosition[m_SpotLightsCount] = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
				m_SpotLightsSpotPower[m_SpotLightsCount] = 64.0f;
				m_SpotLightsIsEnable[m_SpotLightsCount] = true;
				++m_SpotLightsCount;
			}
		}

		ImGui::SameLine();

		if (ImGui::Button("Delete Spot Light Source"))
		{
			if (m_SpotLightsCount > 0)
			{
				m_SpotLightsIsEnable[m_SpotLightsCount - 1] = false;
				--m_SpotLightsCount;
			}
		}
	}

	ImGui::End();
}

void ImguiManager::ShowSetSceneWindow()
{
	ImGui::Begin("Scene");

	ShowGeometryShapesHeader();
	ShowMaterialsHeader();
	ShowModelsHeader();

	ImGui::End();
}

void ImguiManager::ShowGeometryShapesHeader()
{
	if (ImGui::CollapsingHeader("Geometry Shapes"))
	{
		for (auto& e : m_GeometryShapes)
		{
			std::string name = e.first.c_str();
			std::string visible = name + " visible";

			if (ImGui::TreeNode(name.c_str()))
			{
				bool b = e.second;
				ImGui::Checkbox(visible.c_str(), &b);
				e.second = b;

				ImGui::SameLine();

				if (ImGui::Button("Delete Shape"))
					m_GeometryShapeEraseName = e.first;

				ImGui::TreePop();
			}
		}

		ImGui::Separator();

		if (ImGui::Button("Add Shape"))
			ImGui::OpenPopup("Add Shape popup");

		if (ImGui::BeginPopup("Add Shape popup"))
		{
			m_AddShape.Pos = m_CameraPosition;
			m_AddShape.Pos.y -= 2.0f;

			if (ImGui::BeginMenu("Box"))
			{
				ImGui::Text("Create a box with the given dimensions and given subdivisions.");
				ImGui::InputFloat("Box width", &m_AddShape.BoxWidth, 1.0f, 1.0f);
				ImGui::InputFloat("Box height", &m_AddShape.BoxHeight, 1.0f, 1.0f);
				ImGui::InputFloat("Box depth", &m_AddShape.BoxDepth, 1.0f, 1.0f);
				ImGui::InputInt("Num subdivisions", &m_AddShape.BoxNumSubdivisions, 1, 1);
				if (m_AddShape.BoxNumSubdivisions < 1)
					m_AddShape.BoxNumSubdivisions = 1;

				if (ImGui::Button("Select Box material"))
					ImGui::OpenPopup("my_select_box_material_popup");
				ImGui::SameLine();
				ImGui::TextUnformatted(m_AddShape.BoxMaterial.c_str());
				if (ImGui::BeginPopup("my_select_box_material_popup"))
				{
					for (auto e : m_MaterialsName)
						if (ImGui::Selectable(e.c_str()))
							m_AddShape.BoxMaterial = e.c_str();
					ImGui::EndPopup();
				}
				ImGui::InputFloat3("Box Position", (float*)&m_AddShape.Pos);

				if (ImGui::MenuItem("Add Box"))
				{
					m_AddShape.ShapeGeo = Geometry::Box;
					AddShapeFlag(true);
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Grid"))
			{
				ImGui::Text("Create grid with m rows and n columns, with width and depth.");
				ImGui::InputFloat("Grid width", &m_AddShape.GridWidth, 1.0f, 1.0f);
				ImGui::InputFloat("Grid depth", &m_AddShape.GridDepth, 1.0f, 1.0f);
				ImGui::InputInt("Grid M", &m_AddShape.GridM, 1, 1);
				if (m_AddShape.GridM < 1)
					m_AddShape.GridM = 1;
				ImGui::InputInt("Grid N", &m_AddShape.GridN, 1, 1);
				if (m_AddShape.GridN < 1)
					m_AddShape.GridN = 1;

				if (ImGui::Button("Select Grid material"))
					ImGui::OpenPopup("my_select_grid_material_popup");
				ImGui::SameLine();
				ImGui::TextUnformatted(m_AddShape.GridMaterial.c_str());
				if (ImGui::BeginPopup("my_select_grid_material_popup"))
				{
					for (auto e : m_MaterialsName)
						if (ImGui::Selectable(e.c_str()))
							m_AddShape.GridMaterial = e.c_str();
					ImGui::EndPopup();
				}
				ImGui::InputFloat3("Grid Position", (float*)&m_AddShape.Pos);

				if (ImGui::MenuItem("Add Grid"))
				{
					m_AddShape.ShapeGeo = Geometry::Grid;
					AddShapeFlag(true);
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Sphere"))
			{
				ImGui::Text("Creates a sphere with the given radius. The slices and stacks control the tessellation.");
				ImGui::InputFloat("Sphere radius", &m_AddShape.SphereRadius, 1.0f, 1.0f);
				ImGui::InputInt("Sphere slice count", &m_AddShape.SphereSliceCount, 1, 1);
				if (m_AddShape.SphereSliceCount < 1)
					m_AddShape.SphereSliceCount = 1;
				ImGui::InputInt("Sphere stack count", &m_AddShape.SphereStackCount, 1, 1);
				if (m_AddShape.SphereStackCount < 1)
					m_AddShape.SphereStackCount = 1;

				if (ImGui::Button("Select Sphere material"))
					ImGui::OpenPopup("my_select_sphere_material_popup");
				ImGui::SameLine();
				ImGui::TextUnformatted(m_AddShape.SphereMaterial.c_str());
				if (ImGui::BeginPopup("my_select_sphere_material_popup"))
				{
					for (auto e : m_MaterialsName)
						if (ImGui::Selectable(e.c_str()))
							m_AddShape.SphereMaterial = e.c_str();
					ImGui::EndPopup();
				}
				ImGui::InputFloat3("Sphere Position", (float*)&m_AddShape.Pos);

				if (ImGui::MenuItem("Add Sphere"))
				{
					m_AddShape.ShapeGeo = Geometry::Sphere;
					AddShapeFlag(true);
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Geosphere"))
			{
				ImGui::Text("Creates a geosphere with the given radius. The depth controls the tessellation.");
				ImGui::InputFloat("Geosphere radius", &m_AddShape.GeosphereRadius, 1.0f, 1.0f);
				ImGui::InputInt("Geosphere num subdivisions", &m_AddShape.GeosphereNumSubdivisions, 1, 1);
				if (m_AddShape.GeosphereNumSubdivisions < 1)
					m_AddShape.GeosphereNumSubdivisions = 1;

				if (ImGui::Button("Select Geosphere material"))
					ImGui::OpenPopup("my_select_geosphere_material_popup");
				ImGui::SameLine();
				ImGui::TextUnformatted(m_AddShape.GeosphereMaterial.c_str());
				if (ImGui::BeginPopup("my_select_geosphere_material_popup"))
				{
					for (auto e : m_MaterialsName)
						if (ImGui::Selectable(e.c_str()))
							m_AddShape.GeosphereMaterial = e.c_str();
					ImGui::EndPopup();
				}
				ImGui::InputFloat3("Geosphere Position", (float*)&m_AddShape.Pos);

				if (ImGui::MenuItem("Add Geosphere"))
				{
					m_AddShape.ShapeGeo = Geometry::Geosphere;
					AddShapeFlag(true);
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Cylinder"))
			{
				ImGui::Text("Creates a cylinder. The bottom and top radius form various cone shapes. The slices and stacks parameters control tessellation.");
				ImGui::InputFloat("Cylinder bottom radius", &m_AddShape.CylinderBottomRadius, 1.0f, 1.0f);
				ImGui::InputFloat("Cylinder top radius", &m_AddShape.CylinderTopRadius, 1.0f, 1.0f);
				ImGui::InputFloat("Cylinder height", &m_AddShape.CylinderHeight, 1.0f, 1.0f);
				ImGui::InputInt("Cylinder slice count", &m_AddShape.CylinderSliceCount, 1, 1);
				ImGui::InputInt("Cylinder stack count", &m_AddShape.CylinderStackCount, 1, 1);

				if (ImGui::Button("Select Cylinder material"))
					ImGui::OpenPopup("my_select_cylinder_material_popup");
				ImGui::SameLine();
				ImGui::TextUnformatted(m_AddShape.CylinderMaterial.c_str());
				if (ImGui::BeginPopup("my_select_cylinder_material_popup"))
				{
					for (auto e : m_MaterialsName)
						if (ImGui::Selectable(e.c_str()))
							m_AddShape.CylinderMaterial = e.c_str();
					ImGui::EndPopup();
				}
				ImGui::InputFloat3("Cylinder Position", (float*)&m_AddShape.Pos);

				if (ImGui::MenuItem("Add Cylinder"))
				{
					m_AddShape.ShapeGeo = Geometry::Cylinder;
					AddShapeFlag(true);
				}
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}
	}
}

void ImguiManager::ShowMaterialsHeader()
{
	if (ImGui::CollapsingHeader("Materials"))
	{
		for (int i = 0; i < m_MaterialsCount; i++)
		{
			if (ImGui::TreeNode(m_MaterialsName[i].c_str()))
			{
				ImGui::ColorEdit3("Diffuse Albedo", (float*)&m_MaterialsAlbedo[i]);
				ImGui::SliderFloat3("FresnelR0", (float*)&m_MaterialsFresnelR0[i], 0.0f, 0.99f);
				ImGui::SliderFloat("Roughness", &m_MaterialsRoughness[i], 0.0f, 0.99f);

				ImGui::TreePop();
			}
		}
		ImGui::Separator();

		if (ImGui::Button("Create Material"))
			ImGui::OpenPopup("Create Material popup");

		if (ImGui::BeginPopup("Create Material popup"))
		{
			m_NameAlreadyExists = false;

			ImGui::Text("Creates a material.");
			ImGui::InputText("Name", m_AddMaterial.Name, IM_ARRAYSIZE(m_AddMaterial.Name));
			for (int i = 0; i < m_MaterialsCount; i++)
			{
				if (m_MaterialsName[i] == m_AddMaterial.Name)
					m_NameAlreadyExists = true;
			}
			if (m_NameAlreadyExists)
			{
				ImGui::SameLine();
				ImGui::Text("(this name already exists)");
			}
			ImGui::ColorEdit3("Diffuse Albedo", (float*)&m_AddMaterial.Albedo);
			ImGui::SliderFloat3("FresnelR0", (float*)&m_AddMaterial.FresnelR0, 0.0f, 0.99f);
			ImGui::SliderFloat("Roughness", &m_AddMaterial.Roughness, 0.0f, 0.99f);

			if (ImGui::MenuItem("Create"))
				if (!m_NameAlreadyExists)
					m_MaterialCreateFlag = true;

			ImGui::EndMenu();
		}
	}
}

void ImguiManager::ShowModelsHeader()
{
	if (ImGui::CollapsingHeader("Models"))
	{
		for (auto& e : m_Models)
		{
			std::string name = e.first.c_str();
			std::string visible = name + " visible";

			if (ImGui::TreeNode(name.c_str()))
			{
				bool b = e.second;
				ImGui::Checkbox(visible.c_str(), &b);
				e.second = b;

				ImGui::TreePop();
			}
		}
	}
}